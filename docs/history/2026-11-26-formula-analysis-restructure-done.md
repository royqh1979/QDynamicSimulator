# Formula 归属与 Analysis 重构计划

创建时间：2026-11-26 16:30:23 +0800

状态：已完成并合并；Formula 归属与 AnalysisReport 迁移通过全量验证

## 1. 决策

不再保留当前镜像 Document 的 ModelAnalysis。目标不是简单地把所有分析字段塞入 Document，而是：

- 局部 Formula 随 ModelElement 或 MacroDefinition 归属 Document；
- 全局 AnalysisReport 按需生成，完成后不可变，不拥有或复制公式 AST；
- 仿真 ExecutionPlan 属于运行期，不与编辑器 Validation 共用大而全的状态对象。

现有 ModelAnalysis 中的 ElementAnalysis、MacroAnalysis 及其下标/指针关系随迁移删除，不建立兼容层或第二套平行模型。

## 2. Formula 值对象

### 2.1 逻辑内容

Formula 是“一条原始公式及其本地解析结果”的不可变值对象：

```text
Formula
├─ source：QString 原始公式
├─ root：不可变 AST，可为空
└─ issues：本地语法诊断
```

FormulaAnalysis 不继续作为另一套类型存在；现有 AST/issues 内容迁移为 Formula，不新增平行 ParsedFormula。

### 2.2 不可变性与替换

- Formula 构造完成后，source、AST 节点和 issues 不提供修改接口。
- 编辑公式时从完整新文本构建新 Formula，再替换旧值；不修改旧 AST。
- Formula 可以整体移动/复制或替换，具体复制成本由实现和 profile 决定；不可变不代表禁止值替换。
- root() 只返回 const FormulaNode*。该指针只能在当前调用栈上同步使用，所属 Formula 被替换或其 Document 所有者被修改/移动/删除后必须重新获取。
- 空公式使用空 source、空 root、无 issues。语法失败保留原始 source、结构有效的部分 AST 和 issues，不构造含空子节点的节点。
- 本地解析不解析模型符号，因此无需前置定义，也不要求 Document 中其它元素已经存在。

### 2.3 ModelElement

ModelElement 只保存一个 Formula 作为总体公式，以及逐元素 Formula 集合：

```text
ModelElement
├─ formula：Formula
├─ dimensions：QVector<QString>
└─ elementFormulas：QVector<ArrayElementFormula>
```

`equation()` 如保留，只能是返回 formula.source() 的只读便利接口，不再指向另一份字符串状态。

- setEquation：先构建新 Formula，再替换总体 Formula并清空逐元素集合。
- setDimensions：更新维度并清空逐元素集合，保留总体 Formula。
- setElementEquation：替换对应 ArrayElementFormula，清空总体 Formula。
- Capture/Apply/Restore、复制粘贴和 QDS/XMILE 只交换公式源文本及下标；接收方重新构建 Formula，不持久化 AST。

### 2.4 数组逐元素公式

ArrayElementEquation 迁移为 ArrayElementFormula：

```text
ArrayElementFormula
├─ indices：有序零基下标
└─ formula：Formula
```

不把“原始 equation 字符串 + 另行保存的 AST”拆成两个平行字段。下标合法性、维数、覆盖完整性和重复检查仍由 Document 在写入时负责。容器与构建接口需要适应 Formula 的不可复制节点所有权，不能为了 Qt 容器统一而要求 AST 可复制。

### 2.5 宏

MacroDefinition 的宏体和 MacroParameter 的默认值使用同一 Formula 表示：

- 宏体只有一份 Formula，不复制到调用方；
- 默认参数也只有一份 Formula；
- requiredArguments() 和默认参数顺序使用“是否存在默认值”语义，不把 AST 指针作为存在标志；
- 宏调用方只保留 CallNode；
- 宏修改时替换对应 Formula，调用方 AST 不变；
- MacroAnalysis::definition 指针及独立宏 AST 容器随之删除。

## 3. AnalysisReport

### 3.1 内容边界

AnalysisReport 只包含全局派生结果：

```text
AnalysisReport
├─ DependencyGraph：EntityId 关系
├─ StructuralUse：EntityId 集合
└─ issues：全局及引用本地公式位置的诊断
```

不保存 Document*、ModelElement*、MacroDefinition*、AST 指针或 ElementAnalysis 容器。诊断中的 SourceSpan 仍是公式内的 QString 字符位置。

