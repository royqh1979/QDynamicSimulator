# 模拟器界面设计

## 1. 范围与当前基础

状态：运行会话、SimulationSettingsDialog、Run Name/Enter、Run/Cancel、设置撤销和诊断定位已实现；最小 Run results Dock 已覆盖 Workbench Variable、最新优先 Datasets、加载状态、单变量表格、历史名称和过期提示。Qt Charts 基础折线图已实现，支持多 Run 叠加、图例显隐、缩放、平移和完整范围复位；多变量可见性、最近样本提示与结果导出尚未开始。

当前 Core 已提供 SimulationSettings、SimulationPlan、同步 SimulationRunner 和不可变 SimulationRun。本设计覆盖建模到运行的完整工作流、命名结果、Workbench Variable、折线图和数据表。命名多 Run 与叠加比较属于基本结果管理；数组、参数扫描、批量实验和蒙特卡洛仍不提前混入首批实现。

参考资料为用户提供的 Vensim Rabbit 建模/分析教程，原文摘录用本地 gitignore 目录 `docs/references/vensim/` 保存。Vensim 的单击建模方式仅作流程参考，不覆盖本项目已经确定的拖放元素和左键拖拽连线交互。

目标流程：

```text
创建模型 → 编辑结构/方程 → 模型与单位检查 → 命名并运行 → 选择 Workbench Variable → 折线/表格/多 Run 分析
```

界面只调用 Core 公开接口，不在 Widget、Action 或图表中实现公式解析、拓扑排序或积分算法。

## 2. 主窗口组织

### 2.1 Simulation 菜单与工具栏

新增顶层文字菜单 `Simulation`，不设置主菜单图标。菜单和模拟工具栏共享同一批 QAction：

| Action | 默认快捷键 | 作用 |
| --- | --- | --- |
| Run | Ctrl+R | 编译当前 Document 并开始一次新 Run |
| Cancel | Esc，仅运行时捕获 | 请求当前 Worker 取消，不影响编辑器其他 Esc 操作 |
| Graph | — | 在结果区显示 Workbench Variable 的全部已加载 Run 折线 |
| Table | — | 在结果区显示同一变量的逐时刻数据表 |
| Causes Strip | — | 后续显示 Workbench Variable 及其直接原因 |
| Runs Compare | — | 后续比较前两个已加载 Run 的 Constant/Lookup 差异 |
| Simulation settings | — | 打开设置对话框 |
| Export results | — | 导出当前选中变量的 CSV；实现时单独审核文件错误语义 |

- 菜单与工具栏不得分别创建行为对象；图标仍使用极简 SVG。
- Run 在已有运行进行中禁用，Cancel 只在没有运行时禁用。
- 运行期间仍可编辑下一次运行的设置；修改不写入 activeRequest，当前 Worker 继续使用其固定设置，已有结果被标记为旧模型结果。
- 状态栏显示 Ready、Compiling、Running、Completed、Cancelled、Failed，以及当前时间/总进度。

**Run Name**

- 工具栏在 Run 前提供可编辑 `Run name` 输入框，默认使用当前模型中未占用的 Run1、Run2 等名称；支持中文，修剪首尾空白，空名称不允许运行。
- 输入框获得焦点时 Enter 与 Run/Ctrl+R 等价；不应要求双击才能编辑。
- 新名称创建新命名结果。相同名称已存在时，运行前明确询问是否覆盖；取消询问则不启动 Worker，旧结果不变。
- 即使允许覆盖，也只有新 Run 成功完成后才替换同名旧结果；失败或取消不删除已有数据。
- Run Name 是结果身份的一部分，但不是 EntityId，也不进入模型公式命名空间。新建/打开文档时清空结果名称占用。

**Workbench Variable**

- 当前分析变量称为 Workbench Variable，与画布多选集合和图表可见变量集合分开保存。
- 单击画布中的数值 ModelElement 会同时选中它并设为 Workbench Variable；Connector、Cloud、Comment 不能成为 Workbench Variable。多选不改变“唯一当前分析变量”的规则。
- 工具栏/结果区的 Variable Selection 控件提供同一变量列表和搜索；控件与画布选择双向同步。
- 标题栏或紧邻标题的固定区域显示当前 Workbench Variable，修改画布选择、变量改名或文档替换后立即刷新。
- Graph/Table 默认分析 Workbench Variable；若尚未设置则使用首个 Stock，再否则使用首个可输出变量。没有可用结果时提示先运行，不隐式启动新 Run。
- Workbench Variable 只决定当前分析对象；Graph 默认叠加所有已加载 Run，变量列表仍可向图中加入其他变量。

