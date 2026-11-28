# 仿真引擎设计

## 1. 状态与范围

状态：SimulationSettings、标量编译、同步 Euler/RK4、不可变 Run 与取消/失败处理已实现并通过验证；结果界面处于设计阶段。

本文定义从现有 Document、Formula、AnalysisReport 和 EvaluationContext 到可运行模型的边界。功能阶段计划只记录在本文，不进入 `docs/history/`；实际演进由 git log 记录。

首个可运行闭环只包含：

- 标量 Stock、Flow、Variable；常量与辅助计算共用 Variable，由公式区分；
- 当前已经实现的纯数学、时间、条件和宏公式；
- Euler 与 RK4；
- 起止时间、DT、结果保存间隔；
- 独立运行结果、诊断、取消及性能 profile。

基本数组执行已实现，支持 Apply-to-All、显式元素、Stock/Flow/Variable、Euler/RK4 和独立 arraySeries；数组结果 UI、Lookup、随机函数、DELAY/SMOOTH、显式 INTEG 状态、Data 外部输入分别作为后续明确阶段，不因公式可以解析就视为已经支持。

## 2. 当前公式能力边界

`findFunctionSignature()` 只提供名称和参数个数，供语法检查使用，不代表存在求值实现。

| 类别 | 当前状态 | 仿真处理 |
| --- | --- | --- |
| 数学函数、比较/逻辑、IF、TIME/DT/STARTTIME/STOPTIME | 已可求值 | 首批复用 |
| 数组函数与数组运算 | 已可单独求值 | 首批标量引擎不自动声称支持数组 Stock |
| 表达式宏 | 已可求值，含默认参数和受控递归 | 首批可用于无状态纯公式 |
| INTEG/INTEGR | 只有签名，未实现 | 首批拒绝显式调用；Stock 使用自身初值与端点语义 |
| DELAY N/SMOOTH N 及一阶/三阶别名 | N 阶物料延迟/信息平滑已实现 | Plan 级联 Level、可变 delayTime、离散/连续输出和 Euler/RK4；见 [N 阶延迟设计](delay-functions.md)。其它状态函数待办 |
| STEP/PULSE/PULSE TRAIN/RAMP | 纯时间函数已实现 | 使用冻结测试输入时间/DT，支持数组广播和 Euler/RK4；见 [纯时间输入函数设计](time-functions.md) |
| RANDOM/NORMAL/LOGNORMAL/POISSON/EXPRND | 已实现 | Run 内随机流、固定种子、非零 Stream ID、标量/数组和 Euler/RK4；见 [随机函数设计](random-functions.md) |
| LOOKUP | 独立/内联 LookupDefinition 已实现 | 支持 continuous/extrapolate/discrete、标量/数组和 QDS/XMILE；其它 Lookup 函数另行实现 |

## 3. 模型语义

### 3.1 Stock 是唯一首批积分对象

XMILE Stock 的 `<eqn>` 表示初始值，Stock 的 inflow/outflow 与 FlowEndpoints 表示状态变化来源。内部 Stock 使用独立 `initialEquation`，不占用普通 equation；QDS 使用 `<initial>`，Vensim 的完整 INTEG 公式按端点派生。内部仿真语义为：

```text
stock(start) = evaluate(stock.initialEquation, initialContext)
stock'(t) = sum(inflow values) - sum(outflow values)
```

- 每个进入 Stock 的 Flow 对导数贡献正值，每个离开 Stock 的 Flow 贡献负值；Cloud 端不增加或减少其他 Stock。
- Stock 初值只计算一次；运行时不得把初值公式当作代数输出反复覆盖状态。
- 首批若 Stock 公式显式包含 INTEG/INTEGR，应报告不支持，而不是同时进行两次积分。
- 非 Stock 中的显式 INTEG 需要独立隐式状态槽，首批不实现。
- 不自动限制 Stock 非负；是否允许负值由模型公式决定。

### 3.2 其他变量

