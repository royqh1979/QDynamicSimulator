# UI 对象生命周期与指针规则

## 1. 状态与范围

本文记录 QObject、QGraphicsObject 及结果数据指针的审查结论和当前约束。实现已按本文完成迁移，实施范围与验收步骤见 [UI 对象生命周期重构计划](../history/2026-11-27-qobject-lifetime.md)。

范围包括 MainWindow、DocumentController、DiagramScene、PropertyPanel、SimulationController、SimulationResultsWidget、画布图元和结果视图。核心 Document 的 `unique_ptr` 所有权、Worker 的不可变共享数据以及 Qt 父子所有权体系继续保留，不引入 `shared_ptr<QObject>`。SimulationController 是会话对象，由 MainWindow/session 直接拥有，而不是由它所引用的 DocumentController 拥有。

## 2. 所有权分类

### 2.1 Qt 父子所有的对象

MainWindow 对 DocumentController、SimulationController、Scene、View、Dock 的持有，以及 Widget 对子控件、Model、Chart 的持有，都是所有者保存的直接子对象句柄。只要创建、重设 parent 和删除都经过明确的 Qt 所有权边界，这些句柄可以继续使用 raw pointer，不机械改成 QPointer。MainWindow 构造函数应把 SimulationController 的 parent 设为 MainWindow；其 DocumentController 参数只是外部服务引用，不再兼任 parent。

直接子对象句柄不承诺在所有者继续正常使用时支持外部任意删除内部服务；析构次序独立是指各依赖对象能安全完成 teardown，不是把 MainWindow 的内部 Controller/Scene 变成可热插拔服务。

`CausesStripWidget::m_charts` 也是子对象句柄集合。刷新时先移出集合并断开布局，再 `deleteLater()`；不得在集合中保留已经提交删除的对象。`TimeSeriesChartWidget::m_chart` 由 QChartView 接管，继续使用所有者句柄。

Qt 父子关系负责释放对象，不代表外部引用在任意析构阶段都可安全访问，也不保证派生类成员在子对象析构时仍然有效。

### 2.2 Scene 独占管理的图元索引

QGraphicsScene 通过 `addItem()` 拥有 DiagramItem、ControlPointHandle 和临时橡皮线。Scene 的 `m_items`、当前控制柄列表和橡皮线句柄是所有者为投影管理保存的索引，可以保留 raw pointer，但必须满足：

- 单个图元先移出相关索引，再删除；
- 批量清空前先使 ID 索引、控制柄列表和临时引用失效，再调用 `QGraphicsScene::clear()`；清理期间通过显式状态阻止选择通知重入并重新创建控制柄，清理完成后才恢复同步；
- 外部通过 `item()` 等接口取得的 raw pointer 只在当前调用栈即时使用，删除或文档重置后重新按 EntityId 查询；
- 不在 DiagramScene 之外直接删除其独占管理的图元。RoutedItem 端点弱引用的独立删除测试使用普通 QGraphicsScene 和单独构造的 Item，不经过 DiagramScene 的 `m_items`；若未来允许绕过 DiagramScene 删除其登记图元，必须同时把相关索引改为弱引用或增加销毁注销机制。

这些索引不拥有核心 DocumentElement，也不以 Item 地址作为身份。

### 2.3 长期非拥有 QObject 引用

下列对象需要长期访问由其他所有者管理的 QObject，使用 `QPointer` 并提供明确的空值行为：

| 引用 | 当前位置 | 空值行为 |
| --- | --- | --- |
| PropertyPanel → DocumentController、DiagramScene | `PropertyPanel::m_controller/m_scene` | 清空并禁用相关编辑状态；提交安全失败，不访问已销毁对象 |
| SimulationResultsWidget → SimulationController | `SimulationResultsWidget::m_controller` | 清空结果视图，不再读取 Run 或修改 Workbench |
| DiagramScene → DocumentController | `DiagramScene::m_controller` | 查询返回空结果，编辑操作不执行；不得在回调中继续访问 Document |
| SimulationController → DocumentController | `SimulationController::m_documentController` | 不启动新 Run；已有结果按无法比较当前修订处理；清除失效的 Workbench 引用 |
| RoutedItem → 两端 DiagramItem | `RoutedItem::m_from/m_to` | 按开放端使用现有 fallback 几何；重新绑定后恢复正常端点 |

