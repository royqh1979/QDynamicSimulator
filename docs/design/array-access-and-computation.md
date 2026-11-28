# Array 访问与计算设计讨论

## 1. 讨论范围

本文说明当前 Evaluator、SimulationPlan、ExecutionStack 和 SimulationRun 中 Array 的访问与计算方式，并解释 ArrayValue 堆句柄和 ArrayElementView 分别解决什么问题。权威实现约定同时见 `formula-analysis.md`、`simulation.md`、`time-functions.md` 和 `delay-functions.md`；性能对比见 `docs/profile/array-values.md`。

Array 不是一种单一操作。至少要区分：

1. 数据如何布局与持有；
2. 如何读取整个数组、单个元素、切片或重排视图；
3. 如何进行逐元素、归约、状态积分和历史保存；
4. 哪些操作只复制句柄，哪些必须创建新 ArrayData。

## 2. 数据布局与持有

### 2.1 逻辑布局

ArrayData 包含有序 dimensions、shape 和行主序 values。维度长度必须等于 shape 长度，values 元素数等于各维长度乘积。

示例：

```text
dimensions = [Location, N]
shape      = [3, 2]
values     = [1, 2, 3, 4, 5, 6]
```

逻辑表格为：

| Location | N1 | N2 |
| --- | ---: | ---: |
| Boston | 1 | 2 |
| Chicago | 3 | 4 |
| LA | 5 | 6 |

行主序 flat 计算：

```text
flat = locationIndex * 2 + nIndex
LA/N1 的 flat = 2 * 2 + 0 = 4，值为 5
```

### 2.2 ArrayValue 是值语义的共享句柄

ArrayValue 内部共享 ArrayData：

- const 读取只复制句柄，不复制三组 QVector 元数据；
- ExecutionStack、宏参数和公式结果传递句柄；
- ArrayData 类型允许可变，但函数参数只读，新结果独立创建；
- 非 const `values()` 在句柄不唯一时克隆，保持值语义；
- 发布到历史 Run 后不再写入。

旧表示同样需要为 dimensions、shape、values 的 QVector 数据分配空间，只是 ArrayValue 直接位于 Value 内，不需要独立的 ArrayData 对象。新表示在这些 QVector 数据分配之外，增加 ArrayData/共享控制对象的分配；`make_shared` 通常可将 ArrayData 与控制块合并分配。所谓“新增分配”指后者，不表示旧数组没有堆内存成本。

因此下面两种操作成本不同：

```text
Identity(A)       只复制 ArrayValue 句柄，不创建 ArrayData
A * 2             创建独立结果 ArrayData 和输出 values
```

从 Run 取得数组副本后，调用方可以修改自己的副本，但不会反写 Run。共享对象不意味着所有句柄必须被同一次修改影响。

## 3. 求值的两种范围

### 3.1 整体求值

一次公式调用产生完整 ArrayValue，适用于直接数组运算、切片、转置和归约：

```text
A + B
A[1, *]
A'
SUM(A)
```

此时 `scope.indices` 为空，输出自身携带 dimensions/shape。

### 3.2 当前元素求值

Simulation 对 Apply-to-All 逐元素调用同一公式，scope 包含输出 dimensions、当前 indices、self 和 elementIndex：

```text
输入数组公式：N + STEP(1, 0.25)
N = [1, 2, 3] 时，三个元素分别求值
```

Executor 负责遍历，不在 Evaluator 内隐藏一套全局循环状态。显式 ArrayElementFormula 使用公式自身保存的固定下标。

整体 ArrayValue 也可以出现在当前元素公式中，Executor 会按当前 indices 提取对应标量；不能把整组值误赋给当前元素。

## 4. 访问类别

### 4.1 整个变量读取

```text
A
SELF
```

读取变量的完整 Value。数组只复制共享句柄，不做数值复制。`SELF` 使用当前变量的 nameKey，与普通变量共用读取入口。

用途：身份表达式、宏参数、数组归约、切片和广播输入。

### 4.2 标量读取与标量广播

```text
A * 2
STEP(1, start)
```

标量本身不需要 ArrayData。只有真正生成完整数组结果时，才把标量扩展为输出元素；ArrayElementView 对标量直接返回同一个 double，不创建广播数组。

### 4.3 相同布局的直接元素访问

当输入 dimensions/shape 与目标完全一致时：

```text
flat = 目标 flat
value = input.values[flat]
```

ArrayElementView 不创建维度映射或 strides。已有坐标时也按行主序直接计算：