- Constant 在初始化时求值一次，运行期间保持不变。
- Auxiliary 和 Flow 在每个积分求值点按依赖顺序重新计算。
- Data 在外部时间序列语义实现前不参与首批运行。
- Connector、Cloud、Comment 没有独立数值状态；Cloud 只参与 Flow 端点关系。
- 无状态宏与普通公式使用同一求值器，不做宏体展开或复制。

### 3.3 数组执行

- 数组 ModelElement 使用与标量相同的初始化/运行期拓扑顺序。Apply-to-All 和逐元素方程均按当前下标求值：Apply-to-All 遍历维度笛卡尔积并设置 `EvaluationScope.dimensions/indices/self`，显式 `ArrayElementFormula` 使用其保存的下标。公式返回整体 ArrayValue 时也按当前下标提取，不把整组值误赋给单个元素。
- SimulationPlan 自持有每个数组变量的 ArrayLayout，编译期固定 dimensions、shape、strides 和元素数。Apply-to-All 直接在 Scope.indices 中以里程表方式递增，不为每个元素重新计算 strides 或分配坐标 QVector；显式元素使用预计算 strides 定位。Plan 不因此持有 Formula/AST/Document 指针。
- 数组 Stock 的每个元素是独立状态，具有独立初值、净流量和 Euler/RK4 阶段值。数组 Flow 可以返回标量或与 Stock 兼容的 ArrayValue；标量按 Stock 全部元素广播，数组按维度名重排后逐元素累加。缺少维度、维度数不同或形状不兼容时以 `incompatible-stock-flow` 失败，不按位置猜测。
- `Value=std::variant<double,ArrayValue>` 同时用于变量状态和净流量。标量保持原有逐元素一次求值路径；数组值检查形状、元素数及所有元素的有限性。数组维度或元素数溢出、非有限值和求值顺序错误均不得替换为零。
- `SimulationRun::series(nameKey)` 继续只返回标量序列，`arraySeries(nameKey)` 返回按采样时间排列的 ArrayValue；两者均返回 QVector 值，未知 nameKey、类型不符或零样本统一表现为空容器，变量存在性由 RunVariableInfo 元数据表达。现有 Graph/Table/Causes Strip 因没有标量序列而跳过数组，数组结果 UI 另行设计，不把数组首元素伪装成标量曲线。
- 首批数组执行覆盖 Apply-to-All、显式元素方程、数组 Stock/Flow/Variable、标量广播、维度重排、Euler/RK4、守恒和失败前缀。Vensim `:EXCEPT:` 混合分片、数组宏状态、Lookup/Data 和数组图表不属于本阶段。

## 4. 仿真设置与持久化

新增不可变输入完成后的 `SimulationSettings` 值：

```text
SimulationSettings
├─ startTime：double，默认 0
├─ stopTime：double，默认 100
├─ dt：double，默认 0.25
├─ saveInterval：double，默认等于 dt
├─ integrationMethod：Euler / RK4，默认 Euler
└─ timeUnit：QString
```

设置属于模型语义，不由 DiagramView 或 Workspace 持有。Document 通过受控接口保存和修改；编辑设置的 undo/redo 与公式、名称等修改使用同一命令体系，并增加文档级变化记录，不把设置伪装成某个元素；旧 SimulationRun 不随 undo 改变。

校验规则：

