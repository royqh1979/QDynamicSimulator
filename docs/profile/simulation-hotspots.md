# 仿真计算热点审查

## 方法与负载

本次只做测量和修改建议，未修改生产代码。

- 普通基线：现有 Release `profile-simulation 1000`，MinGW GCC 8.1.0、Qt 5.15.18 静态 Release。
- 热点构建：独立构建目录使用 `-O3 -g -pg` 和链接 `-pg`，运行相同 `profile-simulation 1000`，再以 gprof 读取 flat profile 和调用链。
- 固定负载：标量指数模型 Euler/RK4 各 100 万步；数组模型为 16 元素 Stock、Flow=`Stock * N`、Euler 100 万步、1000 次运行。
- gprof 的 `_mcount_private` 与 `__fentry__` 合计占其 self-time 约 35.8%，属于插桩开销，不作为生产热点。gprof 数组总耗时 21.636 s，高于普通 Release 的 15.085 s，因此绝对耗时以普通 Release 为准，调用次数和函数分布用于定位重复工作。

普通 Release 结果：

| 场景 | 总耗时 | 归一化耗时 |
| --- | ---: | ---: |
| 标量 Euler，100 万步 | 448.134 ms | 0.448 μs/步 |
| 标量 RK4，100 万步、400 万阶段求值 | 1725.38 ms | 1.725 μs/步 |
| 16 元素数组 Euler，100 万步 | 15085.3 ms | 15.085 μs/数组步，0.943 μs/元素步 |

标量 RK4/Euler 总步耗时比约 3.85，与四次阶段求值基本相称。16 元素数组/Euler 总步耗时比约 33.7，明显高于仅按元素数增加的 16 倍，数组逐元素辅助处理是首要审查对象。

## 热点证据

插桩运行的主要非插桩 self-time 和调用次数显示：

- `coordinates()` 在 Simulation 和 Evaluation 两处分别执行约 1603 万次；Simulation 中主要由 `netRates()` 的逐元素 Flow 对齐调用。
- `flatIndex()` 执行约 1603 万次，并重复校验形状、计算 strides、线性查找维度名称。
- `Document::findDimension()` 约 8617 万次，`normalizeIdentifier()` 约 1.072 亿次，`Document::findByName()` 约 2104 万次，`findFunctionSignature()` 约 3707 万次。
- `QVector<size_t>` 的析构、复制、append/realloc 和分配处于前列；每次坐标生成、scope 设置和数组元数据复制都产生引用计数或分配工作。
- `QVector<QString>` 复制、QString 析构/append/realloc 同样显著，主要来自反复复制 dimensions 和重复名称规范化。
- `ArrayValue`/`Value` 的构造、析构和 QHash 操作可见，但结果保存使用 QVector 写时复制，没有证据表明保存历史结果发生了每点数值深拷贝。
- gprof 对同名小函数和部分内联调用的调用图归属存在噪声；本结论不采用明显不合理的归属计数，也不把各函数 inclusive 时间相加。

## 不必要处理环节

### 1. 固定数组布局在每个时间步、每个元素上重新建立

`SimulationRunner::run()` 对数组变量反复执行：查找 Dimension、构造 shape、分配 output、生成 coordinates、复制 dimensions/nameKey 到 scope。数组维度、shape 和 strides 在不可变运行副本中固定，不应在每步重新推导。

`coordinates()` 还会为每个元素重新计算 strides 并分配 indices QVector。仅为了避免分配而预计算全部坐标也不一定是最佳方案，因为大数组会浪费内存；更合理的是使用里程表式递增下标迭代，或让执行计划持有预计算布局并采用无分配迭代器。

### 2. 公式引用在每个元素上重复做名称语义解析

普通变量引用会先经过函数签名、Dimension 判断和 nameKey 规范化，再通过 Document::findByName 找 EntityId；读取状态时又分别执行 contains 和 value 两次 QHash 查询。相同运行副本中，引用到变量、Dimension、特殊常量的绑定关系不变，不需要在每个元素/阶段重新分析。

这不是简单增加一个孤立索引就能完成的优化：应让编译/执行计划明确保存解析结果，同时保留宏局部变量、SELF、TIME/DT 和 Dimension 当前元素等原有优先级。

### 3. Stock/Flow 对齐在每一步重复计算维度映射

`netRates()` 对每个数组元素执行 coordinates + flatIndex，flatIndex 又重复 shape 校验、strides 和维度名称线性查找。Stock 与 Flow 的布局在编译后固定：

