# 延迟求值与数组值性能优化计划

创建时间：2026-11-28 21:16:57 +0800

## 目标与证据

基于 `docs/profile/delay-functions.md` 的固定 Release 基线和独立 gprof 结果，分阶段消除已证实的无效速率计算、通用数组参数复制和重复分配。每阶段保持数值序列、采样时点、初始化规则、错误/取消行为、数组元素独立性和历史 Run 内容不变。

基线提交为 `a95eba4`。统一性能负载为 `profile-delay 100 1000 16`，同时保留 SDXorg 三组参考、解析解/RK4 收敛和 Release 后 Debug 全量门禁。阶段一使用独立分支 `perf/delay-rate-selection`；后续阶段从已审核的前序提交建立独立分支，不自动合并或推送。

## 不变量

- Fixed 只推进离散管道，不具有微分速率。
- 离散 DELAY N/SMOOTH N 在每个实际 DT 只使用 K1 推进一次；K2–K4 仅供连续状态使用。
- 每个 Run 独立拥有状态与工作区，不能跨 Run 引入需要手工失效的缓存。
- 公开公式和 Run 结果保持值语义；不得把栈槽、QHash 节点或临时数组的裸引用泄漏到长期对象。
- 数组参数只求值一次；需要独立输出时显式创建新数组，不能通过修改共享输入制造结果。
- 优化不得以移动诊断阶段或改变失败前缀换取表面性能。

## 阶段一：按状态语义选择速率计算

热点：Fixed 在实际步创建无效速率结果；离散 RK4 的 K2–K4 结果未使用。

目标模型：

- 速率计算区分实际步与 RK 中间阶段，不使用两个含义相近的布尔开关。
- Fixed 不进入速率结果分配和 Euler 速率积分。
- 离散 N 仅在实际步计算 K1；连续状态在实际步和中间阶段计算。
- 没有有效速率的场景返回空工作结果，积分循环只访问具有速率的计划。

验收：Fixed-only RK4、离散-only RK4、Fixed/离散/连续混合模型及现有参考全部通过。gprof 中目标负载的 `delayRates` 调用由 700400 降为 500300；checksum 不变。记录两轮总耗时和归一化耗时，不将调用次数降幅当成总耗时降幅。

## 阶段二：统一数组堆句柄与参数传递

热点：ExecutionStack 读取宏参数/局部值会复制 `Value`，ArrayValue 复制包含 dimensions、shape、values 三个 QVector 的元数据；数组 Delay 还重复求值参数。

采用通用 ArrayValue 堆句柄，而非 Delay 专用辅助函数或 borrowed EvaluationResult：

- ArrayData 在堆上保存 dimensions、shape、values；ArrayValue 作为共享句柄保留在 `std::variant<double, ArrayValue>` 中。
- ArrayData 类型允许可变，但函数参数只读；新结果显式分配新对象，不能修改共享输入制造输出。
- ExecutionStack、宏参数和结果只复制共享句柄；堆对象稳定，不依赖 QVector 栈槽地址。
- 发布到历史 Run 后不再写入，UI 和其它线程只读；不引入自研 Heap、GC、全局锁或强制全类型不可变。
- 统一当前元素选择，避免重复 AST 求值及为读取单个元素创建完整广播数组。

比较当前 QVector COW 表示与堆句柄表示，覆盖宏数组参数、广播、下标、栈扩容、历史采样和 Delay 数组。若小数组因额外分配稳定回退，保留当前表示或重新审查，不强行替换。

## 阶段三：复用延迟速率工作区

热点：每次速率计算创建完整状态长度 QVector。

目标：Run 内预分配可复用速率工作区，只清理和计算启用槽位；Fixed 和离散中间阶段不占用工作。不得共享跨 Run 工作区。先由阶段一/二结果确定该阶段是否仍有明显收益。

## 阶段四：取消

