# 随机函数与 Run 随机流实施计划

创建时间：2026-11-28 18:21:57 +0800

## 目标与范围

实现当前已登记签名的 `RANDOM`、`NORMAL`、`LOGNORMAL`、`POISSON`、`EXPRND`，提供 Run 内独立随机流、固定种子复现、非零 Stream ID、标量/数组和 Euler/RK4 求值。不实现其它 RANDOM 分布、Data、延迟/状态宏或实验 UI。

权威设计见 `docs/design/random-functions.md`。参考 Vensim 官方 `RANDOM Functions`、`NOISE SEED`、`NOISE RNG`、`Stream ID`；本地摘录保存在 `docs/references/vensim/`。

## 语义边界

- 每次实际调用都取样：初始化、各时间步、Euler/RK4 各阶段、宏实际调用和数组元素按既定执行顺序独立取值。
- 同一 Document、设置、执行顺序和种子产生完全一致的序列；同方法复现不要求不同积分方法序列相同。
- `NOISE SEED` 缺省为 4487556；首阶段只接受数值字面量常量，初始化前设置默认流。动态公式明确拒绝。
- Stream ID 0 使用默认流；非零值使用独立流。ID 必须是有限、非负整数且不超过 2^31-1。
- 使用 C++ `std::mt19937`，保证本实现固定种子复现和统计分布，不承诺与 Vensim SIMD MT 位级序列一致。`NOISE RNG` 不支持，出现时明确拒绝，不能静默假装选择引擎。
- 所有输入有限；非法参数/Stream ID 明确失败，不以 NaN 静默继续。

## 步骤

1. 将 timeFunction 的通用映射逻辑复用到随机函数，保留固定 slot、数组广播和参数单次求值。
2. 在 EvaluationContext 增加 Run 内 RandomStreamState，按默认/非零 Stream ID 惰性创建独立 MT 流。
3. 实现五种分布及参数检查、NOISE SEED 字面量初始化和不支持 NOISE RNG 诊断。
4. 完成固定种子、不同种子、独立流、数组、宏、Euler/RK4、参数错误及分布统计测试。
5. 增加 SimulationProfile 随机组合场景，审查运行耗时、分配和随机流数量；执行 Release 后 Debug 全量及完整 XMILE corpus。
6. 更新设计、profile 和实际过程。若参考语义未证实，不宣称位级 Vensim 兼容。

## 实施记录

- 实现 Run 内 `RandomStreamState`，默认/非零 Stream ID 惰性创建 `std::mt19937`；NOISE SEED 在编译期验证并保存到 Plan，NOISE RNG 明确拒绝。
- 五种函数复用 mappedFunction 的连续 slot 和数组广播，参数单次求值，每次实际调用独立取样。LOGNORMAL 按输出均值/标准差换算对数空间参数。
- 新增固定种子、不同种子、独立流、数组、宏、Euler/RK4、非法参数及统计测试。C++ KS 按唯一值比较经验/理论 CDF，离散 Poisson 使用 `F(x-)`，避免把概率质量点的跳跃误报为偏差。
- 保留正式工具 `tests/profile/RandomSamples.cpp` 和 `tests/profile/check_random_samples.py`。NumPy `RandomState` 的 16 个原始 MT uint32 与 C++ 完全一致；连续分布的 SciPy/自研 KS 完全一致，Poisson grouped_D=0.003413、卡方 p=0.35731。
- SciPy 直接用于离散变量的连续型 kstest 会产生不适用的偏差和 p 值，未据此误判实现；Poisson 仅用 SciPy CDF 校准及卡方检验。
- 普通 Release 五函数组合为 1489.1 ms/1000 Run、500 万次取样，0.297819 μs/次，checksum 18922.6；编译 6.3133 μs/次。插桩未见异常截断循环或随机流重建热点。
- Release、Debug 全量 CTest 均为 18/18，包含完整 XMILE corpus。详细结果见 `docs/profile/random-functions.md`。

## 验收

- 相同种子逐点一致，不同种子/独立流按设计变化；数组和宏每次实际调用均取样。
- 分布参数、范围、统计均值/离散程度和错误路径通过测试。
- 普通 Release profile 与 Release/Debug 全量通过，无全局随机状态或跨 Run 污染。