```text
flat = 0
for each dimension i:
    flat = flat * shape[i] + coordinates[i]
```

这是 Delay 和 mapped functions 的热路径。

### 4.4 不同维度顺序的重排访问

设 B 为：

```text
dimensions = [N, Location]
shape      = [2, 3]
values     = [10, 30, 50, 20, 40, 60]
```

计算 `A + B` 时，输出使用 A 的 `[Location, N]` 顺序。B 的 N/Location 顺序不能直接按 flat 相加，需要维度名映射：

```text
输出 Boston/N1 -> B[0]  = 10
输出 Boston/N2 -> B[3]  = 20
输出 Chicago/N1 -> B[1] = 30
...
```

结果：

```text
A + B = [11, 22, 33, 44, 55, 66]
```

维度集合不同或形状不兼容时报错，不按照位置猜测，也不隐式引入 sub-range。

### 4.5 固定标量下标

普通下标从 1 开始：

```text
A[1, 1]              -> 1
A[Boston, 2]         -> 2
A[Location.Chicago, 1] -> 3
```

非整数、非有限值或越界下标返回 0。全部维度均为单值时结果是 double，不创建 ArrayData。

### 4.6 当前维度与 `@n` 下标

```text
A[Location, N]
A[@1, @2]
```

`Location/N` 使用当前元素在对应维度上的下标；`@n` 表示当前输出方程的第 n 个维度。整体求值时它们相当于沿该维通配，当前元素求值时转为单值。

例如当前为 LA/N2：

```text
A[Location, N] -> 6
A[@1, @2]      -> 6
```

### 4.7 通配、区间与切片

```text
A[1, *]     -> dimensions=[N], values=[1, 2]
A[2:3, 1]   -> dimensions=[Location], values=[3, 5]
A[1]        -> dimensions=[N], values=[1, 2]
```

不足的下标按尾部通配处理。切片保留被选择的维度，创建独立 ArrayData；它不是原数组的零成本 View，因为结果需要独立寿命并可能改变 shape。

### 4.8 转置

```text
A'
```

转置同时反转 dimensions/shape 并重排 values：

```text
dimensions = [N, Location]
shape      = [2, 3]
values     = [1, 3, 5, 2, 4, 6]
```

不能只修改显示名称而不移动数据。转置结果需要独立 ArrayData。

### 4.9 宏参数与 ExecutionStack 槽位

```text
Identity24(Values) + 1
Values = [1, 2, 3]
结果   = [2, 3, 4]
```

每个实参只在调用方求值一次，再以 Value 句柄写入连续 slot。形参多次出现不会重复读取变量或执行实参表达式。嵌套/递归宏使用不同 base index，返回时恢复栈顶。

数组句柄保证 ExecutionStack 扩容只移动句柄，不影响 ArrayData 地址。新结果不能通过修改共享输入生成。

### 4.10 mapped function 的逐元素参数访问

```text
height = [2, 3]
start  = [0, 1]
STEP(height, start)
```

在 t=0 和 t=1 的输出分别为：

```text
t=0 -> [2, 0]
t=1 -> [2, 3]
```

STEP、PULSE、PULSE TRAIN、RAMP 和随机分布函数通过统一 ArrayElementView 逐元素读取参数。标量参数直接读取，相同布局直接寻址，不同布局使用重排映射，不为每个元素生成完整广播数组。

随机函数逐元素调用分布函数，不复制首元素；同一 Stream 按实际调用顺序取样。

### 4.11 Delay 的当前元素与内部状态访问

```text
input(t) = N + STEP(1, 0.25)，初始值为 N=[1,2,3]
DELAY N(input, 4, N, N)
```

每个元素有独立阶数、Level、delayTime、初始化标志和离散输出缓存。N 在首次初始化时截断并固定，不随后续公式值改变。

在 DT=0.25 时，初始输出和 t=0.25 均为 `[1,2,3]`，t=0.5 为：

```text
[1.0625, 2, 3]
```

Delay 的参数只求值一次，ArrayElementView 选择当前元素；内部状态槽为：

```text
base + level * elementCount + elementIndex
```

阶数与 Fixed 指针使用每元素独立槽位，不能用一个 Plan 级阶数或指针覆盖整个数组。

### 4.12 Fixed 管道访问

XMILE `DELAY` 的每个元素拥有独立管道长度和 pointer：

```text
pipe[pointer] = input
pointer = (pointer + 1) % order
output = pipe[pointer]
```

例如延迟时间分别为 `[1,2,3]`、DT=0.25 时，管道步数分别为 `[4,8,12]`，各元素首次输出新输入的采样时刻不同。

