# N 阶延迟与信息平滑设计

## 1. 范围

统一实现 `DELAY N`、`SMOOTH N` 及当前已登记的 `DELAY`、`DELAY1`、`DELAY3`、`SMOOTH`、`SMTH1`、`SMTH3`。支持标量/数组、可变 delayTime、初始化阶数、可选初值和 Euler/RK4。其它离散延迟、INIT/PREVIOUS、外部 Data 和带状态宏另行实现。

参考：[DELAY N](http://vensim.com/documentation/fn_delay_n.html)、[SMOOTH N](http://vensim.com/documentation/fn_smooth_n.html)、[DELAY1](https://www.vensim.com/documentation/fn_delay1.html)、[SMOOTH](https://vensim.com/documentation/fn_smooth.html)、[Material and Information Delays](https://www.vensim.com/documentation/mgu09_material_and_information_delays.html)。本地摘录保存在 `docs/references/vensim/delay-functions.md`。

## 2. 阶数与别名

- N 在首次初始化时求值并固定为整数；不是每步重新赋值的动态阶数。`Order Variable` 可改变其公式值，但不改变已初始化阶数。
- 阶数必须至少为 1；小数按截断取整。若 `N*DT > delayTime`，按 Vensim/PySD 规则逐步降低有效阶数；无法得到有效正整数时明确失败，不伪造状态。
- `DELAY N`/`SMOOTH N` 使用第四参数初始化阶数。
- `DELAY1`/`DELAY1I` 为物料一阶；`DELAY3`/`DELAY3I` 为物料三阶；`SMOOTH`/`SMOOTHI`/`SMTH1` 为信息一阶；`SMOOTH3`/`SMOOTH3I`/`SMTH3` 为信息三阶。
- XMILE `DELAY` 是 DelayFixed 管道延迟，不是 DELAY1；其采样语义见下文。
- `DELAY N`/`SMOOTH N` 必须直接位于所属公式右侧，不能把该离散状态函数隐藏在任意子表达式中；其它一阶/三阶别名不受此限制。

## 3. 级联状态

每个调用点和数组元素都有 N 个 Level。总平均延迟为 T，每级导数含 `order/T`：

```text
L1' = order * (input - L1/T)
Li' = order * (L(i-1)/T - Li/T)
output = LN/T
```

初始化每级为 `input*T`；带 initial 时为 `initial*T`。

信息平滑：

```text
Y1' = order * (input - Y1)/T
Yi' = order * (Y(i-1) - Yi)/T
output = YN
```

初始化每级为 input 或 initial。

因此当前状态内存为 O(N)，不是 O(N²)。只有需要完整输出历史时才另外保存输出序列。

## 4. 可变 delayTime 与离散输出

- `DELAY N`/`SMOOTH N` 是离散延迟：每个 Time Step 内输出保持，不在 RK 阶段重新计算。
- 物料 `DELAY N` 为每级保存 delayTime，并在每个实际步滚动历史；各级导数使用对应历史时间，输出为最后一级状态除以滚动后的 `times[0]`。
- `SMOOTH N` 的各级都使用当前平滑时间，不滚动 delayTime，输出直接为最后一级状态。
- `DELAY1`/`DELAY3`/`SMOOTH`/`SMTH1`/`SMTH3` 及 I 变体是连续一阶/三阶别名：使用当前阶段的 delayTime，不按离散输出缓存。
- 离散计划只推进一次实际 DT；连续计划参与 Euler/RK4 各阶段导数计算。Stock 的 RK4 与离散输出冻结可以共存。
- 速率计算区分实际步与 RK 中间阶段：Fixed 不创建微分速率，离散 N 只在实际步计算 K1，连续计划参与各阶段；Euler 只积分具有微分速率的计划。
- 未触达的调用保持原状态，不伪造输入或重新初始化。

XMILE `DELAY` 使用固定管道。初始化时将 delayTime/DT 四舍五入为步数，至少为 1、最多为 `DelayPlan::maximumOrder`；每个数组元素有独立阶数和管道指针：

```text
pipe[pointer] = input
pointer = (pointer + 1) % order
output = pipe[pointer]
```

输出在 Time Step 内缓存，因此与 SDXorg `delay_xmile` 的采样时序一致。

## 5. Plan 与 Run

- BoundCallNode 的 Delay 索引在编译期确定。DelayPlan 保存 Fixed/Material/Information、数组元素数、状态 base、固定/初始化阶数和是否离散输出；是否滚动 delayTime 由类型语义决定。
- Run 持有当前 Level、各级 delayTime、当前输入/时间、初始化标志、离散输出缓存和阶数。全部状态按 `base+elementIndex` 定位，无全局状态或跨 Run 污染。
- 通用 N 和 Fixed 按最大阶数预留状态；实际阶数与 Fixed 指针按数组元素独立保存，禁止不同元素共享阶数或管道位置。
- 可选 initial 和阶数表达式只在首次初始化时求值，避免每步重复读取或随机取样。
- Delay 与其它 mapped functions 共用 ArrayElementView：输入/时间只求值一次，相同布局直接寻址，不同布局按维度名映射，不为当前元素重复求值 AST 或创建完整广播数组。
- 首阶段带状态宏以 `unsupported-stateful-macro` 拒绝；宏调用路径状态身份另行设计。`FORCST`/`TREND` 等未实现状态函数仍在编译期以 `unsupported-simulation-function` 拒绝。
- 资源上限 `DelayPlan::maximumOrder=64` 是本项目保护措施，不是 Vensim 原生上限。数组元素数与状态槽计算执行溢出检查。

## 6. 验证与 profile

- 连续一阶/三阶别名使用解析解检查 Euler 值和 RK4 四阶收敛；通用 N 的离散输出与连续别名采样时序不同，不要求逐点相等。
- 可变 T 下物料/信息、离散/连续输出的差异正确。
- SDXorg `delays`、`smooth`、`delay_xmile` 全变量/时刻对齐，覆盖 Order Variable 初始化后不变和 delayTime 变化。
- 数组输入、逐元素不同阶数与 Fixed 管道、非法 T/N、条件未触达、宏拒绝、Plan 移动和固定随机种子复现。
- `profile-delay` 覆盖多阶物料/信息、Fixed、离散/连续 RK4 与数组，记录编译/运行耗时、状态数、checksum 和分配；插桩审查重复初始化、重复求值和不必要速率计算。
- Release 后 Debug 全量及完整 XMILE corpus。