- 相同 dimensions/shape/order 时应直接按 flat 下标对齐；
- 维度重排时应由执行计划预先生成一次映射；
- 不兼容布局应在编译或计划建立时校验，而不是在每个时间步重复发现。

该建议依赖统一的 ArrayLayout/映射设计，不是在现有循环里再加一个特判补丁。

### 4. 每次求值重建通用上下文

每次 evaluate 都重新建立 EvaluationContext 和 readVariable/readDimension/readMacro 的 std::function。可以随执行计划/运行状态建立稳定上下文，每步只更新时间、DT 和当前元素 scope。此项应在布局与引用绑定之后测量，避免先优化较低层级接口而掩盖主要重复工作。

### 5. 次要遍历和复制

数组求值后 `finiteValue()` 会再次扫描完整输出，积分后也扫描 Stock。可以在保持错误位置和失败原子性的前提下评估是否并入元素计算/积分，但当前证据不足以把它列为第一优先级。Euler/RK4 的 state/rate 容器复制也应留到主要重复工作消除后再测量。

## 建议实施顺序

1. 为 SimulationPlan 增加固定数组执行描述：公式/变量绑定、dimensions、shape、strides、标量/数组类别和合法布局；将坐标遍历改为无分配迭代。
2. 在编译期完成公式引用绑定，运行期直接按 EntityId 读取状态，并将 contains+value 合并为一次查找；专项验证宏、SELF、Dimension 和特殊常量优先级。
3. 用统一 ArrayLayout 对齐 Stock/Flow，相同布局直接平铺，重排布局复用预计算映射。
4. 再处理 EvaluationContext 重建、成功路径临时对象和重复 finite 扫描。
5. 每项独立建立性能重构计划、解析解/数组重排/宏/取消/失败回归和同场景 profile，不同时混入多个优化，也不预设极限性能目标。

当前不建议优先修改结果保存、增加零散缓存或只给现有函数加同名布局特判；这些都不是已测热点的主要成因。

## 第一阶段：固定布局与无分配下标遍历

实施范围仅为 SimulationPlan 自持有 ArrayLayout、Apply-to-All 直接在 EvaluationScope.indices 中递增下标、显式元素使用预计算 strides。引用预绑定和 netRates 对齐映射未提前实施。

普通 Release `profile-simulation 1000` 结果：

| 场景 | 基线 ms | 第 1 轮 ms | 第 2 轮 ms | 第 3 轮 ms |
| --- | ---: | ---: | ---: | ---: |
| simulation_array_euler | 15085.3 | 13601.7 | 21535.8 | 13763.8 |
| simulation_euler | 448.134 | 440.214 | 447.288 | 445.459 |
| simulation_rk4 | 1725.38 | 1729.34 | 4184.12 | 1704.48 |

第 2 轮中未修改的 RK4 也增至基线约 2.43 倍，显示为系统级波动而非可由本阶段单独解释的数组回归，因此原始记录全部保留。第 1、3 轮数组分别减少约 9.9% 和 8.8%；标量 Euler 和正常轮次 RK4 与基线相近。所有轮次样本数、标量/数组 checksum 均一致。

插桩复查中，QVector<QString> 复制由约 10722 万次降至 7614 万次，QVector<size_t> 复制由约 10721 万次降至 7513 万次，符合移除逐元素 scope/坐标复制的预期。插桩后的绝对耗时波动和开销较大，只使用调用次数作辅助证据。Apply-to-All 的 Simulation.cpp coordinates 调用已消除；netRates 的 coordinates/flatIndex 路径留给第三阶段。

本阶段 Release 和 Debug 全量 CTest 均为 18/18 通过，包括 Apply-to-All、多维/显式元素、维度重排、广播、守恒、失败和取消。

## 第二阶段：普通公式引用预绑定

Plan 以自有 ReferenceBinding 值保存 Variable、Dimension、DimensionElement、SELF、TIME/DT/STARTTIME/STOPTIME、PI/INF 的解析类型。普通引用只查询一次绑定并直接按 EntityId 读取状态；内置绑定覆盖同名模型名称，宏局部帧保持优先。解析 std::function 只捕获一个栈上 ResolvedReferenceContext 指针，避免多引用捕获造成额外堆分配。

最终实现冷却后的两轮普通 Release 结果：