QPointer 只负责发现 QObject 已完成销毁，不延长寿命，也不替代业务级联删除。删除 Stock/Cloud 时仍必须先删除相关 Flow/Connector，再删除端点图元；不能因为端点引用可自动清空而保留半条语义连接。

所有 QPointer 解引用前都必须检查。仅在构造函数检查一次不够，槽、刷新、提交、几何计算和绘制入口都要遵守所访问对象当前可能为空的事实。上述对象均只在 UI 线程访问，QPointer 不被拿来同步 Worker 跨线程生命周期。

### 2.4 非 QObject 数据引用

QPointer 不适用于 QVector、Document、SimulationRun 等非 QObject 数据。此类指针优先通过缩短持有时间或明确值/共享所有权消除：

- `SimulationTableModel` 不得长期保存指向另一份 Run 内部 QVector 的 `values` 裸指针，也不必为了读取时间和序列而保留整个 Run。每个列直接以值语义保存 `runName`、`times` 和 `values`，单元格读取继续使用列内时间序列做二分查找。QVector 写时复制使设置列时通常不深度复制数值缓冲区，但表模型持有的是明确的容器值，不再依赖指向其它 Run 节点的地址；无需逐单元格按 nameKey 重复查询，也无需额外的 `shared_ptr<SimulationRun>`。
- Worker 的 Document、SimulationRunRequest、SimulationRun 和取消标记继续使用 `shared_ptr<const ...>` 或明确共享对象，不与 Qt QObject 所有权混用。
- `MacroEvaluation::definition`、Dimension/Formula/Document 查找结果只在同步求值或当前调用栈中使用；Document 修改后重新获取。
- QVector 的隐式共享是容器实现能力，不作为跨对象指针所有权协议。

## 3. 析构期规则

### 3.1 不依赖兄弟对象销毁次序

MainWindow 当前先创建 DocumentController，再创建 Scene 和面板，并不意味着该顺序可以成为生命周期接口。QObject 自动断开连接只能处理连接双方自身已经走到 QObject 析构的情况，不能防止：

1. 一个兄弟对象已经销毁，另一个兄弟对象析构时仍向存活接收方发信号；
2. 基类析构发信号时，派生类成员已经销毁；
3. parent 在 QObject 基类析构中删除 child 时，parent 的派生成员已经销毁。

因此不能通过交换成员声明、调整创建顺序或简单把 Scene 改挂到 Controller 下面解决问题。正常应用可以有清晰的拥有关系，但每个类仍须独立满足析构安全。

### 3.2 DiagramScene 停止析构期业务通知

QGraphicsScene 基类清理图元时可能发出 `selectionChanged/changed`。此时 QPointer 尚未必然清空，而 DiagramScene 的派生成员可能已销毁，所以仅用 QPointer 保护 Scene 引用不完整。

DiagramScene 必须在派生类析构入口调用 `blockSignals(true)` 且不恢复，随后结束内联编辑和临时交互、使投影索引失效，再让基类清理图元。不能使用会在派生析构函数末尾自动恢复原状态的局部 QSignalBlocker，否则随后的 QGraphicsScene 基类析构仍会发出通知。Qt 明确保证 `QObject::destroyed` 不能被 blockSignals 阻止，因此这种做法不会破坏 QPointer 失效通知。

该规则由 DiagramScene 自己封装，不能由 MainWindow 枚举 PropertyPanel、MiniMapView 等接收方逐对 disconnect。文档正常重置与对象析构是两种操作：正常重置可以在新 Document 和空 Scene 状态有效时刷新观察者；析构则不得让观察者回调进入半销毁的 Scene。QObject::destroyed 和 QPointer 的失效机制不属于业务刷新通知。

### 3.3 SimulationController 静默析构

显式 `reset()` 是用户/会话操作，需要取消 Worker、清理状态并通知视图。析构可以在整个函数体使用 QSignalBlocker 后复用 reset 的取消、等待和清理逻辑，避免复制线程管理代码，同时阻止业务信号；这里没有 QGraphicsScene 式的后续基类清理通知，QSignalBlocker 在析构体结束时的恢复不会造成同类问题。析构后不得再驱动外部视图查询 Document。