### 4.13 Stock/Flow 净速率访问

数组 Stock 的每个元素独立保存状态，Flow 累加到对应 Stock 元素：

- 相同 ArrayLayout 使用直接 flat；
- 不同维度顺序使用 Plan 预计算 mapping；
- 标量 Flow 累加到 Stock 全部元素；
- 不兼容布局以 `incompatible-stock-flow` 失败。

Plan 的映射只在编译期创建一次，不在每元素步重新计算维度映射。

### 4.14 Run 历史采样访问

```cpp
QVector<ArrayValue> samples = run.arraySeries("output");
```

每个采样点保存一个 ArrayValue 句柄。发布后不再修改；调用方取得副本后若进行非 const 修改，按值语义克隆，不影响历史 Run。

未知 nameKey、类型不符或零样本返回空 QVector；变量是否存在由 RunVariableInfo 元数据判断。数组结果不能把首元素伪装成标量曲线。

## 5. 计算类别

### 5.1 逐元素算术、数学和逻辑

```text
A + B
SQRT(A)
MAX(A, 2)
A > 3
```

输出元素数与输入相同，必须创建结果 ArrayData。标量条件保持短路；数组 IF/AND/OR 逐元素计算，不能沿用标量短路。

```text
IF A > 3 THEN A ELSE 0
A = [1,2,3,4,5,6] -> [0,0,0,4,5,6]
```

### 5.2 广播与重排计算

广播解决标量到数组、数组维度顺序到输出顺序的问题。需要区分：

- 输入访问：可以用 ArrayElementView 按需读取，不必生成广播临时数组；
- 输出计算：完整数组结果仍需要 ArrayData。

“避免完整广播”不等于“所有数组运算都不分配”。

阶段一与阶段二的语义规则相同：标量可对数组广播，数组按维度名称严格对齐。阶段一的 `broadcast()` 会为标量或重排输入创建完整目标数组；阶段二仍保留该函数供普通算术、条件等路径使用，因此 `profile-array` 的 `A + 1` 仍会创建标量临时数组和输出数组，并新增 ArrayData 分配。只有 Delay 与 mapped functions 改用 ArrayElementView，按需读取输入而跳过完整输入广播。当前不能把普通 `A + 1` 的回退归因于广播规则改变，也不能声称所有运算都已改为惰性视图。

回退归因也不能只归咎于 ArrayData。实际 values 分配包含固定分配与 N 个 double 初始化，ArrayData 主要增加固定对象/控制块成本；输出循环中的非 const `values()` 还逐元素检查句柄唯一性并增加间接访问。对 4 元素广播的一次性插桩显示，reserve 和四次 push 前的 use_count 均为 1，未发生 ArrayData 克隆；这里的额外成本是重复检查而不是克隆。小数组高频操作时固定成本更明显，大数组通常由 values 初始化与内存访问主导。后续分步实验证明重复唯一性检查仅约 1% 影响；标量临时 values 是两种实现共有的冗余。操作级 profile 显示 ArrayData 创建/完整发布约 30–34 ns，与稳定公式差距同量级，但直接/variant 共享句柄复制反而快约 18–20 ns，会抵消一部分成本。早期较大的公式差距无法稳定复现，不作为最终归因；最终 300000 次迭代四轮正反向对照中，A+1 新句柄平均慢 4.48%/3.51%（16/4），A+A 快 1.70%/1.75%，因此不能把新表示描述为所有数组计算的一致回退。

### 5.3 切片与转置计算

切片和转置都改变布局或元素集合，通常需要 O(输出元素数) 的数据重排和独立结果。相同布局读取是 O(1)，不代表这些布局转换也是 O(1)。

### 5.4 归约

```text
SUM(A)    -> 21
MEAN(A)   -> 3.5
MAX(A)    -> 6
SIZE(A)   -> 6
STDDEV(A) -> 总体标准差
```

归约读取整个数组或切片，返回 double，不创建输出 ArrayData。当前元素下的切片归约只读取对应切片：

```text
当前 LA/N2：SUM(A[Location, *]) -> 11
当前 Chicago/N1：SUM(A[Location, *]) -> 7
```

### 5.5 排序与 RANK

RANK 按行主序位置返回排名，可使用次级排序数组。次级数组先按维度名对齐；它不是逐元素算术，也不产生同形输出数组。

### 5.6 数组初始化列表

```text
1, 2, 3, 4, 5, 6
```

