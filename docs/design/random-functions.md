# 随机函数与 Run 随机流设计

## 1. 范围

实现当前函数表已登记的 `RANDOM`、`NORMAL`、`LOGNORMAL`、`POISSON`、`EXPRND`，支持标量/数组、宏、Euler/RK4、固定种子和独立 Stream ID。其它 RANDOM 分布、NOISE RNG、Data、延迟/状态函数和蒙特卡洛实验界面另行实现。

参考：[RANDOM Functions](https://www.vensim.com/documentation/fn_random.html)、[NOISE SEED](https://www.vensim.com/documentation/noise_seed.html)、[NOISE RNG](https://www.vensim.com/documentation/noise_rng.html)、[Stream ID](https://www.vensim.com/documentation/stream_id.html)。本地摘录保存在 `docs/references/vensim/`。

## 2. 调用与复现语义

- Vensim 每次实际调用都会取新值。因此初始化、每个时间步、Euler/RK4 各阶段、每次宏调用和每个数组元素都独立取样，不按公式文本缓存结果。
- 求值顺序继续由 Plan 决定。相同 Document、设置、执行顺序和种子必须逐点一致；不同积分方法没有序列一致的承诺。
- 随机状态归 EvaluationContext/Run 所有，不进入 Plan、Document 或历史 Run 的共享状态。新 Run 总是重新初始化随机流。
- 使用 `std::mt19937`。目标是本实现可复现及正确分布，不宣称与 Vensim SIMD MT 的位级序列一致。

## 3. Stream ID 与 NOISE SEED

- Stream ID 0 使用默认流，默认种子为 4487556。
- 模型可定义 `NOISE SEED` 覆盖默认种子；首阶段只接受有限、非负、不超过 2^31-1 的整数数值字面量，在初始化求值前装配。动态公式报告 `invalid-noise-seed`。
- 非零 Stream ID 使用独立流并以该 ID 初始化。相同 ID 的调用共享流，添加其它同 ID 调用会改变后续序列；不同 ID 互不影响。
- ID 必须有限、非负、为整数且不超过 2^31-1。动态 ID 按当前求值值选择流，不为每个公式调用点另建全局状态。
- `NOISE RNG` 当前不支持，出现时报告 `unsupported-noise-rng`，不能静默忽略引擎选择。

## 4. 函数规则

| 函数 | 参数 | 规则 |
| --- | --- | --- |
| RANDOM(min,max[,stream]) | min < max | 在 (min,max) 内均匀取样，排除端点 |
| NORMAL(mean,stddev[,stream]) | stddev >= 0 | 正态分布；stddev=0 返回 mean |
| LOGNORMAL(mean,stddev[,stream]) | mean > 0，stddev >= 0 | 以输出均值/标准差换算对数空间参数；mean=0 且 stddev=0 时返回 0 |
| POISSON(mean[,stream]) | mean >= 0 | Poisson 整数分布；mean=0 返回 0 |
| EXPRND(mean[,stream]) | mean > 0 | 均值为 mean 的指数分布 |

LOGNORMAL 使用输出均值 m 和标准差 s 计算：

```text
sigma² = ln(1 + (s/m)²)
mu = ln(m) - sigma²/2
value = exp(N(mu,sqrt(sigma²)))
```

所有输入必须有限；其它非法参数返回明确求值失败。除 RANDOM 自身区间外，这些签名不涉及 Vensim 的通用拉伸/截断参数。

## 5. 执行结构

- 通用映射求值复用 timeFunction 的连续 slot、固定数值存储和数组广播，参数从左到右各求值一次。
- RandomStreamState 以 `QHash<StreamId,std::mt19937>` 保存当前 Run 的流，首次使用时按默认种子或 Stream ID 创建。
- 标量每次调用取一个样本；数组按输出 flat 顺序逐元素取样；宏展开不发生时，每次实际宏调用独立取样。
- 不支持或非法输入在求值时失败，不伪造 0 或 NaN 继续运行。

## 6. 验证与 profile

- 相同种子逐点一致，不同 NOISE SEED 序列不同，非零 Stream ID 独立，同 ID 共享。
- 数值字面量 NOISE SEED、非法/动态 seed 和 NOISE RNG 诊断。
- 五种函数的边界、非法参数、标量/数组、宏、Euler/RK4、取消和失败前缀。
- 大样本范围、均值和标准差采用固定种子的宽松统计容差，不用单次随机值判断分布。
- SimulationProfile 增加五函数组合负载，记录总耗时、归一化耗时、样本数、checksum 和随机流数量；检查没有全局状态或逐调用流对象分配。
- Release 后 Debug 全量回归及完整 XMILE corpus。
