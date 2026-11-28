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

## 6. 实际执行过程

### 6.1 基线与热点确认

先保持生产代码不变，运行普通 Release `profile-simulation 1000`，再在独立目录以 `-O3 -g -pg` 构建相同负载并采集 gprof。确认标量 RK4/Euler 比例约 3.85，基本符合四次阶段求值；16 元素数组/Euler 总步耗时比约 33.7，异常集中在数组辅助处理。

热点证据包括逐元素 coordinates/flatIndex、固定 shape/strides 重建、名称规范化、Document/Dimension 查找、scope 元数据复制和 EvaluationContext/宏 reader 重建。gprof 插桩自身约占 35.8% self-time，因此后续以普通 Release 为绝对耗时依据，gprof 主要用于调用次数和路径核对。基线、调用次数和负载定义详见 `docs/profile/simulation-hotspots.md`。

### 6.2 第一阶段：固定数组布局与无分配遍历

分支：`perf/simulation-array-layout`；提交：`d5816a7 Precompute simulation array layouts`。

- 在 Plan 中增加自持有的 ArrayLayout，编译期固定 dimensions、shape、strides 和元素数，不保存 Document/Formula/AST 指针。
- Apply-to-All 直接递增 EvaluationScope.indices，显式元素使用预计算 strides；未提前实施引用绑定或 Flow 对齐。
- Core 仿真测试、Release 和 Debug 全量通过。
- 数组由 15085.3 ms 降至首轮 13601.7 ms、第三轮 13763.8 ms，正常轮次减少约 9.9%/8.8%。第二轮为 21535.8 ms，同时未修改的 RK4 也升至 4184.12 ms，保留为系统波动，不归因于本阶段代码。
- 插桩复查确认 QVector<QString>/QVector<size_t> 复制次数显著减少；Apply-to-All 的 Simulation coordinates 调用消除。

### 6.3 第二阶段：普通公式引用预绑定

分支：`perf/simulation-reference-binding`；提交：`42fa0b8 Prebind simulation formula references`。

- Plan 以值保存 Variable、Dimension、DimensionElement、SELF、TIME/DT/STARTTIME/STOPTIME、PI/INF 的 ReferenceBinding；不引入 Document 对象指针。
- 内置绑定覆盖同名模型名称，宏局部帧仍优先；变量读取由 contains+value 改为一次 EntityId 查找。
- 新增同名 TIME、Dimension 当前元素和 Dimension.Element 回归；宏、SELF、数组和未知引用沿用既有测试。
- 初版解析 lambda 捕获多个引用，两轮数组为 14776.5/14692.2 ms，比第一阶段正常结果慢。分析 std::function 小对象存储后，改为只捕获一个栈上 ResolvedReferenceContext 指针，数组降至 14435.5 ms，标量恢复。
- 随后将特殊常量/SELF 纳入统一绑定类型，避免普通引用先做多次字符串比较。持续热态出现 60891.9/60982.8 ms 异常；暂停后为 18025.4 ms。相同冷却条件下最终实现两轮为 11140.6/11875.1 ms，较原始基线减少约 26.2%/21.3%。
- gprof 确认 Document::findByName 从约 2104 万次降至 3003 次，findFunctionSignature 从约 3707 万次降至 1004 次。Release、Debug 全量通过。

### 6.4 第三阶段：Stock/Flow 编译期布局对齐

分支：`perf/simulation-flow-alignment`；提交：`945eb53 Precompute stock flow layout alignment`。

- StockFlows 改为持有 FlowRatePlan，固定 Flow、加减方向、直接平铺标志、兼容性和可选映射。
- 相同布局直接按 flat 下标累加；重排布局由 ArrayLayout 在编译期生成映射；标量广播保持原路径。
- 实施时发现原方案“不兼容布局在编译期报错”会破坏现有运行时失败及失败前缀，因此修正为编译期标记、运行时仍产生 `incompatible-stock-flow`。没有为追求优化而修改既有语义。
- 新增标量 Stock 接数组 Flow 用例；初次夹具漏定义 N 导致 setDimensions 失败，修正夹具后保留并验证原场景。
- 数组正常两轮为 9489.29/9616.61 ms，较原始基线减少约 37.1%/36.3%；中间热态异常为 36437.2 ms，原始数据保留。
- gprof 不再出现旧运行时 flatIndex，Simulation 的逐元素 coordinates 不再用于 netRates。Release、Debug 全量通过。

### 6.5 第四阶段：单次运行 EvaluationContext 复用

分支：`perf/simulation-context-reuse`；提交：`65350dd Reuse simulation evaluation contexts`。

- 第三阶段后仍有约 600 万次求值环境/宏 reader 建立，具备独立优化依据。
- Context、readers 和宏 reader 改为每次 run() 建立一次；evaluate 只更新 time、实际 DT 和当前 target 指针；每个变量仍清空并重设 scope。
- 可变上下文仅属于当前运行，不写回 Plan、不跨 Run/Worker、不进入历史结果。有限值扫描和状态容器未顺带修改。
- 两轮 Euler 为 313.911/311.886 ms，RK4 为 1450.11/1451.38 ms，数组为 9544.53/9594.27 ms；数组与第三阶段持平，标量和多阶段路径进一步改善。
- makeMacroReader 从约 600 万次降至 3001 次。Release、Debug 全量通过。

## 7. 最终对比与合并

最终实现相对原始基线的两轮结果：

| 场景 | 原始 ms | 最终两轮 ms | 降幅 |
| --- | ---: | ---: | ---: |
| 标量 Euler | 448.134 | 313.911 / 311.886 | 30.0% / 30.4% |
| 标量 RK4 | 1725.38 | 1450.11 / 1451.38 | 16.0% / 15.9% |
| 16 元素数组 Euler | 15085.3 | 9544.53 / 9594.27 | 36.7% / 36.4% |

各阶段 checksum、样本数、数组重排/广播/守恒、取消和失败语义均保持不变。主 profile 的归一化对比见 `docs/profile/simulation.md`，完整测量条件及异常轮次见 `docs/profile/simulation-hotspots.md`。

系列分支最终由 `64ced11 Merge simulation execution optimizations` 合并到 master；计划文件随后标记为 done。整个优化没有引入可变全局缓存、Plan 对 Document/AST 的裸指针或跨 Run 可变上下文。