- 所有时间值必须有限；`stopTime >= startTime`，`dt > 0`，`saveInterval >= dt`。
- Vensim 资料要求 TIME STEP 不大于 SAVEPER；本项目首批进一步要求 saveInterval 是 dt 的正整数倍，避免通过插值伪造未计算的输出。后续如需任意 SAVEPER，必须单独定义采样语义，不能把本项目附加限制误写成 Vensim 规则。
- 首批固定限制总步数和保存点数均不超过 10,000,000，防止非有限循环或内存耗尽；确有需求时再设计参数化上限。
- 最后不足一个 dt 的区间使用明确的短步长，确保终点恰好为 stopTime；该步的 DT 使用实际步长。
- QDS 保存全部设置。XMILE `<sim_specs>` 映射 start/stop/dt/method/time_units，替换当前导出时硬编码的 0/100/0.25；标准没有 SAVEPER 字段，因此 XMILE 不保存 saveInterval，导入时令其等于 DT。XMILE 的 DT 缺省为 1，reciprocal=true 表示写入值是 DT 的倒数；RK2 按标准回退到 RK4，其他不支持方法必须明确报错。
- 文件缺省值使用上述默认值；非法设置导入失败且不修改原 Document。内部设置允许 stopTime==startTime，XMILE 按标准拒绝该零时长导出且不得覆盖已有文件。

## 5. 核心对象与所有权

```text
Document
└─ SimulationSettings              持久模型输入

SimulationCompiler
└─ Document + AnalysisReport → SimulationCompileResult

SimulationCompileResult
├─ optional<SimulationPlan>        仅编译成功时存在
└─ diagnostics

SimulationPlan                    与一个不可变 Document 副本配套的不可变执行描述
├─ settings
├─ compiledFormulas：模型/Stock 初值/逐元素绑定 AST
├─ compiledMacros：宏体和默认参数绑定 AST
├─ initializationOrder：EntityId
├─ evaluationOrder：EntityId
└─ stockBindings：Stock ↔ 流入/流出 EntityId

SimulationRunner
├─ const Document &               调用方保证运行期间不修改
├─ const SimulationPlan &
└─ SimulationState                当前时间、状态和取消标志

SimulationRun                     一次运行的完整不可变记录
├─ RunId / runName / modelRevision
├─ settings / method
├─ times
├─ EntityId → 标量时间序列 / ArrayValue 时间序列
├─ 完成/取消/失败状态与诊断
├─ RunVariableInfo：EntityId、displayName、nameKey、unit、kind
└─ causes：EntityId → 直接原因 EntityId 列表
```

约束：

- SimulationPlan 拥有全部 CompiledFormula/CompiledMacroDefinition 和绑定 AST；Document 唯一持有源文本和原始 AST，编译不修改 Document。Plan 仅可移动，内部编译对象使用稳定堆身份。详见 [统一名称管理与绑定 AST 设计](call-resolution.md)。
- RunId、runName、modelRevision、RunVariableInfo 和 causes 与数值结果属于同一个 SimulationRun，保证 EntityId 与运行副本元数据自洽；不再使用 NamedRun 平行保存这些字段。
- `withMetadata()` 返回补齐身份的新对象，原结果不变，禁止发布后改写元数据。RunId 是运行记录身份，不是 EntityId，也不是跨 Run 的模型元素 ID。
- 对外结果查询统一使用 nameKey：`series(nameKey)`、`arraySeries(nameKey)` 和 `causes(nameKey)`。EntityId 版本及 `RunVariableInfo::id()` 私有化，仅由 SimulationRun 完成运行副本内部映射；SimulationRunner 作为 friend 标记执行边界，不向应用层 Controller 开放 friend。
- Qt 容器利用写时复制，值拷贝不主动复制数值矩阵；Worker 和跨视图仍共享不可变对象。
- Plan 和 Run 不进入 Document，也不进入编辑器 ChangeSet。
- Document 修改后 Plan 必须重新编译；UI 不得让旧 Plan 静默作用于新模型。
- 首批运行采用同步调用，Document、Plan 和 Runner 的生命周期由同一调用栈保证。异步执行需要单独设计模型只读快照或生命周期共享，不能只把 raw reference 移入线程。
- Run 完成后不可变，可由 UI 以共享 const 对象读取；运行历史不属于 QDS 模型文件，后续多 Run 持久化另行设计。

### 5.1 执行性能重构的目标不变量

固定布局与无分配遍历、普通公式引用预绑定、Stock/Flow 布局对齐及单次运行 Context 复用均已实现。对象结构与基线见 [仿真执行计划性能重构方案](../history/2026-11-28-simulation-execution-plan-done.md)：

