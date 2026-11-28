# UI 对象生命周期重构计划

创建时间：2026-11-27 23:01:53 +0800

状态：已完成并合并；专项测试、五轮 Debug 全量、三轮 Release profile 及后续 Release/Debug 全量复验均通过

## 1. 背景与已确认根因

此前随机崩溃在独立 `-Og -g` 构建中取得完整调用栈：`QGraphicsScene::clear()` 删除选中图元并发出 `selectionChanged`，随后 `PropertyPanel::refresh()` 在 `src/app/PropertyPanel.cpp:152` 遍历 `m_controller->document().model().elements()`。此时 MainWindow 的 DocumentController 已先销毁，Scene 和 PropertyPanel 仍在析构流程中。

反复创建窗口只能概率性触发，不能作为根因判断或修复验收。现有 MainWindow 析构中的两条定向 disconnect，以及 `propertyPanelDestruction()` 压力场景，是诊断现场，不代表最终方案。

本次审查确认问题不局限于 PropertyPanel：长期外部 QObject 引用、Scene 基类析构通知、SimulationController 析构通知、RoutedItem 端点引用和结果表内部数据指针需要按统一规则处理。

## 2. 目标

建立不依赖兄弟对象创建/销毁顺序的生命周期协议：

- 保持 Qt 父子所有权体系，不引入 `shared_ptr<QObject>`；SimulationController 改由 MainWindow/session 直接拥有，DocumentController 仅作为弱引用服务；
- 能消除的长期裸指针优先消除；必须保留的外部 QObject 引用使用 QPointer 和空值语义；
- Scene 自己封闭析构期业务通知和索引失效过程；
- SimulationController 析构停止 Worker 时不驱动外部业务刷新；
- 结果表只访问自己明确持有的数据；
- 保持现有级联删除、撤销/重做、文档重置、仿真和结果显示语义。

当前权威规则及逐项分类见 [UI 对象生命周期与指针规则](../design/object-lifetime.md)。

## 3. 审查范围与结论

### 3.1 需要迁移

- `PropertyPanel::m_controller/m_scene`：长期外部引用，原崩溃直接位置。
- `DiagramScene::m_controller`：大量同步和编辑入口假定 Controller 永久有效。
- `SimulationController::m_documentController`：运行启动、过期判断和 Workbench 操作假定 Document 永久有效。
- `SimulationResultsWidget::m_controller`：结果刷新与 PropertyPanel 属于同类风险。
- `RoutedItem::m_from/m_to`：几何、绘制和样式长期访问兄弟图元。
- `SimulationTableModel::Column::values`：指向另一份 Run 的内部 QVector，依赖隐式共享维持寿命，所有权不清晰。

### 3.2 需要独立析构协议

- QGraphicsScene 基类析构可能发出业务信号，QPointer 不能保护派生成员已经销毁的半析构 Scene。
- SimulationController 析构若继续发出 reset 通知，接收方可能访问正在销毁的 Document。
- 简单把 Scene 改挂到 Controller 下面无效：parent 的派生成员可能早于 QObject 基类中的 child 删除而销毁。SimulationController 的所有权调整不是销毁顺序技巧，而是让会话所有者直接拥有会话对象，解除 DocumentController 同时充当被引用服务和 parent 的不合理关系。

### 3.3 保持现状

- MainWindow 和 Widget 对 Qt 直接子对象的所有者句柄；SimulationController 的 parent 按上述会话归属修正；
- Scene 对图元的独占所有权及受控 ID 索引，但批量清空顺序需要调整；
- QChartView 对 QChart 的所有权；
- CausesStripWidget 对子图表的列表管理；
- Worker 对不可变 Request/Run 和取消标记的共享所有权；
- Core Document 的 unique_ptr，以及只在当前调用栈使用的查找指针。

完整分类和空值行为以设计文档为准，本计划不再复制一套平行规则。

## 4. 非目标

- 不改变 Document/Scene 的双层模型、EntityId 或 ChangeNotification 语义。
- 不用 QPointer 取代 Stock/Cloud 删除时的 Flow/Connector 级联删除。
- 不改变 Worker 数值计算、Run 数据、名称查询或 UI 功能。
- 不通过交换 MainWindow 成员顺序、调整 child 创建顺序或增加其它局部 disconnect 掩盖问题。
- 不对全部 raw pointer 做机械替换，不把 Qt 父子体系改造成 shared_ptr 体系。
- 不顺带修复与本生命周期问题无关的功能或测试。

## 5. 实施步骤

1. **审计全部 raw pointer 返回接口**  
   已逐项覆盖 Core、I/O、App 的对象和容器指针返回，记录所有者、调用寿命、缺失/空结果语义及迁移结论。series/arraySeries 的调用不需要区分缺失和零样本，采用 QVector 值返回，不增加 bool found/optional；其它接口按静态所有权、多态对象、迭代器或 Qt 契约分类保留，完整表格见设计文档。

2. **固定回归场景**  
   保留现有 MainWindow 压力场景，增加确定性的不同销毁次序测试；必要时使用 `-Og -g` 构建和调试器核对修复前栈，但不以偶发通过作为证明。

3. **封闭 Scene 析构与批量重置**  
   由 DiagramScene 在派生析构入口 `blockSignals(true)` 且不恢复，结束临时编辑，先失效索引再清理图元。不能使用析构体结束时自动恢复的局部 QSignalBlocker。Qt 的 destroyed/QPointer 机制不受 blockSignals 影响。文档重置按“临时状态/索引先失效，Scene clear 后重建”的顺序迁移。

