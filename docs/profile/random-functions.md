# 随机函数与统计验证

## 可重复工具

在源码根目录执行：

```bash
<RELEASE_BUILD_DIR>/tests/profile/random-samples.exe 20000 > random-samples.csv
python tests/profile/check_random_samples.py random-samples.csv
<RELEASE_BUILD_DIR>/tests/profile/profile-simulation.exe 1000
```

- `tests/profile/RandomSamples.cpp` 使用真实 Document/Plan/Runner 输出五种函数的 20000 个样本及 16 个原始 MT uint32。
- `tests/profile/check_random_samples.py` 使用 NumPy `RandomState` 对照原始 MT，以 SciPy CDF/KS 验证连续分布，并以离散 CDF 左右极限和卡方检验验证 Poisson。
- `scipy.stats.kstest` 的连续型公式不直接用于 Poisson；不将其离散误用结果作为失败依据。
- Python 依赖：NumPy、SciPy。Qt Test 不依赖 Python，C++ 测试另有独立 CDF/KS 实现。

## 独立校验结果

20000 样本、默认 NOISE SEED=4487556：

```text
raw_mt19937: 16 identical
uniform: scipy_D=0.00493082182184 grouped_D=0.00493082182184 p=0.713734
normal_output: scipy_D=0.00478479881286 grouped_D=0.00478479881286 p=0.747727
lognormal_output: scipy_D=0.00410383620086 grouped_D=0.00410383620086 p=0.887738
poisson_output: grouped_D=0.00341324452377 chi_square_p=0.35731
exponential_output: scipy_D=0.00401320660886 grouped_D=0.00401320660886 p=0.902792
```

连续分布的 SciPy 与自研 KS 数值完全一致；Poisson 的 CDF 使用 `scipy.stats.poisson.cdf` 校准，离散统计量采用唯一值分组及 `F(x-)`，卡方检验 p=0.35731。NumPy 的 `MT19937(seed)` 与 `RandomState(seed)` 使用不同种子展开；本项目原始 MT 对照使用后者，与 `std::mt19937` 一致。

## 执行 profile

普通 Release `profile-simulation 1000`：

| 操作 | 数量 | 总耗时 | 归一化耗时 | checksum |
| --- | ---: | ---: | ---: | ---: |
| 随机函数编译 | 1000 | 6.3133 ms | 6.3133 μs/次 | — |
| 五函数组合运行 | 1000 | 1489.1 ms | 1489.1 μs/Run | 18922.6 |
| 五函数实际取样 | 5,000,000 | 1489.1 ms | 0.297819 μs/次 | 每 Run 18.9226 |

同种子重复 Run 的 checksum 完全一致。插桩中，标准分布算法、C++ 数学函数和通用 Evaluator 是主要可见成本，没有异常截断循环或逐 Run 之外的随机流重建热点。`RandomStreamState::stream` 的调用归属存在模板/内联噪声，不据此推断每次取样都创建新流。绝对耗时以普通 Release 为准。

验证同时覆盖固定种子、不同种子、独立/共享 Stream ID、数组逐元素、宏实际调用、Euler/RK4、非法参数、动态 NOISE SEED 和不支持 NOISE RNG。Release、Debug 全量 CTest 均为 18/18，包含完整 XMILE corpus。