MainWindow 关闭时可以显式 reset 以完成正常会话收尾，但内存安全不得依赖这一调用一定发生。Worker 始终使用独立共享请求，不访问正在销毁的 Document、Scene 或 Widget。

### 3.4 Lambda 与信号参数

长命 lambda 继续遵守 AGENTS.md 的 context 规则。以下模式不需要机械改写：

- sender 与捕获指针相同，且信号发出时 sender 必然存活；
- context 与捕获的 UI 所有者相同，sender 也属于同一所有权树；
- QListWidgetItem 等信号参数只在直接连接的当前调用中使用；
- Worker 回调使用值语义 generation 和共享不可变数据。

若捕获的是第三个外部对象，其寿命不能只凭 sender/context 推测，应消除捕获或使用受保护的弱引用。

## 4. 当前审查结论

- 已确认的随机崩溃是 DocumentController 先销毁，DiagramScene 随后清理图元并发出选择变化，PropertyPanel 再通过失效的 `m_controller` 遍历 Document。
- PropertyPanel 不是唯一风险点；Scene、SimulationController、SimulationResultsWidget 和 RoutedItem 都存在需要明确处理的长期非拥有引用。
- `SimulationTableModel::Column::values` 当前依赖 QVector 隐式共享及 Run 替换方式维持数据寿命，虽未确认现时崩溃，但所有权表达脆弱，应纳入本次重构。
- DiagramView/MiniMapView 没有另存一份 Scene 成员，使用 QGraphicsView 的 Scene 关系；其安全依赖 Scene 析构期通知得到控制。
- MainWindow、普通子控件、QChart、Causes Strip 子图表及核心 `unique_ptr` 不需要全局改成 QPointer/shared_ptr。
- 当前 `MainWindow::~MainWindow()` 中针对 PropertyPanel 的两条 disconnect 只是诊断现场，不能作为最终设计；仅断开这两条连接也无法覆盖其它接收方和其它销毁次序。

## 5. 返回 raw pointer 的接口审计

除长期成员指针外，本次重构还必须逐项审计 Core、I/O、App 的公开和内部接口中所有返回 raw pointer 的函数，包括对象指针和容器指针。每项记录调用方式、pointee 所有者、缺失/空结果语义、是否可能逃逸出当前调用栈，以及保留、缩短寿命或改值返回的结论。不能因当前调用方碰巧立即使用，就把接口本身标记为无风险。

默认优先使用清晰的值语义。对 `SimulationRun::series/arraySeries` 等查询接口，先审计调用方是否确实需要区分“未找到”和“存在但为空”；不需要时直接返回 QVector 值，以空容器表示无可用结果。只有确有区分需求时才增加 `bool &found` 等显式查询状态；`std::optional<QVector<...>>` 不作为默认答案，也不无差别增加接口复杂度。QVector 值返回利用写时复制，不以担心立即深拷贝为由保留内部指针。

### 5.1 容器指针：改为值返回

审计发现返回容器 raw pointer 的接口只有 `SimulationRun` 的四个公开/私有重载：

- `series(const QString &nameKey)`、`arraySeries(const QString &nameKey)`；
- 私有的 `series(EntityId)`、`arraySeries(EntityId)`。

当前接口直接返回 `QVector<double>` / `QVector<ArrayValue>` 值，不增加 found 或 optional。依据如下：

- QVector 写时复制使返回和保存容器值通常不深度复制数值数据，调用方不再依赖 Run 的内部节点寿命。
- SimulationRunner 在首次采样前失败或取消时，输出变量元数据可以存在，但 times 和数值序列为空；已完成 Run 至少有一个采样点。
- App 的 Table、Chart、Runs Compare 只处理已完成命名 Run；空容器直接表示没有可用序列，不影响现有行为。
- 当前只有 Core 测试显式检查 missing 返回 nullptr，没有生产调用需要区分“名称不存在”和“变量存在但尚无样本”。这些断言改为检查空容器。
- 变量是否属于本次输出由 `variables()` 元数据查询；序列是否有样本由返回容器是否为空表达。未来出现真实区分需求时，再单独增加明确的 `bool &found` 等查询接口，不预先增加 optional/found。

`SimulationRun::variable` 两个私有重载返回 RunVariableInfo 指针，不属于容器接口；它们只在 Run 内部立即使用，可以保留。