用户决定取消 RK 阶段状态拆分。只有优化后仍存在明确且值得处理的剩余热点时才继续其它分析，不预设需要 DelayState 拆分、数组槽或 Arena。

## Delay 访问寿命与参数装箱优化

- 分支 `perf/delay-access-lifetime`。剩余插桩仍显示 input/time 视图的 dimensions/shape 元数据复制和构造/析构，Delay 每元素还将 double 重新装箱为 Value。
- ArrayElementView 改为只持有调用期间有效的 dimensions/shape const 引用；Plan 或调用函数局部对象均覆盖视图寿命，不新增长期指针。
- Delay 的 evaluateOne 直接接收 double，标量路径只拆箱一次，数组视图读取后不再构造四个 Value variant。保持状态推进、错误语义和现有执行路径。
- 对比上一阶段相同负载并执行全量回归；本阶段不属于 RK 状态拆分，不自动提交。

### 实施结果与计划收尾

- ArrayElementView 的 dimensions/shape 改为 const 引用，Plan 或调用方局部对象覆盖视图寿命；未新增跨调用指针。
- Delay 的 evaluateOne 直接接收 double，标量路径只拆箱一次，数组路径不再逐元素构造四个 Value variant。
- 相对上一阶段，Material/Information 数组再降约 5.4%/5.6%；与最初基线相比每元素步约 576.0→290.9 ns、525.5→241.3 ns。checksum 不变。
- 剩余主要是实际状态计算、RK 阶段和输出管理，没有足够证据继续做 DelayState 拆分、统一热循环或容器特判。Release 与 Debug 的 18/18 CTest 和完整 XMILE corpus 通过，当前阶段尚未提交。
- 阶段四按用户决定取消；本优化计划到此结束，不再为完成计划寻找微优化。

## 每阶段验收与记录

1. 在 history 记录实现、异常和设计修正，同步当前设计文档。
2. 保留正常、边界、错误、数组、Euler/RK4 和黄金文件回归。
3. 使用相同 Release 负载记录优化前后两轮总耗时、每步/每元素步、分配和 checksum。
4. Release 后 Debug 构建 `all-test-targets` 并执行完整 CTest/XMILE corpus。
5. 自我审核后停止。阶段一按用户授权提交；阶段二只报告 profile 比较，等待用户决定。
6. 清理本任务专用插桩构建和结果目录，保留标准 Release/Debug 目录及正式文档。

## 阶段一实际过程

- 在 `perf/delay-rate-selection` 使用 `DelayRateStage` 区分实际步和中间阶段；Fixed-only 不分配速率，离散-only 的 K2–K4 为空。
- 中间阶段插值原路径会遍历全部 Level，直接跳过速率计算会产生空向量访问。新增 `advanceContinuousDelayStages`，中间阶段只推进连续计划，离散输出和状态保持冻结。
- Euler 改为只积分非 Fixed 的实际阶数槽位，不再遍历 Fixed 和通用 N 的无效预留槽。
- 新增 Fixed-only RK4、Fixed/离散/连续混合测试；专项测试与三组 SDXorg 参考通过，checksum 不变。
- 首轮 Debug 发现 RK 最终合成仍访问 Fixed 的空 K1；Release 对应路径属于未定义行为。根据完整调用栈在最终合成中跳过 Fixed，Debug/Release 专项通过后，废弃并重测所有受影响 profile 轮次。
- 最终两轮 Release 显示离散 RK4 总耗时下降 32.9%，Fixed 下降 21.9%；数组 Material/Information 两轮均值分别下降 4.1%/4.0%。连续 DELAY3 均值增加 0.9%，围绕基线波动，未判定稳定回退。
- gprof 的 QVector resize 减少 200050 次，与目标调用完全一致；C++ 分配计数不变。完整数据见 `docs/profile/delay-functions.md`。
- 修正后 Release 与 Debug 均重新构建 `all-test-targets`，18/18 CTest 和完整 XMILE corpus 通过；阶段一提交为 `dea3337`，未合并、未推送。