- SimulationPlan 包含 CompiledFormula、CompiledMacroDefinition、ArrayLayout 和 Stock/Flow 对齐的完整执行描述；全部执行数据由 Plan 自持有并在编译发布后不可变。BoundMacroCall 只指向 Plan 内编译宏，Lookup 绑定可指向配套不可变 Document 副本。
- ArrayLayout 固定 dimensions、shape、strides 和元素数；StockRatePlan 固定 Flow 方向及相同布局/重排映射。它们是编译结果，不是运行期可变缓存。
- 全局变量、Dimension、DimensionElement、SELF、TIME/DT/STARTTIME/STOPTIME、PI/INF 和调用在编译时转换为显式 Bound 节点，取代 ReferenceBinding。运行期不再按 nameKey 查询全局引用；宏局部帧仍优先，变量状态按 EntityId 读取。
- 每个 FlowRatePlan 固定 Flow、加减方向、直接平铺标志、兼容性和可选重排映射。相同布局不再逐元素调用 coordinates/flatIndex；重排映射由 ArrayLayout 在编译期生成。不兼容布局保留运行时 `incompatible-stock-flow` 和失败前缀，不改写成编译失败。
- EvaluationContext、ExecutionStack、当前下标和阶段 Value 作为单次 run() 的局部可变状态复用，不写回 Plan、不跨 Run/Worker、不进入历史结果。Run 为 ExecutionStack 预留 4 MiB Value 存储，参数和宏局部变量使用连续 slot 与 base index；每次 evaluate 只更新 time、实际 DT 和当前 target 指针，每个变量仍清空并重设 scope。绑定 AST 不再需要运行期宏 reader 或名称引用 reader。
- ArrayValue 以共享 ArrayData 堆句柄在 ExecutionStack、变量状态和 Run 采样间传递；求值参数只读，新结果独立分配，非 const 可变访问在句柄不唯一时克隆。发布到 Run 后不再写入，SimulationRunRequest、SimulationRun 和 Document/Formula 的其它所有权不变；每次模型运行仍重新编译，不使用需要手工失效的全局缓存。

## 6. 编译与执行顺序

### 6.1 编译阶段

1. 运行现有语法、名称、端点和函数参数检查，保留 EntityId 与 SourceSpan 诊断。
2. 拒绝首批明确不支持的状态函数、随机函数、Lookup、Data 输入和非标量状态。
3. 建立初始化依赖：Stock 初值、Constant 及被它们引用的 Auxiliary；只为初始化需求建立这套关系。
4. 建立运行期代数依赖：Auxiliary、Flow 以及输出所需的引用关系。
5. 分别进行拓扑排序；真正的代数环报错，经过 Stock 状态的反馈回路不是代数环。
6. 生成 Stock 到流入/流出 Flow 的绑定和稳定输出顺序。

现有 AnalysisReport 可提供名称诊断和通用依赖，但不能替代初始化/运行期两种语义不同的执行图。Compiler 只读遍历 Document 原始 AST，建立 EntityId 关系并在 Plan 中生成独立 CompiledFormula/绑定 AST；不修改源 AST，也不把绑定结果写回 Document。

### 6.2 与 Vensim 求值顺序对齐

依据用户提供的 Vensim 求值顺序说明；原文保存在本地、由 gitignore 排除的 `docs/references/vensim/evaluation-order.md`。其核心规则是：计算顺序由因果关系决定，与变量定义先后无关；初始化环和运行期代数环分别诊断。

本设计对应为：

