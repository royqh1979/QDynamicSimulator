# 仿真执行计划性能重构方案

创建时间：2026-11-28 07:48:46 +0800

状态：四个阶段均已实现，专项测试/profile 和 Release、Debug 全量均通过，计划已完成；未额外实施有限值扫描或状态容器改造。

## 1. 基线

完整测量方法、gprof 证据和环境限制见 [仿真计算热点审查](../profile/simulation-hotspots.md)。本方案使用以下固定基线，不在优化过程中更换模型、步长或校验方式：

| 场景 | 总耗时 | 归一化耗时 |
| --- | ---: | ---: |
| 标量 Euler，100 万步 | 448.134 ms | 0.448 μs/步 |
| 标量 RK4，100 万步、400 万阶段求值 | 1725.38 ms | 1.725 μs/步 |
| 16 元素数组 Euler，100 万步 | 15085.3 ms | 15.085 μs/数组步 |
| 结果表绑定，1000 次 | 约 1 ms | 约 1 μs/次 |

标量 RK4 与 Euler 的比例约 3.85，符合四次阶段求值；16 元素数组与标量 Euler 的总步耗时比约 33.7，主要异常是数组逐元素辅助工作。gprof 插桩约占自身 self-time 的 35.8%，只用于热点分布和调用次数，不用于生产绝对耗时。

热点优先级：固定数组布局重复建立、逐元素名称解析、Stock/Flow 对齐重复映射；EvaluationContext 重建、finite 扫描和状态容器复制随后单独评估。结果保存没有逐样本数值深拷贝证据，不作为首个优化对象。

## 2. 目标与边界

目标是消除固定语义在每一步/元素上的重复推导，同时保持当前数值结果、错误语义、生命周期和线程边界。

- 不用零散索引、同名布局特判或可变全局缓存掩盖现有执行结构问题。
- 不把 Document、Formula、AST、ModelElement 或 MacroDefinition 的裸指针写入 SimulationPlan。
- 不改变 Euler/RK4、采样时点、Stock 初值/净流量、数组广播/重排、宏作用域、取消和失败前缀语义。
- 不改变 SimulationRun 的结果内容、不可变性或 nameKey 查询边界。
- 每次运行仍使用不可变 Document 副本并重新编译；不引入需要手工失效的跨编辑缓存。

## 3. 对象与不变量变化

### 3.1 SimulationPlan：由顺序计划扩展为完整执行描述，仍不可变且自持有

当前 Plan 只保存 settings、初始化/运行期/输出 EntityId 顺序和 StockFlows。目标是在编译成功时一次性形成：

```text
SimulationPlan（编译发布后不可变）
├─ 既有 settings 与 EntityId 执行顺序
├─ 各变量的 ArrayLayout / 标量类别
├─ 普通公式的引用绑定值
└─ Stock 与各 Flow 的符号及布局对齐计划
```

新增值类型的约束：

- `ArrayLayout` 持有 dimensions、shape、strides 和元素数；维度数一致、元素数等于 shape 乘积且无溢出、strides 与维度顺序一致。Plan 发布后不得改写。
- 引用绑定以值表达 Variable/EntityId、Dimension 当前元素、特殊常量等解析结果。普通变量运行期不再重复 normalizeIdentifier 或 Document::findByName；宏局部名称仍优先按宏帧解析，不用全局绑定覆盖局部变量。
- Flow 对齐计划区分相同布局直接平铺、维度重排预计算映射和不兼容布局标记；不能在运行时按位置猜测。不兼容性在编译期识别，但运行时仍在原净流量阶段产生 `incompatible-stock-flow` 和既有失败前缀，不改成编译失败。
- 所有新增元数据由 Plan 以值/const 值容器拥有，不引用 Document 内部对象。Plan 移出编译结果后仍可独立保存其执行元数据，不增加新的悬空指针条件。

`SimulationCompileResult` 仍在成功时提供完整 Plan、失败时提供诊断；不得发布半初始化 Plan 供 Runner 使用。

### 3.2 StockFlows：转为或扩展为不可变 StockRatePlan

现有 StockFlows 只保存 Stock 与流入/流出 ID。目标是让编译期同时确定每个 Flow 的方向和数组对齐方式。它仍是 Plan 内的不可变值，不持有运行时 Value 引用，不保存随时间变化的净流量。

### 3.3 SimulationRunner / 运行状态：可变状态限制在单次运行