**模型检查入口**

新增文字 Model 菜单或等价现有菜单位置：Check Model（Ctrl+T）执行结构、公式和可运行性检查；Units Check（Ctrl+U）在单位语义实现后启用。检查结果定位到变量和公式，不能只显示统一失败框。缺少单位检查实现前必须明确标为未提供，不能总报告 Units OK。

### 2.2 设置对话框

`SimulationSettingsDialog` 是非画布、非属性面板的独立 Widget，编辑整个 Document 的 SimulationSettings：

- Start time、Stop time；
- DT；
- SAVEPER；
- Euler / RK4；
- Time unit。

OK 时一次性构造不可变 SimulationSettings 并由 DocumentController 提交一个文档级撤销命令；非法组合显示具体字段错误，OK 不关闭且 Document 不变。Cancel 不写 Document。DT 改变时不擅自覆盖用户显式设置的 SAVEPER；若原 SAVEPER 仍等于旧 DT，可同步为新的 DT，否则保留并显示校验错误。

设置属于 QDS 模型，不属于窗口 QSettings；窗口只记忆对话框几何等纯 UI 状态。

### 2.3 结果 Dock

主窗口通过 `setCorner()` 将 BottomLeftCorner 分配给 LeftDockWidgetArea、BottomRightCorner 分配给 RightDockWidgetArea；左右 Dock 占据各自侧边的全部高度，底部 Dock 只占据中间剩余宽度。

新增可隐藏、可停靠的 `Run results` Dock，默认位于底部并参与现有 saveState()/restoreState()。当前最小实现提供 Workbench Variable 与 Run 两个独立选择器、Datasets 勾选列表和单变量时间表；变量搜索、多变量可见性以及 Charts/Data/Diagnostics 三标签整合在后续阶段扩展：

```text
Run results
├─ Datasets：命名 Run、加载状态、最新运行优先
├─ 左：变量搜索、可见性列表、全选/清空、使用画布选择
└─ 右：Charts / Data / Diagnostics 三个标签页
```

- Datasets 同时是 Control Panel 的数据集页面；每个命名 Run 具有独立加载状态。最新运行排在前面，Graph/Table/Causes Strip 默认作用于全部已加载 Run。
- 已加载 Run、Workbench Variable 和图中其他可见变量是三组不同状态，不得共用一个布尔选择字段。
- 没有 Run 时 Dock 可保持隐藏；首次成功或失败运行后显示对应页面。即使小模型瞬间完成，也显示 Completed、样本数、Run Name 和结果 Dock，不让用户靠“是否弹出覆盖提示”判断是否运行。
- Diagnostics 复用现有模型检查面板或共享诊断视图，不复制诊断存储。
- 变量列表显示名称、单位、可见勾选状态和 Run 状态，不把名称作为身份。
- 默认显示 Workbench Variable；未设置时选择首个 Stock。Auxiliary/Flow/Constant 可搜索并追加。变量过多时使用列表虚拟化和延迟搜索，不一次生成图表点对象。
- Del/Close 只在结果视图具有焦点时关闭当前输出，不删除数据集，也不删除画布元素。Windows > Close All Output 只关闭输出视图，不清空 Datasets；清空结果需要独立明确操作。

### 2.4 结构、方程与完整性提示

建模到模拟的界面必须显示“结构完成但尚不能运行”的状态，而不是等运行失败后才知道公式缺失。