4. **迁移外部 QObject 引用**  
   PropertyPanel、DiagramScene、SimulationController、SimulationResultsWidget 使用 QPointer，并为刷新、提交、启动、过期判断和几何访问实现一致的空值行为。RoutedItem 端点改为 QPointer，保留正常删除顺序和开放端 fallback。SimulationController 增加独立 parent 参数并由 MainWindow 直接拥有；必然存在的 `undoStack()` 改为引用返回。

5. **迁移结果序列与表格数据所有权**  
   SimulationRun 的 series/arraySeries 公开及私有重载直接返回 QVector 值；当前调用不需要区分 missing 与零样本，因此不增加 found/optional。变量存在性由 variables 元数据表达，零样本由空容器表达。删除 SimulationTableModel 的长期 `values` 裸指针及不再需要的整个 Run 共享指针；每列以值语义保存 runName、times、values，利用 QVector 写时复制避免主动深拷贝，同时保持单元格读取无重复名称查询。

6. **审查 SimulationController 析构通知**  
   区分显式 reset 与析构停止；析构函数体内以 QSignalBlocker 复用 reset 的取消、等待和清理逻辑，确保 Worker 完全退出且不驱动外部查询。MainWindow 正常关闭的显式收尾可以保留，但不再承担唯一安全屏障。

7. **专项验证与 profile**  
   执行下述单元/集成场景，并在 Release 可执行文件中增加固定模型、固定窗口/Scene 次数的创建销毁与文档重置 profile，记录 QPointer 和索引调整的额外开销，不设置超出需求的极限门槛。

8. **全量验证与自审**  
   依次执行 Release、Debug 的 `all-test-targets` 和 `ctest --output-on-failure`，检查所有长期 raw pointer、lambda 捕获和新增文档是否仍符合统一规则。完成后等待用户审核，不自动提交、合并或推送。

## 6. 验收场景

- Controller 先销毁，再销毁带选中 Stock/Flow 的 Scene 和 PropertyPanel，不发生 Document 访问。
- Scene 先销毁，Controller、PropertyPanel 和 MiniMapView 后销毁，不回调半析构 Scene。
- SimulationController 先销毁，ResultsWidget 后销毁；DocumentController 在仿真和结果视图仍存在时销毁也不发生失效访问。
- 在普通 QGraphicsScene 中单独构造 RoutedItem 和端点，删除连接端点后，boundingRect、shape、paint 和样式查询不访问悬空指针；该测试不绕过 DiagramScene 的独占索引。正常模型删除仍完整级联。
- 选中连接线时替换 Document，旧索引、控制柄和外部面板状态不跨重置存活。
- 原始 Run 集合替换或销毁后，表格仍能读取自身持有的不可变结果；series/arraySeries 对缺失 nameKey 和零样本统一返回空 QVector，返回值在 Run 销毁后仍可使用，修改返回值不影响历史 Run。
- 运行中关闭窗口、显式 reset、Controller 析构及 queued 回调不与 Worker 资源释放竞争；以 QSignalSpy/计数器断言 Scene 基类清理和 SimulationController 析构均不发送对外业务通知。
- 现有反复创建/销毁场景保留为正式回归；所有新增场景具有有效断言，而非只依赖“没有碰巧崩溃”。

## 7. 复审结论与剩余实施决策

复审已修正以下方案问题：

- 全部 raw pointer 返回接口已审计；series/arraySeries 不需要 found/optional，直接返回 QVector 值。Document/AST/迭代器保留即时对象指针，静态函数表保留静态指针，Qt MIME/控件接口遵循其所有权契约，非空 undoStack 改用引用。
- Scene 若只在派生析构体中使用局部 QSignalBlocker，基类析构前就会恢复信号，保护不完整；改为整个剩余析构过程持续 blockSignals。
- 原方案把 MainWindow 对 SimulationController 的持有误写成直接所有权，实际 parent 是 DocumentController；改为会话对象由 MainWindow/session 直接拥有。
- 逐单元格按 nameKey 查询会增加不必要的查找开销；改为列内 QVector 值语义，既消除指针又保持原有查询复杂度。
- 直接删除 DiagramScene 登记端点会与 raw ID 索引约定冲突；RoutedItem 独立删除测试改用普通 QGraphicsScene，不扩大 DiagramScene 的外部删除语义。

实施前仍需确认 QPointer 空值时各公共 API 的返回约定。生命周期 profile 的迭代规模在实现时根据现有 editor profile 风格确定，并记录输入、输出和运行命令。Qt 参考：[QObject::destroyed 不可阻塞](https://doc.qt.io/qt-5/qobject.html#destroyed)、[QPointer 在 QObject 析构阶段清空](https://doc.qt.io/qt-5/qpointer.html)。

## 8. 完成结果

实现提交为 `167a5fd Fix QObject lifetime and result series ownership`，合并提交为 `ade852b Merge QObject lifetime fixes`。初次 Release 全量的 Windows 剪贴板环境失败已保留在 `docs/profile/object-lifetime.md`；后续环境恢复后，相同工作树的 Release 和 Debug 全量均通过。本计划没有剩余实现事项，本次将文件标记为 done。
