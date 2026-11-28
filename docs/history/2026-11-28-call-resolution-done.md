# Document 名称管理与 Plan 绑定 AST 重构计划

创建时间：2026-11-28 11:47:15 +0800

## 目标与范围

将仿真编译改为只读 Document、由 SimulationPlan 拥有完整绑定 AST，消除运行期全局名称和函数重复解析。Document 自己承担名称 registry 职责，区分 invokable 定义与其它命名对象。每次 Run 继续创建独立 Document 副本并重新编译，源 Document 和原始 AST 不变。

本计划在重构完成前同时保存目标设计和迁移边界；完成前不修改 `docs/design/` 或其它正式规则文档，完成后再统一同步。不改变公式语义、宏重载/默认参数、Lookup 数值、错误/取消、历史 Run、UI 功能或持久化格式。

非目标：Plan revision、Plan/Document 副本缓存、UI 新功能、通用修改版本下沉，以及现有 App `affectsSimulation()` 遗漏 LookupDefinition 的独立修复。

## 已确认的设计决策

- 不引入 HasNameKey：displayName 属于展示/持久化，不属于索引能力；两个索引没有需要统一遍历的多态对象集合，也不通过基类删除对象。ModelElement、Dimension、MacroDefinition 保留具体名称 getter。
- Document 自己管理名称，不增加独立 NameRegistry 类。Invokable 使用纯 name 聚类以支持命名冲突、宏重载/默认参数重叠和错误诊断；`name+actualArgumentCount` 只用于编译选择，不建立 Document 派生表。
- Lookup 只登记在 invokable 侧并保存 EntityId；Document 门面统一对象查找。
- Document 副本和原始 AST 不变；Plan 拥有模型公式、Stock 初值、逐元素公式、宏体和默认参数的绑定 AST。
- BoundMacroCall 指向 Plan 的 CompiledMacroDefinition，不指向 Document 原始 MacroDefinition。Plan 使用稳定堆身份，必要时仅可移动。
- 不设置 Plan revision，也不在每次编译后做冗余版本比较。指针寿命由 Request 共同持有不可变 Document 和 Plan 保证。
- 删除 Plan ReferenceBinding/readResolvedReference 仿真路径，不保留名称表与绑定 AST 两套执行方式。

## 迁移步骤

### 1. 清理中间方案并保护基线

- 移除被否决的独立 NameRegistry、HasNameKey 中间文件和 Dimension 接口改造，恢复具体值对象/名称 getter。
- 保留 profile 时间函数变量的非保留名称修正。
- 固定现有公式、宏、Lookup、数组、参考文件和 Worker 生命周期测试。

### 2. 迁移 Document 名称索引

- 保留 ModelElement、Dimension、MacroDefinition 的具体 displayName/nameKey 接口，不增加多态基类。
- 用 Document 内部的 invokable 名称聚类和其它对象名称绑定替换 m_nameIndex/m_dimensionIndex 平行索引。
- 统一创建、恢复、改名、宏重载、Lookup、删除、clear 和移动的一致性维护。
- 完成中文名称、冲突、重载范围、删除/恢复和移动测试。

### 3. 建立 Plan 执行表示和 Bound 节点

- 定义 CompiledFormula、CompiledMacroDefinition 及显式 Bound 引用/调用节点。
- 建立 Plan 对模型公式、Stock 初值、逐元素公式、宏体和默认参数的唯一执行所有权。
- 保留 SourceSpan、参数子树、宏局部作用域和递归定义身份；不展开宏。
- 验证编译前后 Document/AST 不变及 Plan 移动稳定性。

### 4. 迁移编译器

- 编译时按名称和实际参数个数选择 Builtin/Macro/Lookup，直接生成 Bound 节点。
- 转换所有模型公式、Stock 初值、逐元素公式、宏体和默认参数。
- 保留未知/错误 arity、依赖、初始化/运行期拓扑和数组布局诊断。
- 不再生成 ReferenceBinding，不建立运行期 CallBinding 查表。

### 5. 迁移 Evaluator 和 Runner

- Runner 从 Plan 取得执行 AST；Document 只提供只读元数据和 LookupDefinition 等配套对象。
- Evaluator 直接执行 Bound 节点；保留宏局部帧、单次实参求值、短路、递归限制、数组和非有限值语义。
- 删除 Plan ReferenceBinding/readResolvedReference 路径；通用无编译求值继续使用原始 AST/readers。
- 保持现有 Request/Worker UI 结构，不修改 Document 副本。

### 6. 完整回归与持久化验证

- 绑定 AST 与通用求值对照，覆盖正常、边界、错误、数组、宏、Lookup、Euler/RK4、取消和失败。
- 验证原 Document 编辑、undo/redo、QDS 保存/加载和历史 Run 不受编译影响。
- 验证当前 Document 运行期间编辑、窗口关闭和不同销毁顺序。
- 运行 MDL/XMILE 参考结果和完整 XMILE corpus。