- 元素创建继续使用现有拖放面板和左键拖拽 Flow/Arrow。Vensim 的单击起点/终点教程只说明流程，不改变本项目鼠标约定。
- Equation Editor 动作聚焦现有 PropertyPanel 的公式输入，不新增另一套公式存储。变量列表/补全可以插入名称；换行和空白只影响公式源文本，名称内部不能插入换行。
- 缺少公式、公式不完整、缺少必要输入或存在诊断的元素在画布上有明确警告轮廓/标记；完成后立即清除。颜色不是唯一通道，还需图标或文本以支持可访问性。
- Stock 编辑显示 Initial value 公式、单位和流入/流出关系；Flow 由端点方向自动决定加减。不得把 Stock 初值伪装成可自由编辑的 INTEG 表达式，也不要求用户手工维护净流入字符串。
- Flow/Auxiliary/Constant 编辑各自公式和单位；Flow 的方向与符号由 FlowEndpoints 决定，改变方向必须经过现有重连命令。
- Check Model（Ctrl+T）汇总结构、语法、引用、初始化/运行期环和可运行性；错误可定位到元素及公式 SourceSpan。
- Units 输入保留既有单位历史供选择。Units Check（Ctrl+U）需要真正的量纲分析；实现前不提供虚假通过结果。单位错误应指出相关变量、期望/实际单位和运算位置。
- 新建模型可以直接使用默认 SimulationSettings，Time Bounds 设置入口在向导/菜单中清晰可发现；是否强制弹窗由后续可用性测试决定，不强制中断熟悉用户。

## 3. 运行期架构与生命周期

### 3.1 不可变运行请求

UI 不能让 Worker 持有正在编辑的 Document 引用。每次运行创建独立 `SimulationRunRequest`：

```text
SimulationRunRequest（不可变、共享所有权）
├─ std::shared_ptr<const Document> model   本次运行的独立语义副本
├─ SimulationPlan plan                     由该副本编译
├─ settings
├─ runName：QString                        本次运行的不可变名称
└─ QVector<RunVariableInfo>                EntityId、当时 displayName、unit
```

- 副本复用 QDS `document.xml` 的内存 XML 读写通道：DocumentFile 直接将 Document 序列化为 UTF-8 XML，再在内存中解析为新 Document；不写文件、不经过 ZIP 压缩/解压，也不依赖 Scene。新增语义字段只需维护同一持久化通道。
- XML 加载允许重新分配 EntityId，因此 RunVariableInfo 完全基于运行副本建立，只保存 runElementId、当时的 displayName/nameKey 和单位；旧 Run 不保存原模型对象或双重 ID 映射。若界面需要定位当前模型，只能在相同修订下按 nameKey 做辅助导航，这不属于 Run 的身份语义。
- 副本保留 EntityId、公式、Dimensions、Macros、FlowEndpoints 和 SimulationSettings；不需要 QGraphicsItem、Workspace 或窗口对象。
- Plan 只与这个副本配套，不能用于后续已修改的 Document。
- RunVariableInfo 固定运行时的显示名称和单位；之后用户改名或删除元素，不改变历史结果中的名称，也不产生悬空 Document 指针。
- 副本建立或编译失败时不启动线程，不产生半成品 Run。

### 3.2 Controller 与 Worker

应用侧 `SimulationController` 拥有当前会话：

```text
SimulationController : QObject
├─ shared_ptr<const SimulationRunRequest> activeRequest
├─ NamedRun 集合                            RunId、runName、shared_ptr<const SimulationRun>
├─ 已加载 RunId 顺序                        最新优先；加载状态独立于结果所有权
├─ Workbench Variable 与可见变量状态
├─ Worker/QThread 的受控生命周期
├─ 原子取消标志
└─ Idle / Compiling / Running / Completed / Cancelled / Failed
```

NamedRun 发布后不可变；覆盖同名结果也创建新 RunId 和新结果对象，不修改旧 SimulationRun。Worker 和信号只持有 RunId/共享 const 指针，不持有界面对象地址。

- Worker 在独立线程中使用共享 const Request 执行同步 Runner；Core 不依赖 Qt 线程或 Widgets。
- 进度通过带 QObject context 的 queued signal 返回 UI；限制更新频率，不逐公式发送信号。
- 完成后 UI 接收 `shared_ptr<const SimulationRun>`；Worker 不保留可修改结果。
- Cancel 只设置线程安全标志并请求 Runner 协作取消，不强制终止线程。
- 新 Run 必须等待旧 Worker 完全退出；不并行覆盖 activeRequest。
- 新建、打开、替换 Document 或关闭 MainWindow 时先取消并等待 Worker，再清理会话； queued signal 使用 Controller 作为 context，Controller 析构后不得访问窗口。MainWindow 析构入口先断开 SimulationController 到窗口的连接，再取消并等待 Worker，防止 reset 状态通知访问正在销毁的 Dock 或状态栏。
- 不用裸指针捕获 Worker、Controller 或 MainWindow 给长命 lambda。QThread 的 finished 与对象销毁顺序必须专门测试。

