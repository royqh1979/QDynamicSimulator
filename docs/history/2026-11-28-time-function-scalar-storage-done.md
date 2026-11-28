# 统一执行栈与时间函数参数存储性能重构计划

创建时间：2026-11-28 17:57:40 +0800

## 目标与范围

消除 STEP/PULSE/PULSE TRAIN/RAMP 求值中每次调用建立和增长 `QVector<Value>`、`QVector<double>` 的工作，并将 Macro 参数/局部变量与多参数函数统一到 Evaluator 持有的可复用执行栈。只修改求值参数存储、宏帧及操作签名，不改变函数算术、数组广播、参数求值顺序或错误语义。

`Value=std::variant<double,ArrayValue>` 继续表达标量/数组二态；本次消除的是不必要的容器与按名称帧分配，不是取消必要的值类型。

## 证据与基线

- 独立 `-O3 -g -pg` 调用图显示，timeFunction 的 self-time 和函数算术不是主要热点；通用输入容器的 append/realloc/detach、析构及 variant 处理构成明显的可避免工作。
- 当前实现即使全标量输入也建立 `QVector<Value> inputs`，再建立 `QVector<double> values`；inputs 未 reserve，存在重复增长。
- 时间函数参数上限为 4（STEP/PULSE=2、PULSE TRAIN=4、RAMP=3），无需堆上动态参数容器。
- 现有 `m_macroFrames` 每次调用建立 QHash，按 nameKey 保存局部变量；Plan 的 LocalReference 已明确局部语义，可在编译时进一步绑定为 slot 下标。
- 当前 master 普通 Release `profile-simulation 1000` 基线：时间函数 1095.08 ms/1000 次运行，1.09508 μs/步，checksum 6000；Lookup 64.8396 ms，Macro 330.572 ms，数组 Euler 9087.26 ms。

## 不变量

- 所有参数仍按从左到右顺序各求值一次，首个失败立即返回。
- 全标量直接求值；任一数组参数时仍使用首个数组维度，其余参数按原 broadcast 规则处理。
- 输出维度、样本时点、Euler/RK4 冻结 inputTime/inputDt、严格边界、非有限值和错误位置不变。
- 原始 AST 与 Plan Bound AST 共用相同 timeFunction 语义，不增加四个函数各自的特殊分支。
- 执行栈只保存当前同步求值链的参数与局部值；帧通过 base index 标识，vector 扩容不能使引用失效，返回时必须恢复调用前栈顶。
- ExecutionStack 直接由 EvaluationContext 持有；每次 SimulationRunner::run() 创建一个上下文，并以具名常量 `runValueStorageBytes=4 MiB` 预留 Value 存储，run 结束释放。公式/宏调用只复用 slot；普通无 Run 求值复用其所属上下文并按需增长，避免每个临时上下文都预留 4 MiB。
- Plan 的 LocalReference 按宏内形参位置绑定 slot；通用原始宏求值可通过帧的形参元数据按名称映射到同一 slot，不保留两套值存储。
- 递归宏仍受 64 帧限制，参数遮蔽、默认值引用前参、数组值和取消/失败行为不变。

## 步骤

1. 在 Evaluator 内建立可复用 ExecutionStack：连续 Value slots、帧 base index 和 RAII 恢复；多参数 timeFunction 使用同一 slot 区域，数组路径继续使用既有 broadcast。
2. 将 Plan LocalReference 编译为宏内 slot，BoundMacroCall 的参数和默认参数统一入栈；通用原始宏通过帧形参元数据映射到同一存储。
3. 运行现有标量/数组、宏遮蔽/递归、边界、Euler/RK4、MDL 参考结果及完整 XMILE corpus，确认 checksum 不变。
4. 以普通 Release 对比时间函数和 Macro 的总耗时、归一化耗时，同时复查 Euler/RK4、Lookup 和数组负载，保留全部轮次。
5. 以独立 `-O3 -g -pg` 构建复查 QVector<Value>/QVector<double>/QHash 帧分配是否从执行链消失，分析残余热点。
6. 执行 Release 后 Debug 的 all-test-targets 和完整 CTest；更新当前设计与 profile/history。若出现稳定回退，分析原因并回退或重新设计，不宣称优化。

## 实施记录

- 最终采用 EvaluationContext 持有的 ExecutionStack，未引入第二套短寿命栈。Run 以 `runValueStorageBytes=4 MiB` 预留 Value 存储；普通上下文按需增长。最大帧深度和时间函数参数上限均使用具名常量。
- timeFunction 的参数进入连续 Value slot，逐元素数值使用具名上限的 std::array；原始/绑定宏调用共用帧恢复和参数入栈逻辑。Plan LocalReference 保存 slot，运行时按 `frame.base+slot` 读取；通用原始宏仅通过帧元数据完成名称映射。
- 返回值继续由 EvaluationResult 按值返回，未为了统一形式扩大为栈内返回；profile 未显示返回方式是需要本轮处理的热点。
- 三轮普通 Release：时间函数为 675.27/721.867/702.793 ms，相对基线 1095.08 ms 分别减少约 38.3%/34.1%/35.8%；Simulation Macro 为 112.068/124.002/120.04 ms，相对基线 330.572 ms 分别减少约 66.1%/62.5%/63.7%。所有 checksum 不变。
- FormulaProfile 的通用 Macro 为 0.2541 ms（基线约 0.4653 ms），递归 Macro 为 0.87 ms（基线约 1.6165 ms），也呈明显下降。
- 独立插桩复查中，Value 存储 realloc 只剩 6001 次（每 Run 一次），不再出现原 timeFunction 的反复参数容器增长。Frame resize/restore 的 self-time 很低；函数算术和返回值仍未成为热点。绝对耗时仍以普通 Release 为准。
- Release 全量 CTest 18/18、Debug 全量 CTest 18/18 通过，包含完整 XMILE corpus、宏递归/遮蔽、数组、Euler/RK4 和 Worker 生命周期场景。

## 验收

- 所有数值、边界和持久化参考结果一致。
- timeFunction 不再逐调用建立堆上参数容器；Macro 参数/局部变量统一使用执行栈 slot，普通 Release 有可重复收益或无回退的明确结论。
- Release/Debug 全量通过，实际过程和最终对比记录完整。