在 `[Location,N]`、shape `[3,2]` 下生成上述 A。列表只在公式根节点解释，必须有 scope.dimensions，元素数必须精确匹配；当前元素求值可以只返回对应标量。

### 5.7 无状态 mapped 计算

时间和随机函数对每个元素执行相同函数映射，但参数和随机取样按元素独立。Euler 与 RK4 使用冻结的测试输入时间/DT，不因积分阶段重复推进测试输入。

### 5.8 连续状态的 Euler/RK4

DELAY1/3、SMOOTH/SMOOTH3 和数组 Stock 的每个元素独立积分：

- Euler 使用实际步 K1；
- 连续状态参与 RK4 各阶段；
- 每个阶段值是独立结果，不能修改共享输入；
- 非有限状态使运行失败。

### 5.9 离散状态推进

DELAY N/SMOOTH N 输出在一个 Time Step 内冻结，只使用 K1 推进一次。Fixed 只推进管道，不创建微分速率。离散-only 的 K2–K4 不需要计算。

### 5.10 宏计算

宏是运行期自定义函数，不是文本替换。标量和数组共用调用协议、默认参数和递归深度限制。参数按逻辑值语义绑定，数组底层可以共享，但新结果必须独立。

## 6. 访问方式与分配行为汇总

| 操作 | 示例 | 输入读取 | 新 ArrayData | 主要成本 |
| --- | --- | --- | --- | --- |
| 身份 | `A` | 整数组句柄 | 否 | 句柄复制 |
| 宏参数 | `Identity16(A)` | 栈槽句柄 | 否 | 帧与句柄复制 |
| 标量下标 | `A[1,2]` | O(1) 元素 | 否 | 下标解析 |
| 同布局映射 | `STEP(A,0)` | 直接 flat | 是 | 输出逐元素计算 |
| 重排映射 | `STEP(B,0)` | 维度名映射 | 是 | 映射与输出计算 |
| 切片 | `A[1,*]` | 选择元素 | 是 | 输出复制 |
| 转置 | `A'` | 重排元素 | 是 | 输出重排 |
| 归约 | `SUM(A)` | 全部/切片 | 否 | 数值遍历 |
| Delay 当前元素 | `DELAY N(A,T,I,N)` | ArrayElementView | 通常仅变量输出 | 状态级联与输出 |
| Run 查询 | `arraySeries` | 历史句柄 | 查询时不复制数据 | 容器/句柄复制 |

## 7. 错误与边界

- 标量下标非法返回 0，不隐式越界读取。
- 数组维度集合、形状或元素数不兼容时求值失败。
- 数组初始化列表元素数不匹配时报错。
- Stock/Flow 不兼容以 `incompatible-stock-flow` 失败。
- 非有限仿真值不被替换为 0。
- Delay 非法时间/阶数、超过 `DelayPlan::maximumOrder` 或状态槽溢出明确失败。
- 未触达的条件延迟调用保持原状态，不伪造输入或推进管道。

## 8. 与 Profile 的对应关系

`profile-array` 的场景对应不同访问成本：

| 场景 | 覆盖内容 | 阶段二结果 |
| --- | --- | --- |
| array_identity | 整数组和句柄复制 | 快 18.2%/21.8%（16/4 元素） |
| array_macro_nested_16 | 栈槽与嵌套宏参数 | 快 8.3%/10.0% |
| array_subscript | 单元素访问 | 快 5.8%/8.8% |
| array_broadcast | 新数组发布 | 慢 10.6%/5.4% |

`profile-delay` 证明相同布局直接寻址和参数复用的收益大于当前 ArrayData 分配成本：数组 Material/Information 相对阶段一分别再快 36.1%/37.5%。

因此优化不能只看 ArrayValue 是否堆化，还要问具体操作属于哪一类：

1. 能否只复制句柄；
2. 能否复用已求值参数；
3. 能否直接 flat 读取；
4. 是否必须重排输入；
5. 是否必须创建输出；
6. 是否存在状态或历史寿命。

## 9. 热循环分类与拆分原则

### 9.1 分类不能只看维度重排

现有热循环至少由以下独立属性共同决定：

- 输入：标量、同布局数组、不同顺序数组、切片；
- 范围：整体数组求值、Apply-to-All 当前元素、当前切片；
- 运算：无错误 double 运算、可失败 mapped 运算、归约、排序、布局转换；
- 状态：无状态、连续级联、离散 N、Fixed 管道、Stock 状态；
- 积分：Euler 实际步、连续 RK 中间阶段、离散 K1；
- 输出：无数组结果、独立临时数组、Run 变量状态、历史采样序列；
- 布局来源：运行期通用求值、SimulationPlan 编译期固定布局。