1. **Preparation**：先求值全部 Constant。Data 读取、Data 方程和初始时间数据值属于后续 Data 阶段；首批含 Data 输入时明确拒绝运行。
2. **Initialization**：按初始公式依赖递归求值 Stock；尚未计算的输入按需递归计算，直到数值常量、外部输入或检测到环。初始环报告 simultaneous initial equation 类诊断，不能当作普通运行期环。该过程可产生初始化专用 Variable 值。
3. **Computation of Auxiliaries**：在 Stock 已有初值后，按运行期依赖计算全部 Auxiliary 和 Flow；未计算输入按需递归，终止于 Constant、Data 或当前 Stock。实际拓扑顺序在运行前确定，不依赖 Document 创建顺序。
4. **Storage of Results**：普通 Euler/RK4 在新速率计算完成后保存当前 Stock 及由这些 Stock 产生的 Auxiliary/Flow，即“Level 和由 Level 得到的值”。Difference 虽然使用相同的状态推进，但在新速率计算前记录，即“Level 和产生这些 Level 的值”；首次记录时部分 Auxiliary 可为缺失值。Diff/Difference 不在当前首批范围内，后续实现不得复用 Euler 的记录时点。
5. **Computation of Net Rates**：基于当前 Auxiliary/Flow 计算每个 Stock 的净流入。
6. **Integration**：Euler 推进一次；RK4 重复 Auxiliary/Flow 和净流量求值，但不保存阶段结果。推进后 Time 与全部 Stock 同时进入下一时刻。
7. **Continuation**：在下一时刻重复 Auxiliary 计算、按需存储、净流量和积分，直到 stopTime。

Vensim 的 ACTIVE INITIAL 会使初始化阶段与正常运行阶段的 Auxiliary 值不同，并按阈值报告；首批不支持 ACTIVE INITIAL，因此不得伪造该比较。Reality Check 测试输入覆盖也不属于当前范围。后续实现这些能力时，必须保留上述阶段差异。

### 6.3 每个求值点

1. EvaluationContext 设置当前 time、实际 dt、startTime、stopTime。
2. 按 evaluationOrder 计算 Auxiliary 和 Flow，写入当前变量值表。
3. 对每个 Stock 读取绑定的 Flow，计算净流入导数。
4. VariableReader 只接受当前值表中已经计算的值；顺序错误或缺失值直接报错，不以 0 代替。

RK4 的每个阶段都重新计算全部相关 Auxiliary 和 Flow，不能复用上一阶段的中间值。

## 7. 积分算法

设当前时间为 `t`、步长为 `h`、Stock 向量为 `S`，净流入函数为 `f(t, S)`。本节依据本地保存的 `docs/references/vensim/integration.md`，并区分资料语义与本项目首批附加约束。

Euler：

```text
S(t+h) = S(t) + h * f(t, S)
```

Euler 假定当前时刻求得的速率在整个步长内保持不变。单步误差与 h² 同阶，完整仿真的全局误差与 h 同阶；这与文档中的“Euler 不够精确”原因一致。

RK4：

```text
k1 = f(t,     S)
k2 = f(t+h/2, S + h*k1/2)
k3 = f(t+h/2, S + h*k2/2)
k4 = f(t+h,   S + h*k3)
S(t+h) = S + h*(k1 + 2*k2 + 2*k3 + k4)/6
```

- k1～k4 是完整 Stock 向量对应的阶段导数，不是逐个 Stock 独立推进。
- 阶段值写入独立工作区，只有整步成功后才提交为新的当前状态。
- 时间由起点、步序号和 dt 派生，并对终点使用短步长，避免累积浮点加法无限接近 stopTime。
- 初始点、每个保存间隔和最终停止点写入结果；RK4 阶段值不作为时间序列样本。
- 任何阶段产生 NaN/Inf、读取失败或取消时，不提交该阶段；Run 保留已完成前缀并报告失败/取消，不把它标记为成功。

### 7.1 精度与固定步长

Vensim 资料给出的误差阶数为：RK2 单步 O(h³)、全局 O(h²)；RK4 单步 O(h⁵)、全局 O(h⁴)。本项目首批只实现固定步长 Euler 和 RK4；RK2 可作为后续独立方法，不因公式简单就自动加入。

