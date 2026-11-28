# 纯时间输入函数设计

## 1. 范围与参考

本阶段实现 STEP、PULSE、PULSE TRAIN、RAMP 的标量/数组求值、Euler/RK4 语义、测试和 profile，不涉及专用 UI、Data、Lookup 或状态/延迟函数。

参考 Vensim 文档：[STEP](https://www.vensim.com/documentation/fn_step.html)、[PULSE](https://www.vensim.com/documentation/fn_pulse.html)、[PULSE TRAIN](https://www.vensim.com/documentation/fn_pulse_train.html)、[RAMP](https://www.vensim.com/documentation/fn_ramp.html)，以及 SDXorg `tests/input_functions/test_inputs.mdl` 和 `output.csv`。

## 2. 测试输入时间

EvaluationContext 增加单次外层积分区间的 `inputTime` 与 `inputDt`：

- 普通 TIME 继续使用当前阶段 time；
- STEP/PULSE/PULSE TRAIN 使用 `timePlus = inputTime + inputDt/2`；
- RAMP 使用冻结的 inputTime；
- Euler 在每个区间设置当前时间和实际 DT；
- RK4 各阶段保留相同 inputTime/inputDt，普通 TIME 仍按阶段推进；
- 初始化使用初始时间和 Plan DT；实际短步使用短步长，不使用过期 DT。

## 3. 函数规则

```text
STEP(height, start):
    timePlus > start ? height : 0

PULSE(start, width):
    width == 0 时按当前 inputDt
    timePlus > start && timePlus < start + width ? 1 : 0

PULSE TRAIN(start, width, repeat, final):
    width <= inputDt 时按一个 inputDt 的宽度
    inputTimePlus <= start 或 > final 时为 0
    width >= repeat 时有效区间内为 1
    否则按 (timePlus-start) mod repeat 的相位，在相位 < width 时为 1
    repeat <= 0 时只保留首个脉冲，不做除零

RAMP(slope, start, end):
    inputTime <= start: 0
    inputTime < end: slope * (inputTime-start)
    否则: slope * (end-start)
```

这些规则保留 Vensim 的严格比较和半步容差。PULSE/PULSE TRAIN 的短脉冲在固定步长下只占一个区间，不能因 RK 阶段而被提前或重复触发。非有限参数继续沿用现有求值/仿真失败规则。

## 4. 数组与执行顺序

- 参数可以是标量或数组；任一参数为数组时，使用首个数组参数的维度作为输出布局，其余参数沿用 Evaluator 的 broadcast 规则广播；
- 输出逐元素计算，不把首元素当成整个数组；
- 函数保持无状态，不需要历史槽；
- 编译器接受且仅接受 STEP(2)、PULSE(2)、PULSE TRAIN(4)、RAMP(3) 的对应参数个数；
- 既有依赖分析、拓扑顺序和公式诊断不变。

## 5. 验证与 profile

- 各函数的起点前、起点、区间内、终点、区间后和负斜率；
- 零宽/短脉冲、脉冲重复、repeat<=0、width>=repeat、final 边界；
- RK4 各阶段不提前触发，TIME 仍使用阶段时间；
- 数组参数广播及不同维度诊断；
- SDXorg input_functions 使用 Vensim output.csv 做全部变量逐时刻比对；
- SimulationProfile 增加固定四函数组合负载，输出总耗时、每求值耗时、样本数和 checksum。
