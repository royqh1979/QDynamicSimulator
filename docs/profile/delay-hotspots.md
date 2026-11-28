# Delay 分场景热点分析

## 方法与限制

在撤销 DelayWorkspace 后，以 `197d845` 的生产实现做独立 `-O3 -g -pg` 构建，分别运行：

```bash
profile-delay 200 1000 16 material_n_rk4
profile-delay 200 1000 16 continuous_delay3_rk4
profile-delay 100 1000 16 material_n_array
profile-delay 100 1000 16 information_n_array
```

`profile-delay` 的第四参数是场景过滤器；原有三参数形式保持不变。gprof 输出包含插桩自身 `_mcount_private/__fentry__`，标量场景约占一半，Material 数组约 37%，Information 数组约 52%。这些开销及小函数采样归属不能当成生产热点，不能把 inclusive 时间相加。

部分内联/相同代码函数的调用计数存在明显符号噪声，例如 `Document::createVariable` 不可能达到百万次。以下只采用与源码调用路径和规模能够相互验证的证据，不引用这些异常名称作结论。

## 标量连续与离散 RK4

两个标量场景的总插桩时间都只有约 0.26–0.27 秒，百分比采样噪声较大，不再据此宣称精确的 self-time 排名。

可见的主要工作不是数学除法或清零，而是：

- QHash detach/节点复制与析构；
- EvaluationResult、variant 和 QVector 元数据操作；
- DelayState 的 QVector<bool>/QVector<double> 复制和析构；
- EvaluationStack restore 和公式求值包装。

`delayRates` 在连续 DELAY3 中可见，但不是唯一大项。逐步创建速率结果与 RK 阶段状态/容器操作同时存在，仅减少速率分配不能覆盖主要成本。这支持撤销 DelayWorkspace，而不是继续优化 Vector 数量或清零。

## 数组 Delay 的直接热点

Material 数组中可见：

- `QVector<double>::detach`：约 9009 万次调用；这是写时复制检查，不等于实际深拷贝。
- `QVector<QString>` 复制：约 842 万次。
- `ArrayElementView` 构造/析构和 `Evaluator::delay/evaluate` 的可见采样。
- `delayRates` 自身也有采样，不能只处理视图而宣称全部解决。

Information 数组中，`Document::findDimension` 约 641 万次，与下面的调用规模精确对应：

```text
100 Run × 1000 步 × 16 元素 × 4 个参数视图 = 641 万次（含终点）
```

当前每个 Delay 元素都创建 input、time、initial、order 四个 ArrayElementView。每个视图即使与目标布局完全一致，也再次按输入维度调用 readDimension 做形状校验。目标 shape 已由 `shapeFor` 查询，因此同布局下这部分输入维度校验是重复工作。

其它可见成本包括：

- 视图持有 dimensions/shape 带来的 QVector 元数据复制和析构；
- detach/refcount 检查；
- ArrayData 创建与销毁；
- Runner 的数组输出和状态管理。

这些证据比 Vector 分配次数更直接地指向“每元素重复建立访问环境”。

## 代码审查确认的额外冗余

以下问题不能完全依赖 gprof 百分比，但可从当前源码和循环次数直接确认：

1. **初始化扫描按全部元素执行**  
   当前 Delay 在 Apply-to-All 的每个元素调用中，都遍历 `plan.count` 检查是否还有未初始化元素。16 元素时，稳态下每步仍执行约 16×16 次检查。当前元素求值只需要检查当前槽，整体求值才需要扫描整组。

2. **初值和阶数视图在稳态仍创建**  
   initial/order 表达式只在首次初始化使用，但四个视图每步每元素都会创建。初始化完成后只有 input/time 需要热路径访问。

3. **相同布局仍重复校验输入 Dimension**  
   目标 shape 已知且输入 dimensions/shape 完全相同，重复查询输入 Dimension 不会改变结果。应先识别同布局，再决定是否需要通用校验/映射，而不是每个参数重复查询。

## 重复准备消除后的验证

实施以下调用模型调整：

- DelayPlan 以值持有编译期 dimensions/shape，Delay 不再 shapeFor；
- 当前元素只检查当前初始化槽，整体求值才扫描整组；
- initial/order 仅初始化时创建视图，稳态只建立 input/time；
- 相同布局直接读取，不再重复查询输入 Dimension。

相同负载两轮结果：

| 场景 | 优化前基线 ms | 优化后 ms | 变化 |
| --- | ---: | ---: | ---: |
| material_n_array | 925.3287 / 917.8934 | 496.6264 / 487.4954 | -46.6% |
| information_n_array | 840.6499 / 840.9728 | 412.0512 / 406.3926 | -51.3% |

Material 每元素步由约 576.0 ns 降至 307.5 ns，Information 由约 525.5 ns 降至 255.8 ns；checksum 和 C++ 分配计数不变。gprof 复测 `Document::findDimension` 从约 641 万次降至 810 次，证明主要目标冗余已消除，而不只是耗时环境变化。

新增不同维度顺序 Delay 测试，`[A,B]` 输入重排到 `[B,A]` 的初始输出在 Euler/RK4 均正确。测试初次期望算式写错，已按 `A+10B` 的真实映射修正，未为通过测试修改生产实现。三组 SDXorg 参考、边界、连续/离散/Fixed 混合及 Release/Debug 18/18 CTest 和完整 XMILE corpus 全部通过。

## 访问寿命与参数装箱的后续结果

进一步将 ArrayElementView 的 dimensions/shape 改为调用期 const 引用，并让 Delay 的逐元素状态函数直接接收 double，避免布局元数据复制和参数重新装箱。相对上一阶段两轮结果：

| 场景 | 上一阶段 ms | 本阶段 ms | 再降 |
| --- | ---: | ---: | ---: |
| material_n_array | 496.6264 / 487.4954 | 467.5061 / 463.3947 | 约 5.4% |
| information_n_array | 412.0512 / 406.3926 | 386.0798 / 386.1610 | 约 5.6% |

与最初基线相比，Material 每元素步约 576.0 ns 降至 290.9 ns，Information 约 525.5 ns 降至 241.3 ns，checksum 不变。标量场景有小幅波动，未据此宣称一致收益。

剩余热点主要是实际状态计算、RK 阶段和输出管理。阶段四已取消，没有足够证据继续做 DelayState 拆分、统一热循环或更多容器特判。Release 与 Debug 的 18/18 CTest 和完整 XMILE corpus 通过。

## 结论与后续顺序

本轮不支持继续优化速率 Vector、清零或 ArrayData 数量。优先审查一个统一的“减少 Delay 每元素重复准备”阶段：

1. 当前元素只检查当前初始化槽，整体求值才扫描全部。
2. 稳态只建立 input/time 访问；initial/order 只在需要初始化时建立。
3. 相同布局跳过重复的输入 Dimension 查询，但保留通用校验和不同布局映射。
4. 用相同 profile 验证这些工作确实减少，再决定是否需要把布局描述提升到 EvaluationScope/SimulationPlan。
5. 若标量 RK 仍受状态复制影响，再单独评估 DelayState 阶段拆分；不预先做 Arena 或全面容器替换。

上述候选必须先审查错误语义、数组初始化顺序和不同布局行为，不能通过跳过必要校验换取性能。