固定 DT 下，Euler 每个区间计算一次净流量，RK2 两次，RK4 四次。可以把求值次数作为 profile 的正确性计数，但不得宣称实际墙钟时间严格呈 1:2:4，因为结果存储和其他工作也占用时间。

TIME STEP 的选择至少考虑：

- 小于模型显著变化的最短周期；固定步长方法通常还应小于最短时间常数的 1/3。
- 不大于 SAVEPER，并能按合适频率读取测试输入和外部数据。
- 若出现逐步正负交替、周期约为 2×DT 的可疑振荡，应减小 DT 检查数值稳定性，而不是把它直接解释为模型行为。原文使用“frequency near twice TIME STEP”的说法存在量纲问题，测试应针对逐步振荡现象，不复述该量纲错误。
- 上述经验规则不是正确性证明；最终以解析解、阶数、参考模型和黄金文件验证为准。

### 7.2 自动调整步长（后续）

Vensim 的自动 RK 步长不是首批功能。后续专项设计必须对齐以下资料语义：

1. 先执行一个完整 RK2/RK4 步，再用两个半步重复；比较两种结果。
2. 任一 Level 的差值同时超过 ABSOLUTE TOLERANCE 和由 RELATIVE TOLERANCE 隐含的相对误差界限时，判定失败并将内部步长减半；整个 TIME STEP 未检测到误差时可加倍，但不超过 TIME STEP。
3. 两个容差可由模型常量方程提供，只使用初始值，缺省均为 0.001；不能每个阶段重新读取变化的容差。
4. 内部步长不作为 TIME STEP 或结果公开；Vensim 将其限制为不小于 TIME STEP/256，无法达到精度时在每个失败区间警告。
5. 可变 TIME STEP、内部步长状态和“至少与固定半步一样准确”的资料声明都需要独立验证，不能未经验证并入固定步长 Runner。

## 8. 结果与运行控制

首批每个可见语义变量可以输出一条标量序列；内部派生缓存不进入公开结果。结果中的 EntityId 用于回查当前 Document，名称显示在读取时完成，不把 displayName 当作结果身份。

Runner 提供同步 `run()` 和协作式 `cancel()` 语义。首批 UI 可以先保持模态进度并处理取消；引入后台线程时，取消标志必须线程安全，且不得在运行中修改 Document。

Runner 的 ExecutionStatus 区分 NotStarted、Running、Completed、Cancelled 和 Failed。SimulationRun 只表示已经进入 Completed、Cancelled 或 Failed 的终态结果，不表示 NotStarted/Running。

错误报告包含运行状态、已保存样本数、失败时间、EntityId、公式 SourceSpan 和可读原因。取消不是数值错误，失败也不删除已有历史 Run。

## 9. 状态函数与随机性的后续边界

这些功能不能通过在当前 Evaluator 中增加静态变量实现：

- INTEG、DELAY、SMOOTH、INIT、PREVIOUS 等每个动态调用实例需要独立状态槽；状态身份必须包含公式调用位置、数组元素和宏调用路径。
- 宏递归会动态产生调用实例，不能在未定义状态分配规则前支持带状态宏。
- 随机数生成器归单次 Run 所有，固定种子必须可复现；Vensim 每次实际调用都取新值，因此 Euler/RK4 每个阶段、宏实际调用和数组元素均独立取样。详见 [随机函数设计](random-functions.md)。
- STEP/PULSE 的跳变点归属和 DELAY 初始化必须先形成书面规范及边界测试。Lookup 的域内/域外规范与测试见 [Lookup 模型与求解器设计](lookup.md)。
- Vensim RK 积分在一个区间内冻结外生输入和 STEP/PULSE/RAMP 测试输入；纯延迟 FIXED/MATERIAL/INFORMATION 在该 RK 计算内不变化，MESSAGE 总返回零。TIME 本身仍按 RK 阶段推进，不能把“冻结测试输入”误实现为冻结时间。STEP/PULSE/PULSE TRAIN/RAMP 已实现该边界；外生 Data、延迟和 MESSAGE 仍不支持。
- 自动步长 RK 的输入冻结和状态提交边界也必须在对应专项中验证，不能沿用每个内部子步重新读取测试输入的实现。
- 上述功能分别设计、实现、测试和 profile，不作为首批标量引擎的隐含承诺。