首批 UI 不使用“在 UI 线程反复 processEvents”的伪异步方案，避免重入修改 Document 或关闭窗口。

### 3.3 结果与文档编辑

多个 Run 用于比较同一模型在不同常量、Stock 初值、公式、结构、积分方法、DT/SAVEPER、Lookup、外部 Data 或随机种子下的结果；输入完全相同时也允许重复运行。每个 Run 都是完整不可变输入及设置产生的历史结果，而不是当前画布的另一块临时状态。

- 当前 Run 是独立历史事实，不进入 Document undo/redo。
- 运行期间允许继续编辑当前 Document，为下一次运行做准备；Worker 始终使用 activeRequest 的旧副本，不会混入部分新公式。
- 修改不修改或删除旧 Run。Completed 与是否对应当前模型是两个维度：模型修订号不同只增加 Outdated，不把 Completed 改为 Failed。
- 首批采用保守失效规则：常量/初值、公式、变量类型、数值对象和 Flow 增删、Flow 端点、数组、宏、Lookup/Data/随机输入、仿真设置以及影响结果解释的名称/单位变化都推进 simulationRevision。
- 元素位置、Route、字体、选择/悬停、画布缩放滚动、窗口布局和不影响计算的 Comment/Connector 展示变化不推进 simulationRevision。undo 后即使内容碰巧恢复，首批也允许保守地保持 Outdated。
- 运行后的普通模型编辑不修改旧 Run；界面标记 `Results are from an older model`，并提供 Run 按钮刷新。
- 新名称生成新的 NamedRun，并置于已加载列表首部；相同名称经确认后也只在新 Run 成功时替换旧记录。失败 Run 可以显示自身诊断，任何失败/取消都不得静默删除旧结果。
- NamedRun 的 RunId 在会话内单调递增；runName 用于显示与覆盖判断，RunId 用于信号、加载列表和界面索引，避免改名或同名覆盖时引用错位。
- 诊断定位只在当前 Document 仍存在相同 EntityId 且用户确认定位当前模型时跳转；历史元素已删除时只显示历史名称，不创建替代对象。

## 4. 折线图与数据表

### 4.1 图表边界

新增 `TimeSeriesChartWidget` 作为 UI 边界，Core 只提供 times 和按 EntityId 的序列。图表实现不得反序列化模型或读取 Document。

首批交互：

- Workbench Variable 及其他选中变量按时间叠加，并默认叠加全部已加载 Run；图例同时显示 Run Name 和变量名，内部用 RunId+EntityId 区分；
- 图例点击显示/隐藏；
- 缩放、拖动和恢复全范围；
- 光标最近样本提示，显示时间、变量名和值；
- 坐标轴使用实际时间范围与数值范围，不假定等间隔显示文本标签；常量结果应显示为准确的水平线，例如 Population=1000，不能因自动坐标范围显得波动；
- 空选择显示提示，不生成空图片覆盖剪贴板；
- PNG 导出使用离屏渲染，不截取屏幕。

已确定使用官方 Qt Charts，不再引入 QCustomPlot 或维护通用自绘图表框架。Qt Charts 只进入应用层，Core 不依赖它；TimeSeriesChartWidget 继续隔离图表实现。Graph/Table 共用 Simulation 菜单和工具栏 QAction，并切换结果 Dock 的 Chart/Data 标签。当前图表默认叠加全部已加载 Run，图例显示 Run Name 与变量名；滚轮缩放、左键拖动平移，双击同时恢复缩放和平移前的完整坐标范围。10 万样本、10 条序列、连续缩放和 PNG 输出仍作为回归 profile 场景，用于发现异常，不设置当前不需要的极限性能门槛。置信区间带、Causes Strip 等领域图形可在 Qt Charts 基础上组合或少量自绘。

### 4.2 数据表

`SimulationTableModel : QAbstractTableModel` 按需为视图提供数据：

- Data 页提供 Run 选择器，默认最新已加载 Run；第一列是时间，其余为选中变量。切换 Run 不修改其不可变结果；
- 行对应当前 RunId 的 Run::times，列通过 EntityId 查询不可变序列；不强行对齐不同 Run 的时间网格；
- 不复制完整结果矩阵；
- 提供统一有效数字显示，底层 double 不因格式化而改变；
- 支持复制选中区域和后续 CSV 导出；导出失败明确报告，不写部分文件冒充成功。
- 表格必须能滚动到末时刻以核对恒定值或长期趋势；不能只显示首屏就宣称验证完成。