## 阶段二实际过程

- 在 `perf/array-value-handles` 先建立 `profile-array` 基线，覆盖身份读取、16 层宏、广播和标量下标，数组大小 16/4 各两轮；不只用 Delay 结果选择表示。
- ArrayValue 改为共享 ArrayData 堆句柄。const 读取不复制三组 QVector；非 const `values()` 在不唯一时克隆，保持 `seriesValueSemantics` 和历史 Run 不被副本反写。ArrayData 不强制不可变，但参数只读、结果独立创建、发布后不再写。
- 新增 24 层嵌套宏数组参数、栈扩容和共享输入不被新结果修改的正式测试。
- 引入通用 ArrayElementView，Delay 与 mapped functions 共用；参数只求值一次，不再为当前元素生成完整广播数组。
- 初版视图对相同布局也创建映射/strides，导致 Delay 数组回退约 35%。改为相同布局直接行主序寻址后，Material/Information 相比阶段一分别下降 36.1%/37.5%，相对最初基线下降 38.7%/40.0%。
- Evaluator 两轮对比：身份读取约下降 18.2%/21.8%（16/4 元素），嵌套宏下降 8.3%/10.0%，下标下降 5.8%/8.8%；广播因 ArrayData 发布分配增加而回退 10.6%/5.4%。未隐藏不利场景。
- 数组 Delay 的 C++ 分配从 13 次/Run 增至 2018 次/Run，反映每个变量发布的 ArrayData；当前负载下收益大于成本。完整数据见 `docs/profile/array-values.md`。
- 最终相等性审查避免以句柄相同替代内容比较；测试区分共享 NaN 数组与独立 NaN 数组，保持 QVector 既有语义。`all-profile-targets` 构建通过。
- Release 与 Debug 均重新构建 `all-test-targets`，18/18 CTest 和完整 XMILE corpus 通过。阶段二在 profile 审核后提交为 `93adc4f`，未合并、未推送。

## 广播归因补充实验

- 在 `perf/array-broadcast-analysis` 以 `93adc4f` 为基线，A 仅将二元输出改为局部 QVector 一次性发布，16/4 元素仅改善约 1.4%/0.9%，重复句柄检查不是主因。
- B 在 A 上移除标量临时广播数组，operator new 从每次迭代 2 次降为 1 次，16/4 元素相比当前基线改善 27.3%/22.9%。
- 原 QVector COW 实现也会创建标量临时 values。B 同时移除临时 values 和 ArrayData，不能将回退单独归因于 ArrayData；B 比原 COW 基线还快约 19.8%/17.6%。结论限定为完整标量临时数组是当前最大可避免成本。
- 标量左乘和标量除以数组测试加入，B 上 Release/Debug 18/18 CTest 与完整 XMILE corpus 通过。当前实验提交为 `8064c90`，未合并、未推送。

## 跨表示同优化对照与最终归因

- 从 master `af25198` 建立 `perf/array-broadcast-cow-baseline`，在旧 QVector COW 上实施相同 B 优化，旧分支提交为 `8622cda`，不合并。
- 早期交替测量出现新句柄 4 元素约 200 ns/次差距，后续无法复现；不将该异常作为最终归因。稳定轮次中 A+1 差距约 22/13 ns（16/4），A+A 新句柄反而略快。
- 新增 `profile-array-operations`：ArrayData 空对象创建约 30 ns，完整计算发布差距约 32–34 ns；直接和 variant 共享句柄复制反而快约 18–20 ns。分配计数原子操作约增加 7 ns，不能解释早期大差距。
- 最终结论收窄为：标量临时 values 是共有冗余；重复唯一性检查影响很小；ArrayData 创建/发布是与稳定公式差距同量级的新增固定成本，共享复制收益会抵消一部分。早期大差距不作为设计归因。
- 最后固定 `8622cda` 与 `8064c90` 两个独立可执行文件，每组 300000 次迭代正反向交替四轮：A+1 新句柄平均慢 4.48%/3.51%（16/4），A+A 快 1.70%/1.75%。可重复差距约 4%，不是早期约 40%。
- 临时对照可执行文件、独立 worktree/构建与 nm 输出在记录后删除；详细数据见 `docs/profile/array-values.md`。

