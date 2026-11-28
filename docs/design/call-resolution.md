# 统一名称管理与绑定 AST 设计

## 1. 目标与边界

Document 统一管理名称占用、对象定位和 invokable 定义。每次仿真仍在独立 Document 副本上编译，源 Document 及原始 AST 完全不变；SimulationPlan 拥有完整绑定 AST，运行期不再按 nameKey 查询全局引用或函数。

不使用 Plan revision、Plan 缓存、跨 Run Document 副本复用或 Document 内的 `name+actualArgumentCount` 派生表。重构计划与实际 profile 见 [Document 名称管理与 Plan 绑定 AST 重构计划](../history/2026-11-28-call-resolution-done.md)。

## 2. Document 名称管理

Document 自己承担 registry 职责，不增加独立 registry 对象或只为统一名称访问而增加多态基类。ModelElement、Dimension、MacroDefinition 各自保留具体 `displayName()`/`nameKey()` 接口：displayName 用于展示和持久化，nameKey 用于索引与语义解析。

逻辑上区分两类索引：

1. **Invokable 定义按 nameKey 聚类**
   - Builtin 来自进程级静态函数签名表，不由 Document 拥有；
   - Macro 使用 MacroSignature 有序容器保存重载，支持按 name 取得同名范围；
   - 独立 Lookup 使用 EntityId 定位，固定接受一个参数；
   - 带内联 Lookup 的 Variable/Flow 不因此成为独立 invokable。
2. **其它命名对象**
   - Stock、Flow、普通 Variable 使用 `nameKey -> EntityId`；
   - Dimension 使用 `nameKey -> Dimension 下标`；
   - Dimension 当前只可追加，下标在 Document 内容不变时稳定；未来删除、改名或重排必须由 Document 同步维护。

索引只保存类型和定位值，不保存长期对象指针。Lookup 只登记在 invokable 侧；`Document::findByName()` 等门面隐藏索引分布，调用方不自行组合多张表。

名称规则：

- 创建、恢复、改名和名称生成检查全部名称类别；任意 Macro 重载占用名称即拒绝其它同名对象。
- Builtin 名称不能被 Macro、Lookup 或普通对象覆盖。
- 定义 Macro 时按 name 取得重载集合，以正式形参总数、必需参数个数和输出个数检查重复及默认值范围重叠。
- 纯 name 查询用于命名冲突及区分“函数不存在”和“名称存在但没有匹配参数个数”。
- 普通裸引用不调用零参数 Macro；宏参数和 DimensionElement 遵守局部/所属作用域，不进入 Document 全局名称表。
- 创建、恢复、改名、删除、宏新增首个重载、clear 和移动必须保持对象容器与两类索引一致；宏新增其它重载及 `setMacro()` 不改变名称绑定。

`findByName()`、`findDimension()`、`findMacro()` 等多态查询仍返回仅限当前同步调用栈使用的 const 指针；Document 修改、移动、清空或销毁后重新查询。

## 3. Plan 拥有完整执行 AST

每次 UI 运行通过现有 QDS 内存通道创建独立 Document 副本，再以只读方式编译：

```text
SimulationRunRequest
├─ shared_ptr<const Document> model
│   └─ 原始 Formula / AST，保持不变
└─ SimulationPlan plan（仅可移动）
    ├─ Variable/Flow 的 CompiledFormula
    ├─ Stock 初值和数组逐元素 CompiledFormula
    ├─ CompiledMacroDefinition
    │   ├─ 宏体绑定 AST
    │   └─ 默认参数绑定 AST
    ├─ 执行顺序、数组布局和 Stock/Flow 关系
    └─ 其它不可变执行信息
```

CompiledFormula/CompiledMacroDefinition 是 Plan 私有的不可变执行表示，不是第二套可编辑模型。Document 仍是源文本、原始 AST 和持久化的唯一权威。`compileSimulation(const Document &)` 不修改 Document。

Plan 以 `std::map<MacroSignature, unique_ptr<CompiledMacroDefinition>>` 等稳定堆容器管理编译对象，移动 Plan 不使内部绑定指针失效；禁止复制整套绑定图。历史 SimulationRun 不保留 Document、Plan 或 AST 指针。

## 4. 绑定节点与编译规则

显式 Bound 节点位于 `core/formula/Binding.h`：

- `BoundReferenceNode`：绑定全局变量 EntityId、Dimension 当前元素、限定 DimensionElement、TIME/DT/STARTTIME/STOPTIME/PI/INF 等语义。
- `LocalReferenceNode`：表示宏局部参数，不能绑定到同名全局实体。
- `BoundCallNode`：区分 Builtin、Macro、Lookup，保存已绑定参数子树。
- Builtin 保存函数签名；Builtin 本身没有公式 AST。
- BoundMacroCall 指向 Plan 拥有的 CompiledMacroDefinition；宏体和默认参数均在 Plan 中完成绑定。
- 独立 Lookup 调用指向运行 Document 副本中的 const LookupDefinition；Lookup 本身没有公式 AST。
- SELF 保留当前输出元素语义；所有节点保留 SourceSpan。

编译器只遍历定义，不按宏调用次数展开 AST。预分配 Plan 内全部编译宏后再绑定宏体和调用，直接/间接递归都引用稳定的 Plan 内定义，不复制调用链。

未知变量、未知函数、无匹配重载和错误参数个数在编译阶段诊断。`name + actualArgumentCount` 只用于编译过程中的调用选择，不形成 Document 或运行期查表结构。

## 5. Runner 与通用求值

- SimulationRunner 从 Plan 获取绑定 AST，不再从 Document 获取用于执行的原始公式。
- 绑定全局引用直接按 EntityId 读取当前状态；绑定调用直接执行，不再使用 ReferenceBinding/readResolvedReference。
- 宏实参和实际使用的默认参数仍各求值一次；条件短路、递归深度、数组广播、非有限值和错误位置保持不变。
- 通用无编译 `evaluateFormula()` 继续接受原始 AST 及现有 readers，用于非仿真公式求值；原始和绑定 Builtin 执行逻辑共用同一实现。
- 当前编辑 Document 的后续修改不影响 Worker；Request 的共享所有权覆盖副本、Plan 及其内部绑定对象的整个使用期间。
- SimulationRun::modelRevision 继续以来源当前 Document 的 simulationRevision 判断 Outdated，与 Plan 无关。

## 6. 验证与 profile

- 名称冲突、宏重载/默认参数、Lookup、中文名称、改名、删除/恢复、clear 和移动后索引一致。
- 原始 Document、公式文本、AST 和 SourceSpan 在编译前后不变；Plan 移动后内部绑定仍有效。
- 普通引用、特殊常量、数组下标、显式元素、维度重排和广播。
- 宏局部遮蔽、嵌套/直接/间接递归、默认参数和实参单次求值；AST 节点数不随调用链展开。
- Builtin/Macro/Lookup、未知函数和错误 arity；未知或错误调用不执行参数。
- 绑定 AST 与通用求值结果对比，Euler/RK4、取消、失败及黄金结果一致。
- 当前 Document 在 Worker 运行期间编辑、窗口关闭及不同销毁顺序不使执行对象失效。
- QDS 往返、MDL/XMILE 参考结果及完整 XMILE corpus。
- 固定 `profile-simulation 1000` 和宏公式负载分别记录编译、运行、归一化耗时及 checksum；以插桩调用次数确认运行期全局 `findByName`、`findFunctionSignature` 和 `readMacro` 查询被消除。
- 单独审查 AST 编译耗时、节点数和分配。若每次编译成为实际热点，停止并重新讨论缓存方案，不预埋 revision 或缓存。