## 10. 正确性验证

每个引擎阶段同时提供 Qt Test 和可重复 Release profile，先 Release 后 Debug 全量回归。

首批自动化验证至少包括：

1. **解析解**：指数增长/衰减、Logistic、二阶欠阻尼振荡；容差随方法和 DT 标定，不对所有场景共用一个任意容差。
2. **积分阶数**：步长逐次减半，Euler 全局误差呈 O(DT)，RK4 呈 O(DT⁴)；同时验证阶段公式读取正确时间。
3. **存量恒等式**：终值等于初值加全部净流入积分；封闭子系统总量守恒，同一 Stock 的流入与流出不会重复计算。
4. **执行顺序**：多层 Auxiliary、双向反馈经 Stock、初始化依赖、代数环和缺失值。
5. **时间边界**：零时长、最后一个短步、保存间隔、取消、NaN/Inf、溢出保护。
6. **真实模型**：使用 SDXorg ABS、eval_order、Teacup、chained_initialization 和 SIR 的对齐设置及参考结果；与 Vensim 比较时必须使用相同积分方法和 DT。SIR 的守恒量是初始 Stock 总和 1005，而 total_population=1000 仅用于感染率归一化。参考 CSV 按文本有效数字设置舍入容差，解析解测试另按积分方法和 DT 标定数值误差。
7. **黄金文件**：保存确定性基准输入、样本时间、结果与容差；文件包含生成语义版本，不能用当前错误结果重写黄金文件来消除失败。

## 11. 性能 profile

新增独立 SimulationProfile，不混入公式或编辑器计时。固定模型规模、步数、保存点数和积分方法，至少输出：

- compile；
- Euler / RK4 每步和每求值点耗时；
- 结果写入与按 EntityId 查询耗时；
- 实际步数、保存点数、状态数和结果校验和；
- 可选峰值内存及结果内存估算。

优化前必须保留正确性校验和与同场景基线。不得通过减少保存点、跳过公式或把失败运行计入吞吐量改善数据。

## 12. 分阶段实施

1. **设置与模型边界**：SimulationSettings、Document 校验、QDS/XMILE 设置往返及不支持积分方法诊断。
2. **标量编译与 Euler**：初始化/运行期拓扑排序、Stock 净流入、同步 Runner、不可变 Run、解析解和不变量。
3. **RK4**：完整阶段状态、实际 DT、阶数与阶段时间验证。
4. **运行控制与结果接口**：取消、失败前缀、进度及 UI 只读访问；多 Run 管理单独审查。
5. **结果可视化**：折线图、数据表和导出；图表库经原型 profile 后决定。界面结构、不可变运行副本和 Worker 生命周期见 [模拟器界面设计](simulation-ui.md)。
6. **扩展语义**：数组执行、纯时间输入、Lookup、历史/状态函数、随机函数和外部 Data，各自独立设计门禁。Lookup 核心模型与求解边界见 [Lookup 模型与求解器设计](lookup.md)。
7. **实验能力**：参数扫描、多 Run 对比和蒙特卡洛，在单 Run 正确性与复现性验证完成后实施。

每个阶段独立实现、测试、profile、Release 后 Debug 全量回归和审核；不在一个阶段中顺带实现后续函数或 UI。

## 13. 当前待审查决策

- 首批只把 Stock 作为积分对象，显式 INTEG 延后。
- SimulationSettings 归 Document，运行 Plan/State/Run 归独立仿真层。
- 首批采用同步运行，不在未解决 Document 生命周期前引入后台线程。
- saveInterval 首批限定为 DT 整数倍，最后允许短步。
- 非有限数值立即失败并保留已完成前缀，不静默截断或替换为 0。