## 普通数组运算统一阶段（放弃）

- 分支 `perf/array-elementwise-views`。尝试以统一 `mapElements` 覆盖普通二元、一元、数组条件和 mapped functions，复用 ArrayElementView 与局部 values 输出。
- 初版使 A+1 从约 121/111 ms 回退到 447/208 ms；增加坐标复用、减少结果包装和直接 double 读取后仍为 148/134 ms，未恢复基线。A+A 虽有改善，但不能抵消标量路径回退。
- 评审结论：统一语义和校验不等于统一热循环。标量、同布局和重排访问需要不同的简单执行路径；继续增加 direct/atDirect 特例会形成补丁式抽象。相关生产改动已撤回，不提交；保留 master 的简单分派、ArrayValue 句柄及 Delay/mapped functions 的 ArrayElementView。
- 后续不采用模板实例化掩盖同一路径重复，也不为了形式统一引入运行时多态。普通数组优化回到具体冗余和分配。

## 热循环分类审查

- 已按输入访问、整体/当前元素、可失败运算、归约/排序/布局转换、连续/离散/Fixed 状态、Euler/RK 阶段和 Run 输出重新分类，见 `docs/design/array-access-and-computation.md` 第 9 节。
- 结论：标量直接、同布局直接、重排、可失败 mapped、归约、排序、布局转换、Apply-to-All、连续/离散/Fixed、Stock/Flow 与输出保存不能由一个抽象循环覆盖。
- 一元/二元/条件可共享直接访问策略，时间/随机可共享可失败 mapped 策略；其它拆分必须有稳定热点证据。统一校验和所有权，不建立平行语义。

## 阶段三：复用延迟速率工作区

- 分支 `perf/delay-rate-workspace`。以 master 的 `197d845` 为基线，只修改 Run 内延迟速率结果的持有方式。
- 目标：每个 Run 按 Euler/RK4 需要预分配 K1–K4 工作区，速率计算写入复用缓冲区；没有有效速率时清空逻辑结果，不跨 Run 共享，不改变 Fixed/离散/连续分流。
- 保持数值序列、采样时点、错误/取消行为、数组状态独立性和历史 Run 不变。比较两轮 `profile-delay 100 1000 16` 的总耗时、归一化耗时、checksum 和分配，必要时以插桩确认速率容器分配消失。
- 完成后执行 Release 后 Debug 全量并等待审核，不自动提交。

## 阶段三实际过程（放弃）

- 新增 Run 局部 DelayWorkspace，K1 按级联计划分配，连续 RK4 再分配 K2–K4；`delayRates` 改为输出到调用方缓冲区并只返回可选诊断。
- 第一版 `QVector::fill(value,size)` 仍逐步 resize，改为尺寸不变时 `std::fill`。最终 gprof 的 resize 从约 502350 次降至 2500 次，realloc 比阶段一减少约 100 万次，checksum 不变。
- 基线两轮与工作区三轮对比：连续 DELAY3 均值快 5.5%，离散 RK4 基本持平；Material/Information 数组均值回退 1.6%/3.3%，Information 数组逐轮差异较大。
- 评审认为预分配与逐步创建没有算法差异，清零也未证实为热点；realloc 次数不能代表主要成本。连续场景的小幅收益不足以支撑该重构，数组场景还存在回退，因此撤销全部生产改动，不提交、不合并。后续按场景重新分析真实热点。

## 分场景热点分析