因此“标量/同布局/重排”只是输入访问分类，不是完整的执行分类。

### 9.2 应保留的简单执行路径

| 类别 | 示例 | 合适的执行方式 | 不宜强行合并的原因 |
| --- | --- | --- | --- |
| 标量数组直接运算 | `A+1`、`2*A` | 单一线性循环，反复使用标量 | 不需要数组参数视图或坐标 |
| 同布局数组直接运算 | `A+A`、一元、数组条件 | 共享 flat 的线性循环 | 不需要 optional 和重排映射 |
| 重排数组运算 | `A+B` | 先建立/复用映射，再线性计算 | 真正需要维度映射 |
| 可失败 mapped 运算 | RANDOM、Lookup、非法参数 | 统一错误处理，直接/重排分别访问 | 与无错误 double 运算的结果包装不同 |
| 归约 | SUM/MEAN/MAX、切片 SUM | 直接累加；切片按映射访问 | 不产生输出数组，不能套用输出路径 |
| 排序 | RANK | 独立排序与次级对齐 | O(N log N)，不是普通逐元素循环 |
| 布局转换 | 切片、转置 | 独立布局/数据重排 | 结果 shape 或元素集合改变 |
| Apply-to-All | 当前 N、当前元素公式 | Executor 里程表推进 | 不应在内部重复整组遍历 |
| 连续 Delay/Stock | DELAY1/3、Euler/RK4 | 按实际阶数计算 K1–K4 | 需要阶段状态，离散规则不同 |
| 离散 N | DELAY N/SMOOTH N | 每实际步仅 K1，输出冻结 | 不需要中间阶段速率 |
| Fixed | XMILE DELAY | 每元素环形管道 | 没有微分速率，不应进入 delayRates |
| Stock/Flow | 标量 Flow、直接 Flow、重排 Flow | 标量广播、直接 flat、Plan mapping | 编译期映射与通用运行期映射成本不同 |
| Run 输出 | 标量/数组采样 | 分别追加序列/句柄 | 输出频率与结果分配是另一层成本 |

### 9.3 可以共享的内容

- 一元、二元和数组条件可共享直接访问策略，仅 operation 不同。
- 时间函数和随机函数可共享可失败 mapped 策略，但不必与普通 double 运算共享结果包装。
- SUM/MEAN/MAX 可共享归约遍历；STDDEV 的额外遍历应按算法处理，不为形式统一改变。
- 相同 ArrayLayout 的 Stock/Flow 可共享直接累加；不同布局使用 Plan 预计算映射。
- Delay 的 Material/Information 可共享槽定位，但时间历史和输出公式必须分别保持。
- 所有路径共享维度校验、错误语义、ArrayValue 所有权、输出发布和参考测试。

### 9.4 不应拆分的内容

- 不因一元、二元函数名不同就复制访问框架。
- 不为小数组/大数组预设另一套语义；是否按规模分派必须有测量依据。
- 不为每种数学函数、每种随机分布建立专用循环。
- 不为模板实例化而拆分；生成代码不同不构成新的语义类别。
- 不把 ArrayData 分配策略与元素访问策略混为一谈。

### 9.5 后续 profile 矩阵

后续优化至少覆盖：

1. 标量直接、同布局直接、重排数组；
2. 一元、二元、数组条件、时间/随机 mapped；
3. 整体求值与 Apply-to-All 当前元素；
4. 归约、切片、转置和嵌套宏；
5. Material/Information/Fixed 数组及连续/离散 RK；
6. Stock 标量 Flow、直接 Flow、重排 Flow；
7. 少采样点与全采样点输出；
8. 小数组、当前代表规模和更大数组，以及一维/多维重排。

只有某类别出现稳定热点时才拆分其实现。拆分后仍由统一校验和参考测试约束，不能形成平行语义。

## 10. 后续优化边界

- 优先复用输入访问和 Run 内工作区，不为只读访问创建临时数组。
- 相同布局必须保留直接路径，不能为统一抽象重新加入每元素映射分配。
- 不同布局使用通用映射，Stock/Flow 继续使用 Plan 预计算映射。
- 新数组发布分配是实测成本，后续可考虑工作区复用，但不能通过修改共享输入规避。
- Arena/编译期临时槽需要完整 shape、别名、宏寿命和结果移出分析，当前不作为 KISS 优化。
- 所有后续优化必须保持下标错误、条件求值、采样时点、Euler/RK4 和历史 Run 语义不变。