| 场景 | 原始基线 ms | 冷却轮次 1 ms | 冷却轮次 2 ms |
| --- | ---: | ---: | ---: |
| simulation_array_euler | 15085.3 | 11140.6 | 11875.1 |
| simulation_euler | 448.134 | 426.435 | 325.778 |
| simulation_rk4 | 1725.38 | 1587.78 | 1551.44 |

两轮数组相对原始基线分别减少约 26.2% 和 21.3%，并低于第一阶段正常结果；Euler/RK4 也优于原始基线。所有轮次 checksum 与样本数一致。

开发过程中，多引用捕获版本两轮数组为 14776.5/14692.2 ms；缩小捕获后为 14435.5 ms。统一绑定后在持续热态下还出现过 60891.9/60982.8 ms 的异常轮次，暂停负载后降至 18025.4 ms，再在相同冷却间隔后取得表中两轮。原始结果不删除，最终结论只使用冷却条件明确且可重复的轮次，不用热态数据做绝对性能比较。

gprof 调用次数确认：`Document::findByName()` 从约 2104 万次降至 3003 次，`findFunctionSignature()` 从约 3707 万次降至 1004 次；名称规范化和 Dimension 查询也显著减少。netRates 的坐标/flatIndex 仍是第三阶段目标。

专项测试新增同名 TIME 优先级、Dimension 当前元素和 Dimension.Element；宏参数/局部变量、SELF、数组和未知引用由既有测试继续覆盖。本阶段 Release、Debug 全量 CTest 均为 18/18 通过。

## 第三阶段：Stock/Flow 编译期布局对齐

StockFlows 现在持有不可变 FlowRatePlan：Flow、加减方向、直接平铺标志、兼容性和可选映射。相同 dimensions/shape 直接按 flat 下标累加；重排布局由 ArrayLayout 在编译期生成 Stock flat 到 Flow flat 的映射；标量广播保持原路径。不兼容布局在编译期标记，运行时仍产生原有 `incompatible-stock-flow` 和失败前缀。

普通 Release 结果：

| 场景 | 原始基线 ms | 第 1 轮 ms | 热态异常轮次 ms | 延长冷却后第 3 轮 ms |
| --- | ---: | ---: | ---: | ---: |
| simulation_array_euler | 15085.3 | 9489.29 | 36437.2 | 9616.61 |
| simulation_euler | 448.134 | 327.956 | 327.446 | 327.890 |
| simulation_rk4 | 1725.38 | 1513.26 | 1535.93 | 1548.68 |

两个正常轮次数组相对原始基线分别减少约 37.1% 和 36.3%。异常轮次的标量 Euler/RK4 仍处于正常范围，数组单独出现长负载波动，因此保留原始数据但不用于代码收益归因。最终结论基于两个相近轮次。所有 checksum 和样本数一致。

gprof 复查不再出现旧的运行时 flatIndex，Simulation 的逐元素 coordinates 不再用于 netRates。专项测试覆盖同布局、维度重排、标量广播、不同维度/形状失败及标量 Stock 接数组 Flow，失败仍发生在运行期并保留前缀。本阶段 Release、Debug 全量 CTest 均为 18/18 通过。

## 第四阶段：单次运行 EvaluationContext 复用

第三阶段后仍有约 600 万次求值环境/宏 reader 建立，因此在 run() 内建立一次 EvaluationContext、readers 和宏 reader。每次 evaluate 只更新 time、实际 DT 和当前 target 指针；每个变量继续清空并重设 scope。ResolvedReferenceContext 使用可切换的 target 指针，不跨 Run 保存状态，也不改变 Plan/Document/Run 的不可变性。有限值扫描和状态容器结构未在本阶段修改。

相同冷却条件下的两轮普通 Release 结果：

| 场景 | 第三阶段正常结果 ms | Context 复用第 1 轮 ms | Context 复用第 2 轮 ms |
| --- | ---: | ---: | ---: |
| simulation_array_euler | 9489.29 / 9616.61 | 9544.53 | 9594.27 |
| simulation_euler | 327.956 / 327.890 | 313.911 | 311.886 |
| simulation_rk4 | 1513.26 / 1548.68 | 1450.11 | 1451.38 |

数组基本持平，Euler/RK4 进一步降低，符合 Context 复用主要影响标量和多阶段求值的预期。所有 checksum 和样本数一致。gprof 中 makeMacroReader 从约 600 万次降至 3001 次。本阶段 Release、Debug 全量 CTest 均为 18/18 通过；没有继续加入 finite 扫描或状态容器优化。