- 撤销工作区后，以独立 `-pg` 构建分别测量离散 RK4、连续 DELAY3 和 Material/Information 数组；profile 工具增加场景过滤参数。
- 标量主要成本在 QHash/variant/EvaluationResult 和 RK 状态容器操作，速率分配不是唯一大项。
- 数组 Information 的 `Document::findDimension` 约 641 万次，与 100 Run×1000 步×16 元素×4 视图精确对应；相同布局仍重复查询输入 Dimension。Material 还可见大量 detach 检查、QString 元数据复制、视图构造/析构和 delayRates 工作。
- 源码另确认：每个元素都扫描全部初始化槽，形成 16×16 稳态检查；initial/order 视图在初始化完成后仍逐步创建。
- 后续应先减少每元素重复准备：只检查当前初始化槽、稳态仅建立 input/time、同布局避免重复 Dimension 查询；验证后再决定是否提升布局描述或拆分 RK 状态。报告见 `docs/profile/delay-hotspots.md`。
- gprof 存在插桩开销和部分内联符号噪声，报告只采用能与源码和规模互证的数据，不引用异常函数名。

## Delay 每元素重复准备优化

- 分支 `perf/delay-evaluation-setup`，只处理热点报告确认的同源冗余，不改普通数组运算或速率容器。

### 责任审计与目标模型

- Runner 已经拥有 ArrayLayout，但 Delay 每元素重新 shapeFor 并创建四个视图，属于调用方丢弃编译期事实。不能通过优化 `Document::findDimension` 或继续增加 direct/atDirect 特例掩盖。
- DelayPlan 改为值持有当前公式的 dimensions/shape，编译器在创建 Delay 调用计划时一次写入。Plan 不持有 Document/Dimension 指针，避免移动副本和寿命问题。
- Delay 热路径直接使用 Plan 布局；初始化与稳态分开，当前元素只检查当前槽，initial/order 只在初始化时创建，稳态只建立 input/time。
- ArrayElementView 仍负责通用输入校验和不同布局映射；dimensions/shape 与目标完全一致时直接读取，不重复查询已由目标布局保证的输入 Dimension。

### 不变量与迁移边界

- N 仅在首次初始化截断并固定，初值/阶数表达式求值次数不增加；数组元素状态、Fixed 管道指针、离散输出、Material 时间历史和连续 RK 阶段保持原语义。
- 标量路径不变；整体求值仍可检查整组初始化，Apply-to-All 当前元素不再做全组扫描。
- 不为布局缓存引入跨 Run 指针或共享可变状态，不改 Document::findDimension 的名称规范化规则，不扩展普通数组统一循环。

### 验收

1. 现有 SDXorg 三组参考、数组不同 N/Fixed、条件未触达和 RK4 收敛通过。
2. 增加不同维度顺序的 Delay 数组测试，验证重排输入和初始输出不被错误平铺。
3. 比较相同 profile-delay 负载的总耗时、每元素步、checksum 和分配；插桩确认目标 Dimension 查询减少。
4. Release 后 Debug 全量与完整 XMILE corpus，完成后等待审核，不自动提交。

### 实施结果

- DelayPlan 以值持有 dimensions/shape，编译器一次写入，未引入 Document/Dimension 指针或跨 Run 缓存。
- 当前元素只检查当前初始化槽；initial/order 使用 optional 视图，仅初始化时创建；minimumTime 也只在初始化阶数时计算。
- ArrayElementView 对相同布局直接返回，不再逐视图查询输入 Dimension；不同布局仍执行完整校验和映射。
- 新增 `[A,B]→[B,A]` 的 Delay Euler/RK4 重排测试。测试初次期望算式写错，按实际映射修正，生产逻辑未为此改变。
- 两轮 Material 数组耗时下降 46.6%，Information 下降 51.3%；每元素步分别约 576.0→307.5 ns、525.5→255.8 ns，checksum 不变。
- gprof 的 `Document::findDimension` 从约 641 万次降至 810 次，确认目标冗余消除。Release 与 Debug 的 18/18 CTest 和完整 XMILE corpus 通过，阶段提交为 `689e91a`，未合并、未推送。