失败或取消的 Run 只展示已完成样本前缀，并在标题中明确状态，不把最后一个点伪装成 stopTime。

### 4.3 原因与 Run 比较（后续分析工具）

- Causes Strip 使用 Workbench Variable 在对应 SimulationPlan/AnalysisReport 中的直接公式依赖，显示该变量及 births/deaths 等直接原因；不能仅凭画面上的 Connector 猜测公式依赖。
- 每个已加载 Run 可显示相应条带；历史结果使用各自不可变模型副本的依赖关系。
- Runs Compare 默认比较加载列表的前两个 Run，报告 Constant 的初值/运行值差异，并列出 Run Name、旧值、新值和差值。Lookup 比较在 Lookup 模型实现后加入。
- 这些工具只是分析视图，不修改模型、命名结果或加载状态。

## 5. 错误与边界交互

- 编译错误在 Diagnostics 中显示变量、代码和公式位置；点击可定位当前画布元素并聚焦公式编辑器。
- 非法设置、无公式、未定义变量、代数环、不支持的函数分别显示明确原因。
- 非有限值、时间停滞和 Worker 内部错误终止当前 Run，UI 保持可编辑。
- Cancel 是正常用户操作，不弹错误框；状态栏显示取消时的实际时间。
- 无 Stock 的纯公式模型仍可运行并显示 Auxiliary/Constant。
- 不支持的数组、Data、Lookup、随机或状态函数不得显示“运行成功”。

## 6. 测试与 profile

### 6.1 单元和集成测试

使用真实 MainWindow、View 和 Worker 覆盖：

- 菜单/工具栏 QAction 共享、快捷键、启用/禁用和状态栏；
- 设置合法提交、非法保留、Cancel 及 undo/redo；
- 成功、编译失败、运行失败和取消；
- Run Name 默认值、中文名称、Enter 运行、空名称、同名覆盖确认/取消、失败不覆盖旧结果；
- 画布与 Variable Selection 双向同步 Workbench Variable，标题栏刷新，多选不与 Workbench 状态混淆；
- Graph/Table 对 Workbench Variable 和全部已加载 Run 的展示，恒定值水平线及表格末时刻；
- Datasets 最新优先、加载状态、RunId 稳定性、Close/Close All Output 不删除结果；
- 缺少公式的画布提示、Model Check 定位、Stock 初值/Flow 方向和单位检查未实现时的明确状态；
- 变量选择、图表/表格/诊断切换及结果导出；
- 编辑后结果过期、新建/打开清理、历史名称保持；
- 重复 Run、运行中再次 Run、关闭窗口和替换 Document 的 Worker 取消/等待；
- Controller 销毁后 queued 回调不得访问窗口；
- 首批无界面运行仍是 Core 同步调用，UI 测试与 Core 数值测试分离。

### 6.2 性能

新增独立 UI profile，不把 Worker 调度时间混入 Core 积分 profile：

- 模型副本建立与编译；
- 进度通知频率；
- 变量列表和表格惰性查询；
- 10 万样本/10 序列的图表绘制、缩放与 PNG；
- 大 Run 的结果共享和销毁耗时。

每个 UI 功能阶段继续执行完整单元测试、适用 profile、Release 后 Debug 全量回归和自审。

## 7. 分阶段实施

1. **运行会话基础**：不可变 Document 副本、SimulationController、Worker 生命周期、取消和文档替换保护。
2. **设置与运行入口**：SimulationSettingsDialog、文档级撤销、Run Name/Enter、Run/Cancel、状态栏和诊断定位。
3. **结果 Dock**：Workbench Variable、命名 Datasets、加载状态、数据表及历史结果过期提示。
4. **Qt Charts 折线图**：统一 TimeSeriesChartWidget 接口，完成 Workbench/多 Run 叠加交互，并用固定场景 profile 检查回归。
5. **结果导出与完善**：CSV/PNG、Close All Output、键盘可访问性、翻译、大结果 profile。
6. **后续独立设计**：Causes Strip、Runs Compare（含 Lookup）、数组结果、参数实验与敏感性分析界面。

每阶段单独提交设计所对应的实现和测试；本文件只记录功能设计，不进入 history。
