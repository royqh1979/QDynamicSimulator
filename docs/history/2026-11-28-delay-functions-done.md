# N 阶延迟与信息平滑实施计划

创建时间：2026-11-28 18:56:27 +0800

## 目标与范围

统一实现 `DELAY N`、`SMOOTH N` 及 `DELAY`、`DELAY1`、`DELAY3`、`SMOOTH`、`SMTH1`、`SMTH3`，覆盖标量/数组、可变 delayTime、初始化阶数、可选初值和 Euler/RK4。建立 Plan 隐藏状态槽，不在 Evaluator 中增加静态状态。

首阶段拒绝宏体/默认参数中的状态调用，以 `unsupported-stateful-macro` 诊断。其它离散延迟、INIT/PREVIOUS、外部 Data 和带状态宏另行实现。

权威设计见 `docs/design/delay-functions.md`。参考 Vensim DELAY N/SMOOTH N、DELAY1/SMOOTH、PySD 与 SDXorg `delays`、`smooth`、`delay_xmile` 输出。

## 已确认的规则

- N 在首次初始化时截断为整数并固定，不是每步动态重赋值；`Order Variable` 后续变化不修改已初始化阶数。PySD 与 Vensim 参考输出已交叉验证。
- N 阶状态为 O(N)：每级导数含 `order/T`，物料输出 LN/T，信息输出 YN。
- 物料 LV 每级初值为 input*T 或 initial*T；信息每级为 input 或 initial。
- `DELAY N`/`SMOOTH N` 为离散输出，每个 Time Step 内保持，只推进一次实际 DT；物料 DELAY N 滚动各级 delayTime，SMOOTH N 各级使用当前 T，其它连续别名使用当前阶段 T。
- `maximumDelayOrder=64` 是项目资源保护，不是 Vensim 原生限制。

## 步骤

1. BoundCallNode 增加 Delay 类型和 DelayPlan 索引；编译确定 Material/Information、数组数量、离散/连续和初始化阶数，拒绝带状态宏及不正确的 DELAY N 位置。
2. Run 保存 N 级 Level、各级时间、输入、初始化状态和离散输出缓存，求值物料/信息输出。
3. 离散计划每步推进一次并滚动时间；连续计划按 Euler/RK4 各阶段导数推进，不污染历史输出。
4. 完成阶数/可变 T/初值/数组/错误/复现/条件未触达及宏拒绝测试。
5. 对齐 SDXorg delays、smooth、delay_xmile 全部变量/时刻；增加组合 profile 和热点审查。
6. 执行 Release 后 Debug 全量及完整 XMILE corpus，更新设计、profile 和实际过程。

## 验收

- 各阶别名与通用 N、可变 T 的物料/信息及离散/连续差异正确。
- 三组参考在各自积分设置下通过；数组状态独立，带状态宏明确拒绝。
- Release/Debug 全量通过，无全局状态、阶数误变或阶段结果污染。

## 实际过程与设计修正

1. 在 `feature/first-order-delays` 分支完成 Plan Delay 调用、Run 级联状态、Euler/RK4 和 MDL 多字函数名导入。`delaysModel` 与 `delayXmileModel` 首先通过。
2. Smooth 参考最初在 delayTime 从 2 变为 4 后偏差。根因是错误地让 Information 也滚动各级 delayTime；修正为只有 Material DELAY N 滚动时间，SMOOTH N 各级使用当前 T，随后 `smoothModel` 全时刻通过。
3. 数组初测全部得到最后一个元素。根因是外层 Apply-to-All 已逐元素求值，Delay 内层又改写当前下标并处理整组状态。恢复当前元素求值边界，并增加真实数组输入、逐元素不同 N 和 Fixed 不同管道长度测试。
4. 阶数和 Fixed 指针由每 Plan 一份改为每数组元素一份；未初始化/未触达元素不推进状态，Fixed 增加最大阶数检查，状态槽计算增加溢出检查。
5. Fixed 数组调试场景最初把期望输出提前一个采样点。依据已通过的 XMILE 管道时序修正测试期望，未改动生产语义；保留首变化时刻和不同元素管道长度断言。
6. RK4 单步 DELAY3 与解析解存在预期截断误差，未用放宽单步容差掩盖；改为步长减半序列并验证误差四阶下降。DELAY1/DELAY3 Euler 值及 RK4 收敛均通过。
7. 审核发现 Delay 类别会让 FORCST/TREND 误入已支持路径，改为统一 `supportedDelayName` 白名单，未实现函数继续在编译期拒绝。
8. 新增 `profile-delay`：Material/Information、离散 RK4、随机初值、Fixed、连续 RK4 和数组各有编译/运行分阶段、checksum、状态数与分配计数。正式负载为 100 次、1000 步、16 元素，两轮 checksum 一致。
9. 独立 `-pg` 插桩确认随机初值每次 Run 只求值一次；发现 Fixed 无效速率分配、离散 RK4 未使用的 K2–K4、数组重复输入/时间求值等候选。本轮只记录，不扩展为性能优化。数据与结论见 `docs/profile/delay-functions.md`。
10. 清理调试输出，保留数组、条件未触达、非法参数、宏拒绝和解析解回归场景。专用插桩构建及输出在验证后删除；标准 Release/Debug 构建保留。
11. 最终自我审核删除未使用的求值时序参数。最终 Release 后 Debug 均重新构建 `all-test-targets`，18/18 CTest 通过，包含完整 XMILE corpus。Release 编辑器焦点用例此前受外部键盘输入干扰出现失败，单独及后续完整套件均通过，未修改无关编辑器实现。
