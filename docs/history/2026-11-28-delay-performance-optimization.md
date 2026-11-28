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

## 阶段四：按证据决定 RK 阶段状态拆分

只有前述阶段后仍有明确热点时，才拆分 Run 稳定状态、连续阶段可变状态和离散输出缓存。数组槽/Arena 需要 shape、别名、宏寿命和结果移出分析，不属于当前 KISS 首选，不在证据不足时实施。

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
