# 模拟器界面设计

## 1. 范围与当前基础

状态：界面目标设计，尚未开始 UI 实现。

当前 Core 已提供 SimulationSettings、SimulationPlan、同步 SimulationRunner 和不可变 SimulationRun。本设计覆盖设置、运行控制、进度、诊断、变量选择、折线图和数据表，不把数组、多 Run 实验或蒙特卡洛界面提前混入首批实现。

目标流程：

```text
设置模型参数 → 编译并运行 → 选择变量 → 查看折线/数据 → 导出结果
```

界面只调用 Core 公开接口，不在 Widget、Action 或图表中实现公式解析、拓扑排序或积分算法。

## 2. 主窗口组织

### 2.1 Simulation 菜单与工具栏

新增顶层文字菜单 `Simulation`，不设置主菜单图标。菜单和模拟工具栏共享同一批 QAction：

| Action | 默认快捷键 | 作用 |
| --- | --- | --- |
| Run | Ctrl+R | 编译当前 Document 并开始一次新 Run |
| Cancel | Esc，仅运行时捕获 | 请求当前 Worker 取消，不影响编辑器其他 Esc 操作 |
| Simulation settings | — | 打开设置对话框 |
| Export results | — | 导出当前选中变量的 CSV；实现时单独审核文件错误语义 |

- 菜单与工具栏不得分别创建行为对象；图标仍使用极简 SVG。
- Run 在已有运行进行中禁用，Cancel 只在没有运行时禁用。
- 运行期间仍可编辑下一次运行的设置；修改不写入 activeRequest，当前 Worker 继续使用其固定设置，已有结果被标记为旧模型结果。
- 状态栏显示 Ready、Compiling、Running、Completed、Cancelled、Failed，以及当前时间/总进度。

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

新增可隐藏、可停靠的 `Run results` Dock，默认位于底部并参与现有 saveState()/restoreState()：

```text
Run results
├─ 左：变量搜索、可见性列表、全选/清空、使用画布选择
└─ 右：Charts / Data / Diagnostics 三个标签页
```

- 没有 Run 时 Dock 可保持隐藏；首次成功或失败运行后显示对应页面。
- Diagnostics 复用现有模型检查面板或共享诊断视图，不复制诊断存储。
- 变量列表显示名称、单位、可见勾选状态和 Run 状态，不把名称作为身份。
- 默认选择全部 Stock；Auxiliary/Flow/Constant 可搜索并追加。变量过多时使用列表虚拟化和延迟搜索，不一次生成图表点对象。

## 3. 运行期架构与生命周期

### 3.1 不可变运行请求

UI 不能让 Worker 持有正在编辑的 Document 引用。每次运行创建独立 `SimulationRunRequest`：

```text
SimulationRunRequest（不可变、共享所有权）
├─ std::shared_ptr<const Document> model   本次运行的独立语义副本
├─ SimulationPlan plan                     由该副本编译
├─ settings
└─ QVector<RunVariableInfo>                EntityId、当时 displayName、unit
```

- 副本通过 Core 的 capture/restore 类型通道建立，不经过 QDS/XMILE 文件，也不依赖 Scene。
- 副本保留 EntityId、公式、Dimensions、Macros、FlowEndpoints 和 SimulationSettings；不需要 QGraphicsItem、Workspace 或窗口对象。
- Plan 只与这个副本配套，不能用于后续已修改的 Document。
- RunVariableInfo 固定运行时的显示名称和单位；之后用户改名或删除元素，不改变历史结果中的名称，也不产生悬空 Document 指针。
- 副本建立或编译失败时不启动线程，不产生半成品 Run。

### 3.2 Controller 与 Worker

应用侧 `SimulationController` 拥有当前会话：

```text
SimulationController : QObject
├─ shared_ptr<const SimulationRunRequest> activeRequest
├─ shared_ptr<const SimulationRun> currentRun
├─ Worker/QThread 的受控生命周期
├─ 原子取消标志
└─ Idle / Compiling / Running / Completed / Cancelled / Failed
```

- Worker 在独立线程中使用共享 const Request 执行同步 Runner；Core 不依赖 Qt 线程或 Widgets。
- 进度通过带 QObject context 的 queued signal 返回 UI；限制更新频率，不逐公式发送信号。
- 完成后 UI 接收 `shared_ptr<const SimulationRun>`；Worker 不保留可修改结果。
- Cancel 只设置线程安全标志并请求 Runner 协作取消，不强制终止线程。
- 新 Run 必须等待旧 Worker 完全退出；不并行覆盖 activeRequest。
- 新建、打开、替换 Document 或关闭 MainWindow 时先取消并等待 Worker，再清理会话； queued signal 使用 Controller 作为 context，Controller 析构后不得访问窗口。
- 不用裸指针捕获 Worker、Controller 或 MainWindow 给长命 lambda。QThread 的 finished 与对象销毁顺序必须专门测试。

首批 UI 不使用“在 UI 线程反复 processEvents”的伪异步方案，避免重入修改 Document 或关闭窗口。

### 3.3 结果与文档编辑

- 当前 Run 是独立历史事实，不进入 Document undo/redo。
- 运行后的普通模型编辑不修改旧 Run；界面标记 `Results are from an older model`，并提供 Run 按钮刷新。
- 只有新 Run 成功建立后才替换当前结果；失败 Run 可以显示自身诊断，旧 Run 是否保留在界面中由多 Run 设计决定，首批至少不能静默删除旧结果。
- 诊断定位只在当前 Document 仍存在相同 EntityId 且用户确认定位当前模型时跳转；历史元素已删除时只显示历史名称，不创建替代对象。

## 4. 折线图与数据表

### 4.1 图表边界

新增 `TimeSeriesChartWidget` 作为 UI 边界，Core 只提供 times 和按 EntityId 的序列。图表实现不得反序列化模型或读取 Document。

首批交互：

- 多个选中变量按时间叠加；
- 图例点击显示/隐藏；
- 缩放、拖动和恢复全范围；
- 光标最近样本提示，显示时间、变量名和值；
- 坐标轴使用实际时间范围与数值范围，不假定等间隔显示文本标签；
- 空选择显示提示，不生成空图片覆盖剪贴板；
- PNG 导出使用离屏渲染，不截取屏幕。

Qt Charts、QCustomPlot 或自绘方案只能在同一原型场景 profile 后决定。原型至少覆盖 10 万样本、10 条序列、连续缩放和 PNG 输出；不得提前引入新依赖。

### 4.2 数据表

`SimulationTableModel : QAbstractTableModel` 按需为视图提供数据：

- 第一列是时间，其余为选中变量；
- 行对应 Run::times，列通过 EntityId 查询不可变序列；
- 不复制完整结果矩阵；
- 提供统一有效数字显示，底层 double 不因格式化而改变；
- 支持复制选中区域和后续 CSV 导出；导出失败明确报告，不写部分文件冒充成功。

失败或取消的 Run 只展示已完成样本前缀，并在标题中明确状态，不把最后一个点伪装成 stopTime。

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
2. **设置与运行入口**：SimulationSettingsDialog、文档级撤销、Run/Cancel、状态栏和诊断定位。
3. **结果 Dock**：变量选择、Run 状态、数据表及历史结果过期提示。
4. **折线图原型**：统一 TimeSeriesChartWidget 接口，比较候选实现的性能后选择依赖并完成基本交互。
5. **结果导出与完善**：CSV/PNG、键盘可访问性、翻译、大结果 profile。
6. **后续独立设计**：多 Run 对比、数组结果、实验与敏感性分析界面。

每阶段单独提交设计所对应的实现和测试；本文件只记录功能设计，不进入 history。