### 5.2 Document、仿真元数据与内部查询：保留即时指针

| 接口类别 | 审计结论 |
| --- | --- |
| `Document::find/findByName/findDimension/findMacro/findMutable`、`Model::findByName`、`Dimension::findElement` | 保留。对象多态、不可复制或需要按地址保持身份；Document 修改、移动、清空后必须重新查询，禁止长期保存 |
| `DiagramScene::selectedModelElement/selectedComment` | 保留。返回 Document 所有的当前选择对象，仅在当前刷新/提交调用中使用；Controller/Scene 本身的寿命另由 QPointer 保护 |
| `SimulationRun::variable` | 保留。私有、Run 发布后不可变，只用于当前查询 |
| `DimensionReader`、`MacroEvaluation::definition` | 保留。同步求值期间 Document/共享运行副本保持存活，指针不得逃逸到异步任务 |
| `Evaluation.cpp::arrayOf` | 保留。std::variant 的即时 alternative 访问，Value 在递归求值调用栈中存活 |
| `XmileFile.cpp::findElementByName/findFlowLayout` | 保留。文件内同步查询，Document/routes 在导入调用栈中存活；不为消除指针而复制完整模型或布局 |

这些接口的 nullptr 都有实际“未找到/类型不符”语义，且调用方已经按该语义使用。它们与可廉价返回值的 QVector 不同，不机械改成 optional 值对象。

### 5.3 AST 与迭代器：保留所有者和迭代器寿命规则

`Formula::root` 以及 Unary/Binary/Call/If/Subscript/Lookup/ArrayLiteral 等节点的 child accessor 返回 `unique_ptr` 管理的多态节点。复制会深克隆且破坏递归遍历的简单性，因此保留 const raw pointer；Formula 修改、移动或销毁后重新获取，AnalysisReport 不保存这些节点指针。

`ElementRange::Iterator::operator->` 和 `MacroRange::Iterator::operator->` 是标准迭代器访问接口，指针有效性与迭代器及底层 Document 容器一致，保留。不得在容器增删、Document 清空/移动后继续使用旧迭代器或旧指针。

### 5.4 静态目录与 App/Qt 接口

| 接口 | 审计结论 |
| --- | --- |
| `findFunctionSignature` | 保留。返回静态只读函数表项，进程运行期寿命稳定，nullptr 用于区分数值函数与宏/未知函数 |
| `DocumentController::undoStack` | 返回 `QUndoStack &`。对象必然存在，引用比可空指针更准确；仍不得保存超出 Controller 寿命 |
| `DiagramScene::item/createCanvasItem/selectedRoutedItem`、`RoutedItem::from/to`、`nodeAt/connectionEndpointAt` | 保留 Scene 所有的即时指针；空值语义有实际用途，调用方不得长期保存。RoutedItem 成员自身改为 QPointer，但返回接口不必返回 QPointer |
| `EntityPalette::mimeData` | 保留。Qt override 要求堆对象及所有权转移，nullptr 表示当前项不可拖放 |
| `SimulationSettingsDialog.cpp::timeEditor` | 保留。新控件立即交给传入 parent，属于 Qt 父子所有权 |

QGraphicsView 等 Qt 框架自身返回的 Scene/Widget 指针不在本项目改写范围内，调用处遵守相同即时使用规则。I/O 的 `QString *errorMessage`、`QVector<ModelIssue> *warnings` 是调用方提供存储的同步输出参数，不是返回内部指针，函数不长期保存，无需改为值返回接口。

## 6. 验证原则

回归必须确定性构造不同销毁次序，不能只通过反复运行期待偶发崩溃消失。至少覆盖 Controller/Scene/Panel、SimulationController/ResultsWidget、普通 QGraphicsScene 中单独端点图元销毁、选中线时文档重置和原始 Run 集合销毁后的表格读取。Scene 析构测试还应以计数器断言基类清理期间没有对外业务通知，SimulationController 析构测试应断言没有 reset 业务信号。

保留真实窗口压力回归，并增加可重复的生命周期创建/销毁与文档重置 profile。完成标准包括 Release 后 Debug 的 `all-test-targets` 和 CTest，全量通过后再进行自我审核，不自动提交、合并或推送。
