---
name: thread-async-lifecycle
description: 审查或实现 Qt/C++ Worker、QThread、原子取消、queued signal 和异步任务关闭。适用于仿真 Worker、跨线程结果、取消/重置、窗口或 Controller 销毁、过期回调和线程随机崩溃；不处理一般值接口或单线程对象所有权规则。
---

# 线程与异步任务生命周期

目标是让异步任务拥有独立输入和明确停止协议，使回调不会访问正在销毁的对象，也不以精巧的事件顺序代替线程安全。

## 1. 划分线程职责

明确：

- UI 线程负责哪些对象和状态；
- Worker 线程执行哪些同步核心计算；
- 谁拥有请求、结果、线程对象和取消状态；
- 哪些操作启动、完成、取消、重置或销毁任务；
- 哪些回调可能 queued 到 UI 线程。

Core 计算不得反向依赖 Widget、Scene 或窗口对象。

## 2. 固定异步输入

- Worker 使用独立不可变模型快照/请求，不读取正在编辑的 Document。
- 请求、计划和必要元数据作为同一次运行的不可变整体。
- 跨线程共享使用 shared_ptr<const T> 等明确数据所有权，不把 UI QObject 放入 shared_ptr。
- 结果发布后不可变；历史结果不随当前模型编辑而改变。
- 每次新运行建立新输入，旧 Plan 不得静默用于新模型。

## 3. 取消与代次

- Cancel 使用线程安全标志和协作式取消，不强制终止正在计算的线程。
- 使用 generation 或等价机制识别过期回调；reset 后旧 Worker 不得发布到新一代会话。
- 新 Run 必须等待旧 Worker 完全退出，不能并行覆盖 activeRequest。
- 取消是正常状态，不伪装成成功结果，也不静默删除已有历史结果。
- 原子标志只保护其自身状态；不要据此假定整个对象状态天然线程安全。

## 4. 信号与回调寿命

- queued signal 使用明确的 QObject receiver/context。
- lambda 捕获值、稳定 ID、generation 或共享不可变数据，不捕获长命 UI 裸指针。
- sender/receiver/context 任一不足时，先调整设计，不依赖“通常来得及”。
- 不为 Worker 保存 Widget、Scene、MainWindow 或正在销毁 Controller 的地址。
- 不在不同线程直接 delete QObject；使用所属线程和 deleteLater/受控清理。

## 5. QThread 与 Worker 收尾

- Worker 在独立线程执行同步 Runner。
- 完成后通知线程退出，并明确 Worker/Thread 的 deleteLater 或所有者清理。
- reset/关闭时先请求取消，再等待线程退出。
- 析构可复用停止逻辑，但不应继续发出驱动外部业务查询的通知。
- 不用 UI 线程反复 processEvents 的伪异步掩盖重入和关闭问题。
- 等待线程时避免 queued 回调依赖当前正在析构的对象才能退出。

## 6. 审核常见竞态

- 运行中再次 Run；
- Cancel 后立即 reset 或关闭窗口；
- reset 后旧 finished 回调到达；
- Document 替换时 Worker 仍在运行；
- Controller 先销毁、Worker 后结束；
- ResultsWidget 已销毁而完成通知到达；
- 同名 Run 覆盖、旧结果加载状态和过期提示；
- 线程 finished、Worker deleteLater 和对象析构交错。

## 7. 验证

至少覆盖：

- 成功、编译失败、运行失败、取消；
- resetDuringRun、destructionDuringRun、关闭窗口和替换 Document；
- Controller/Results 视图不同销毁顺序；
- queued 回调在对象销毁后不得执行失效访问；
- 旧 generation 不修改新会话；
- 多轮 Debug 全量和真实 Worker 压力测试。

需要性能判断时，单独使用 `measured-performance-optimization`，不要把线程修复与性能优化混为一个阶段。

## 完成检查

- [ ] Worker 不读取可变 UI/Document 对象。
- [ ] 请求、结果和取消状态的所有权及线程边界明确。
- [ ] 取消、代次、等待和过期回调规则完整。
- [ ] queued lambda 的 context 和捕获寿命安全。
- [ ] reset 与析构停止均不会发出危险的收尾通知。
- [ ] 确定性销毁/取消测试和多轮压力验证通过。
- [ ] 仅审查时未修改生产代码。