当前已有的 per-element references 若只用于生成依赖图和 StructuralUse，则在分析遍历中直接消费，不再长期保存。未来确有调用方需要时，再以明确的 dependent→dependencies 关系提供，而不是恢复影子元素模型。

### 3.2 构建与只读

- 使用 Analysis.cpp 内部 Builder 收集可变状态；完成后生成不可变 AnalysisReport。
- 主存储和查找优先使用合适的 QHash/QMap/QSet，不使用 vector 下标索引。
- DependencyGraph 直接拥有 EntityId 到依赖关系的关联内容。
- 输出需要稳定顺序时在 Builder 完成前排序 issues，不能依赖 Document 哈希遍历顺序。
- Document 修改后旧 Report 视为过期；当前每次需要时重新生成，不提前引入缓存或增量失效框架。

### 3.3 Validation 与 ExecutionPlan

`validateDocument()` 返回 AnalysisReport 中的诊断，不维护另一套公式扫描。后续执行器从 Document 和 AnalysisReport 构建自己的 ExecutionPlan；运行状态、历史函数和随机状态不进入 Report 或 Document。

## 4. 迁移前置条件：统一宏调用

当前 expandFormula() 会把宏体展开为 ModelAnalysis 中的新 AST。若先移动 Formula 所有权，会同时存在源 AST 和展开 AST，破坏单一权威表示。

因此必须先完成统一宏运行期调用：

1. 所有宏调用保留 CallNode，不做文本或 AST 展开。
2. 实参在调用方按值求值一次，默认参数在当前宏局部作用域求值。
3. 非递归、直接递归和间接递归共用调用帧机制，最大深度 64。
4. 分析期通过宏调用关系和依赖闭包收集全局依赖，不复制宏体。
5. 完成递归、重载、默认参数、数组、修改后生效测试和宏 profile 后，再进入 Formula 归属迁移。

这一步本身是独立功能门禁，不与 Formula 迁移混在同一个半完成实现中。

## 5. 无平行模型的迁移顺序

1. **统一宏运行期调用**：移除 expandFormula 及 recursive-macro 旧诊断，建立后续归属迁移的前提。
2. **Formula 替代 FormulaAnalysis**：加入 source，并一次性迁移 ModelElement、ArrayElementFormula、MacroDefinition 和 I/O 源文本访问。
3. **AnalysisReport 替代 ModelAnalysis**：直接遍历 Document 中的 Formula，删除 ElementAnalysis/MacroAnalysis 及其查找需求。
4. **全链路迁移**：Validation、求值、QDS/XMILE、Capture/Apply/Restore、复制和 undo/redo 使用同一 Formula 源文本通道。
5. **验证与 profile**：覆盖无效公式、数组、宏修改、撤销恢复、中文和 SourceSpan；比较宏及完整公式 profile。

不在中间提交中保留“Document AST + ModelAnalysis AST”的平行可用实现。每个独立阶段仍需完成测试、profile、Release 后 Debug 和审核。

## 6. 验收

- 任一公式只有一份当前权威 Formula；宏不展开复制。
- Document 中定义与 Formula 同步原子替换，无效输入仍保留可编辑源文本及诊断。
- AnalysisReport 不持有 AST/Document 指针，不包含影子元素索引。
- 不需要 EntityId→ElementAnalysis 查找；依赖和诊断使用 EntityId 直接对应 Document。
- 现有编辑、撤销、数组、宏、QDS/XMILE 和中文行为通过回归。
- Release/Debug 全量通过，并具有同场景宏与公式 profile。

## 7. 待审查事项

- Formula 的复制策略：深拷贝、不可变共享节点，还是只允许移动；需结合 Capture/ChangeSet 和 profile 决定，不提前选择 shared_ptr。
- ArrayElementFormula 在可变编辑 DTO 与不可变完成值之间的最小构造接口。
- Analysis Builder 的最小私有接口及宏签名键的表示。
- 本地解析诊断如何与全局诊断稳定排序及定位到数组/宏的具体公式。

## 8. 记录

- 2026-11-26 16:30:23 +0800：确定取消影子 ModelAnalysis 的目标，细化 Formula、AnalysisReport、宏前置条件和无平行模型迁移顺序；待审查后实现。

## 9. 宏签名的输入与输出数量（2026-11-26）

通用宏签名使用：

```text
nameKey + inputCount + outputCount
```

其中 outputCount 是**包括宏名自身主输出在内的总输出数**。Vensim `:MACRO: name(in1,in2:out1,out2)` 中的 out1/out2 是附加输出，因此该示例的 outputCount=3，而不是 2。当前单输出表达式宏的 outputCount=1。