### 7. Profile、全量门禁与收尾

- 分别测量编译和运行，记录总耗时、归一化耗时、checksum、AST 节点数和分配/调用证据。
- 确认运行期全局 findByName/findFunctionSignature/readMacro 查询消除；编译期调用属于预期。
- 若编译成为实际热点，停止并重新讨论，不自动加入缓存。
- 依次执行 Release、Debug 的 all-test-targets 和完整 CTest。
- 自审旧索引、ReferenceBinding 和重复执行路径全部移除；更新设计与实际过程，提交合并后删除已合并分支和任务专用临时产物，不自动推送。

## 实施记录

- 否决 `QVector<QPair<QString, FunctionSignature>> functionSignatures()`：只查询实际使用的内置名称，不复制完整函数注册表。
- 曾讨论 HasNameKey/Named/Lookupable 等接口命名；最终根据两索引的实际调用方式取消该接口。displayName 不属于索引能力，非拥有查询也不需要虚析构或基类删除。
- 结构讨论曾提出按 revision 缓存 Plan，也曾错误混入“每次编译仍比较 revision”。最终撤回：每个 Run 创建独立 Document 副本并编译，Plan 不保存 revision、不比较版本、不建立缓存。
- 进一步确认 Document 副本及原始 AST 不动，所有执行 AST 由 Plan 拥有；BoundMacroCall 指向 Plan 的编译宏，而非 Document 原始宏。
- 固定基线前发现 profile 夹具使用 `step`、`pulse` 等保留内置名作为变量名，重命名被现行标识符规则拒绝，导致时间函数 checksum 以状态 26 退出。夹具改为 `step_output` 等非保留名，生产语义未修改。
- 修正夹具后的 Release `profile-simulation 1000` 基线：Lookup 求解 1000 次/100 万次调用为 149.118 ms、0.149118 μs/调用，checksum 10000；时间函数 1000 次/100 万次求值为 1081.5 ms、1.0815 μs/求值，checksum 6000。标量 Euler/RK4 分别为 318.257/1202 ms，数组 Euler 为 18331.4 ms，编译指数小模型 1000 次为 2.8453 ms、2.8453 μs/次；所有校验通过。
- 绑定 AST 后两轮普通 Release 的 Lookup 求解为 65.9907/66.5164 ms，0.0659907/0.0665164 μs/调用，相对基线减少约 55.8%/55.4%，checksum 不变。两轮时间函数为 1174.33/1164.95 ms，较基线上升约 8.6%/7.7%，保留该现象；插桩未显示新的名称解析或宏试探热点，不用其它场景的收益掩盖它。
- 新增独立编译 profile：Lookup 编译 3.1617 μs/次，相对同轮运行 66.5164 ms/1000 次约 4.8%；带默认参数 Macro 编译 5.0017 μs/次、运行 356.583 ms/1000 次，编译约占 1.4%。指数小模型两轮编译为 2.6617/2.7959 μs，未显示需要引入 Plan 缓存的热点。
- 独立 `-O3 -g -pg` 插桩运行包含 Lookup/Macro 的完整 profile：readMacro/makeMacroReader 无调用记录；findFunctionSignature 共 18071 次、Document::findByName 共 6008 次，均来自编译、名称检查或元数据阶段，不再是百万次运行调用。LookupDefinition::evaluate 为 1002000 次，checksum 全部一致。gprof 的 BFD Dwarf 警告和插桩开销只影响辅助证据，绝对耗时仍以普通 Release 为准。
- 新增测试验证源 Formula/宏 AST 根指针、节点数和文本在编译后不变，移动 Plan 后递归 Fact(5)=120；名称索引测试覆盖 Lookup/Macro 双向冲突、Dimension/普通对象冲突、移动构造/赋值、删除后复用和 clear。
- Release all-test-targets 构建成功，Core/IO/corpus 和 SimulationController 均通过；Debug 全量 CTest 18/18 通过，Release/Debug 显式完整 XMILE corpus 均通过。Release editor-commands 曾出现一次 connectToolTargetsFlow 鼠标场景失败，单独复测通过。
- Release 剩余唯一失败为 pngExportAndClipboard：Windows OleSetClipboard/OpenClipboard 持续返回 0x800401d0，剪贴板被其它环境占用；多轮全量和单独复测均相同，与本轮 Core 改动无关。用户已明确确认该系统问题不作为阻塞并批准提交。
- 正式设计、对象寿命规则和 profile 已在实现验证后统一同步；本计划完成时文件改标为 done。

## 验收

- Document 源模型和原始 AST 不变，Plan 拥有全部执行 AST 且移动安全。
- 运行期不再按 nameKey 查询全局引用或函数；宏/Lookup 参数只求值一次。
- 名称冲突、宏重载、数组、取消/失败、持久化和历史 Run 语义保持。
- 编译成本有独立 profile，未通过隐藏复制或宏展开转移开销。
- Release、Debug 全量通过，profile 和实际过程写入文档。
