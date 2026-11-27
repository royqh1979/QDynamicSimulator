# Vensim MDL 导入设计

## 范围

首版实现文本 `.mdl` 导入，不实现导出。原始格式依据 `docs/references/vensim/mdl-format.md`，不得用简单 `|`、`=` 或行号切割代替结构化解析。

## 解析边界

- 接受 UTF-8 文本及可选 `{UTF-8}` 标记；文件错误、缺少必要记录、重复定义和无法映射的结构必须明确报错并保留原 Document。
- 方程记录识别 `=`、`:=`、`==`、单位、说明、范围和续行。单位与说明进入 ModelElement；范围当前不持久化，产生明确 warning。
- 不建立 Auxiliary/Constant/Data 类型分支，统一映射到 Variable；Stock 的 INTEG 拆分为净导数与 initialEquation。
- 支持基本 subscript range、显式元素方程、数值范围展开和二维字面量数组。复杂 Subrange、exception、mapping 只在当前 Core 能完整表达其语义时接受，否则明确拒绝。
- 支持无输出参数/内部变量的宏定义，映射到 MacroDefinitionData。其它宏形式明确拒绝，不静默丢弃。
- 采用两阶段处理：先收集维度、宏和全部变量定义，再解析下标、归并定义、转换公式和建立元素。不能按文件顺序要求前向引用已经存在。

## 一个变量的多个方程

Vensim 的多方程是数组变量的分片定义，不是对同一值按顺序重复赋值：

- `price[banana]`、`price[apple]` 分别定义不同数组元素，各元素只使用自己的方程。
- 带下标 Stock 的每条 INTEG 定义对应独立状态，分别保存初值并积分。
- 通用方程可用 `:EXCEPT:` 排除部分下标，再由显式元素方程定义这些例外；同一元素不得被重复定义，未定义元素没有值。
- 同一非标量变量的多个普通定义属于错误，不能采用后定义覆盖先定义。

导入纯逐元素定义时，按规范化变量名归并，保留首个非空单位和说明，把方程转换为互不重复的 `elementEquations`。此时总体 `equation` 和 Stock 的总体 `initialEquation` 必须为空，以满足 Core 的互斥约束。字面量数组也展开成同一组逐元素方程，不另建平行表示。

当前 Core 不能同时保存 Apply-to-All 与例外方程，仿真器也尚未实现数组 Stock/Variable 执行。因此首版只保证数组定义的导入、检查和持久化；含 `:EXCEPT:` 的混合定义明确拒绝，不得为了通过校验而丢弃通用方程，也不得声称数组模型可以模拟。数组仿真的后续实现必须先扩展 Core 的分片定义语义。

## 名称与公式转换

QDS 公式沿用 XMILE 引用形式：XML/模型元素的 display name 可以包含空格；不含空格或特殊字符的名称可裸写，含空格或特殊字符的 display name 在公式中使用双引号。中文等 Unicode 名称可以裸写。大小写、空格和下划线的规范化仍统一使用 `normalizeIdentifier()`。

例如 XMILE 属性与公式分别写成：

```xml
<flow name="Heat Loss to Room">
    <eqn>("Teacup Temperature"-"Room Temperature")/"Characteristic Time"</eqn>
</flow>
```

Vensim 允许多词名称在公式中裸写，导入器负责转换：

```text
Heat Loss to Room / Characteristic Time
    → "Heat Loss to Room" / "Characteristic Time"
```

该转换需要完整的符号分析，不能用普通字符串替换伪装实现：

1. 收集全部变量、维度、维度元素和宏；宏转换使用局部参数与全局定义组成的作用域。
2. 扫描引号、转义、运算符、括号、下标和函数调用，在引号外识别 Vensim 名称并优先匹配最长名称。
3. 已加引号的名称保持引用语义并正确反转义/再转义；需要引号时输出 display name，而不是通过下划线转换规避符号分析。
4. 无法确定的名称边界、未知结构或歧义必须报错；不能把猜测结果写入 Document。

不在通用 Formula Parser 中允许裸空格名称。导入转换和 Parser 都需要名称结构分析，但导入器可以在完整模型上两阶段处理并拒绝歧义；交互 Parser 还必须处理未完成输入和未定义引用，若让空格名称依赖当前 Document 分词，新增或重命名变量可能改变同一文本的 AST。把 Vensim 方言限制在 I/O 边界，保持 QDS 的“纯文本 → AST”规则唯一且上下文独立。

## 语义映射

- Stock、Flow、Variable 分别对应 Level、Rate 和广义 Variable。
- 简单净导数按标识符符号连接已有 Flow；无法映射为现有 Flow 的净导数使用确定性的合成 Flow 表达，不能丢失 Stock 方程。
- 每个 Flow 都必须有不同的有效 Stock/Cloud 端点。导入时先建立 Stock 和普通 Variable，再根据全部 INTEG 净导数计算每条 Flow 的最终端点，最后一次性创建 Flow 及实际需要的开放 Cloud。不得为了绕过端点校验先创建两个占位 Cloud 再替换；真正未连接 Stock 的开放 Flow 才使用两个 Cloud，任何导入结果都不得遗留未连接的占位 Cloud。
- FINAL TIME、INITIAL TIME、SAVEPER 和 TIME STEP 映射 SimulationSettings；`SAVEPER=TIME STEP` 必须在全部记录读取后跟随最终 DT，不能受记录顺序影响。时间单位只取控制记录的单位并剥离范围，默认 Euler。
- 只读元数据、分组、视图默认样式和非模型对象不得进入仿真变量索引。

## Sketch 映射

- `V300` 起始标记后，类型 10 映射变量，类型 11 映射 Flow 阀门，类型 12 映射 Cloud 或 Comment；30/31 等不支持对象产生 warning。
- 类型 12 的 `bits` 第 3 位表示下一行是 Comment 正文；48 图标表示 Cloud。Comment 正文不得误用声明行的图标编号，I/O 对象等非普通 Comment 结构明确报告或忽略并 warning。
- 类型 1 按对象 ID 重建连接。阀门与 Stock 之间的 Flow 箭头用于确认端点；其余合法变量/阀门/Comment 箭头映射 Connector，不得误删真实因果关系。
- 坐标使用像素中心。路径中间点转换为现有相对 RoutePoint；单点使用 Bezier，多点使用 Straight。
- 无 Sketch 时使用确定性自动布局，不能因此拒绝模型。

## API 与错误

新增 `io::MdlFile::importFile/importText`，解析到临时 Document 成功后替换。接口额外返回 ModelIssue warnings。DocumentFile 根据后缀选择 `.qds`、`.xmile/.xml` 或 `.mdl`，主窗口与拖放接受 `.mdl`。

## 验收与 profile

- Rabbit/Teacup/ABS 的方程、单位、初值、端点、设置、Sketch 和实际模拟；WFINV 仓库夹具验证三条 Flow 只产生三个实际开放 Cloud，且无孤立占位 Cloud。
- Unicode 名称、引号、续行、注释、数组、数值范围、宏、多方程归并、重复定义及损坏输入。
- 带空格名称转换后的公式必须通过现有 Formula 分析，并在适用模型上完成实际仿真。
- 测试保存/加载往返保持语义，QDS 使用统一 Variable。
- `profile-mdl` 记录小/中模型导入耗时；Release 后 Debug 全量回归和可用 MDL 语料验证。