输入个数不包括输出参数；输出参数也不作为输入重复计数。重载匹配需要名称、实际输入个数和调用上下文要求的输出个数共同确定。默认输入参数仍按可接受输入范围处理，不得造成同一调用的歧义。

当前 AST 的 CallNode 和 Value 仍只支持单输出表达式宏。签名字段可以表达 outputCount，但在实现多出参语法、调用结果和本地输出变量之前，必须明确拒绝 outputCount!=1，不得仅修改索引就宣称支持 Vensim 多出参宏。

- 2026-11-26 16:54:16 +0800：根据用户提供的 Vensim `Defining Macros` 规则补充输入/输出数量签名，明确总输出数包含宏名主输出；当前仍只实现单输出表达式宏。

## 10. 统一宏调用验证记录（2026-11-26）

- 删除分析期 expandFormula、宏展开栈和 recursive-macro 错误路径；所有调用保留 CallNode。
- Document 使用签名关联容器直接保存宏定义，macros() 提供无复制 const 视图；defineMacro/setMacro 共用规范化和参数校验。
- ModelAnalysis 的宏表使用支持移动语义的 std::map，因为 MacroAnalysis 当前含不可复制 AST；没有为 QVector/QMap 强行复制 AST。
- 求值器实现实参单次求值、独立局部帧、默认参数顺序求值、局部名称遮蔽、数组局部参数、直接/间接递归及 64 层限制。
- 分析器记录宏体与实际使用的默认参数的引用/调用，并计算依赖闭包；递归调用关系不作为错误。
- 测试覆盖重载、直接/互递归、重载递归、默认参数引用前参、默认参数不读取调用方局部变量、实参只读取一次、数组实参、默认参数依赖闭包、深度边界和宏修改后生效。
- Release 与 Debug 完整 CTest 均为 12/12。

本机 `profile-formula.exe 1000`：原宏场景分析 2.8092 μs/公式、求值 0.521 μs/公式；独立递归场景分析 1.65 μs/公式、求值 1.7599 μs/公式；递归公式节点总数固定为 2000（每个公式 2 个节点），没有展开增长。上一阶段原宏场景分析为 14.8074 μs/公式，求值为 0.6935 μs/公式；记录差异供后续比较，不替代跨场景解释。

- 2026-11-26 17:25:16 +0800：统一宏运行期调用实现、测试、profile 和 Release/Debug 全量回归完成，等待审核。

## 11. Formula 归属与 AnalysisReport 验证记录（2026-11-26）

- `Formula` 统一持有原始 QString、不可变 AST 和本地 issues；ModelElement、逐数组元素公式、宏体及宏默认参数均直接持有 Formula，不再由 ModelAnalysis 复制。
- 可编辑/持久化输入分别使用 ArrayElementEquation 和 MacroDefinitionData；长期 MacroDefinition 不可复制，由 Document 通过 `std::unique_ptr` 唯一持有，MacroRange 提供只读无复制遍历。
- AnalysisReport 仅保留 DependencyGraph、StructuralUse 和 issues；references 从当前依赖边即时计算。求值通过 `makeMacroReader(document)` 即时查询当前宏，旧 ModelAnalysis、ElementAnalysis、MacroAnalysis 和 FormulaAnalysis 类型已删除。
- Capture/Apply/Restore、重命名、数组编辑以及 QDS/XMILE 均只交换公式源文本，接收方重新构建 AST；没有持久化 AST 或平行公式状态。
- 自审修正了求值测试在 Document 修改后继续使用旧 Formula 引用的生命周期错误，并增加无效公式保留 source、部分 AST 和 issues 的回归断言。
- Release 与 Debug 全量 CTest 均为 12/12；应用、全部测试和全部 profile 目标的 Release 构建通过。

本机 `profile-formula.exe 1000`：基础公式分析 4.9583 μs/公式、Document 分析 3.1495 μs/公式、求值 0.7814 μs/公式；数组分析 3.2297 ms（6000 条公式）、数组求值 1.7606 μs/公式；宏分析 1.6326 μs/公式、求值 0.4427 μs/公式；递归宏分析 0.9678 μs/公式、求值 1.6007 μs/公式。结果校验包括 11000 个基础 AST 节点、6000 个依赖/引用、基础求值校验和 -6775、数组求值校验和 27000、宏校验和 12000、递归节点 2000，均符合固定预期。

- 2026-11-26 18:40:27 +0800：Formula 归属、AnalysisReport、求值和 I/O 全链路迁移及自审完成，等待用户审核；未提交、未合并。