Runner 继续使用同步的 const Document& 和 const SimulationPlan&。新增或整理的运行状态、当前下标、阶段值和可复用上下文只存在于一次 `run()` 调用：

- 每次 Worker/运行有独立状态，不能跨 Run 共享可变的 time、DT、scope 或 Value。
- 可以访问 Plan 的 const 元数据，但不得把运行状态写回 Plan。
- 不在 Run 结果或异步回调中保存指向该临时上下文的指针。
- Document/Plan 的现有调用栈寿命规则保持不变；Worker 仍通过 SimulationRunRequest 同时拥有不可变 Document 副本和 Plan。

### 3.4 EvaluationContext / EvaluationScope：只允许运行期复用，不改变公式语义

现有 Context 每次 evaluate 重建。前三个阶段先消除布局、坐标和引用解析热点；若 profile 仍显示上下文重建显著，再单独改为单次运行内复用：

- time、实际 DT 和当前元素 indices 是可变运行状态；
- startTime、stopTime、当前变量及布局引用在一次求值中有明确受控设置；
- Context 不跨 Run、不跨 Worker、不成为历史结果；
- EvaluationScope 仍准确表达 dimensions、indices、self，复用不能留下上一变量或上一元素的 stale scope；
- 宏帧、默认参数和 SELF 语义保持不变。

该项是后续独立决策，不与第一阶段的布局优化捆绑实施。

### 3.5 保持不变的对象

- `Document`、`ModelElement`、`Formula`、AST、Dimension、MacroDefinition：所有权和现有编辑语义不变；公式仍由 Document 唯一持有。
- `AnalysisReport`：仍由编译器使用，不作为运行时全局缓存；必要绑定在编译时转入 Plan。
- `ArrayValue` / `Value`：继续以 dimensions、shape、values 和 QVector 写时复制表达数值，不增加指向 Plan 的反向指针。
- `SimulationRunRequest`：继续组合共享 const Document 副本和 Plan；不变对象由 Worker 共享读取，可变状态不下放到 Request。
- `SimulationRun` / `RunVariableInfo`：发布后不可变，标量/数组序列仍为 QVector 值返回，历史结果不受后续编译或编辑影响。
- 撤销/重做、QDS/XMILE 持久化和 UI 结果视图不保存或修改 Plan 内部状态。

## 4. 分阶段修改方案

1. **固定数组布局与无分配遍历**  
   建立 ArrayLayout 和变量执行描述，编译期计算 shape/strides；运行期复用布局，使用里程表式递增下标或其它无分配迭代。显式元素方程和 Apply-to-All 使用同一布局约束，不预存全部大数组坐标。

2. **普通公式引用预绑定**  
   编译期形成统一引用解析结果，运行期直接按 EntityId 读取状态，并将 contains+value 合并为一次查找。完整迁移普通变量、Dimension、特殊常量、宏参数/局部变量和 SELF，不长期保留两套普通名称解析规则。

3. **Stock/Flow 统一布局对齐计划**  
   相同布局直接按 flat 下标累加；重排布局使用编译期映射；不兼容布局在计划建立时标记，运行时沿用原诊断和失败前缀。保持标量广播、方向符号、失败位置和原子性。

4. **复审后决定上下文与扫描优化**  
   重新 profile 后，才决定是否复用 EvaluationContext、合并 finite 扫描或调整状态容器。每项独立计划和测量，不与前三项混成一次不可归因修改。

5. **每阶段验收**  
   独立分支、测试保护、专项 profile、自我审核；完成 Release 后 Debug 的 all-test-targets 与 CTest。每阶段停止等待用户审核，不自动提交、合并或推送。

## 5. 验证与完成边界

- 固定基线的 Euler/RK4/数组 checksum、样本数、时间点和最终值完全一致。
- 数组覆盖 Apply-to-All、显式元素、维度重排、标量广播、不兼容布局、零样本失败和取消。
- 引用覆盖普通变量、中文/空格 nameKey、TIME/DT、Dimension、SELF、宏参数、默认参数、局部变量和未知引用诊断。
- 检查 Plan 无 Document/AST/ModelElement/MacroDefinition 裸指针，无可变全局缓存，无跨 Run 可变上下文。
- 多 Run、旧结果、模型修改后重编译和 Worker 销毁不回归。
- 同环境 profile 报告各阶段绝对耗时和调用次数变化，不设置超出当前需求的极限门槛。
- Release 后 Debug 全量通过才视为对应阶段完成；既有环境失败必须明确报告，不以跳过测试代替。
