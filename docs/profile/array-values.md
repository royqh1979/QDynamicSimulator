# 数组堆句柄与元素访问 Profile

## 对比设计

基线为阶段一提交 `dea3337`：ArrayValue 直接保存 dimensions、shape、values 三个 QVector，依赖 QVector 写时复制。阶段二改为 ArrayValue 持有共享 ArrayData 堆对象，const 读取共享；非 const `values()` 在句柄不唯一时克隆 ArrayData，保持调用方修改结果副本不反写原值或 Run。

阶段二同时引入通用 ArrayElementView：参数只求值一次，维度映射只创建一次；相同布局直接行主序寻址，不同布局使用共享重排映射，不再为读取一个元素生成完整广播数组。Delay 与 mapped functions 复用该视图。

运行 Release：

```bash
cmake --build <RELEASE_BUILD_DIR> --target profile-array profile-delay --parallel
<RELEASE_BUILD_DIR>/tests/profile/profile-array.exe 100000 16
<RELEASE_BUILD_DIR>/tests/profile/profile-array.exe 100000 4
<RELEASE_BUILD_DIR>/tests/profile/profile-delay.exe 100 1000 16
```

`profile-array` 的两个参数是迭代次数和数组大小。身份读取与 16 层嵌套宏主要测量参数/栈槽传递，广播主要测量新数组发布，下标主要测量单个元素读取。所有 checksum 与基线一致。

## Evaluator 两轮对比

每个单元格按顺序列出基线两轮与阶段二两轮，单位 ms；变化按两轮均值计算。

| 数组大小 | 场景 | 基线 | 阶段二 | 变化 |
| ---: | --- | ---: | ---: | ---: |
| 16 | array_identity | 9.5456 / 9.2769 | 7.5914 / 7.8127 | -18.2% |
| 16 | array_macro_nested_16 | 258.6621 / 246.7029 | 226.1216 / 237.2391 | -8.3% |
| 16 | array_broadcast | 52.7040 / 51.7333 | 58.1079 / 57.3970 | +10.6% |
| 16 | array_subscript | 41.4225 / 42.6061 | 40.0242 / 39.1157 | -5.8% |
| 4 | array_identity | 9.3069 / 9.4022 | 7.3291 / 7.3041 | -21.8% |
| 4 | array_macro_nested_16 | 253.9188 / 256.4322 | 226.5104 / 232.7014 | -10.0% |
| 4 | array_broadcast | 67.2786 / 67.0316 | 69.9357 / 71.5624 | +5.4% |
| 4 | array_subscript | 24.1925 / 25.1799 | 22.0518 / 22.9843 | -8.8% |

结论：共享句柄减少参数复制，身份读取和嵌套宏稳定受益；新数组发布增加 ArrayData 分配，广播场景稳定回退。不能将该设计称为所有数组操作的普遍加速。

## Delay 数组两轮对比

第一版 ArrayElementView 即使布局相同也创建映射和 strides，曾使数组 Delay 相比阶段一回退约 35%。改为相同布局直接寻址后，最终结果为：

| 场景 | 阶段一运行 ms | 阶段二运行 ms | 阶段一→二 ns/元素步（均值） | 变化 |
| --- | ---: | ---: | ---: | ---: |
| material_n_array | 1488.7886 / 1493.5718 | 958.1881 / 948.1921 | 931.988→595.744 | -36.1% |
| information_n_array | 1402.1998 / 1399.7401 | 880.9690 / 870.5321 | 875.606→547.344 | -37.5% |

相对最初功能基线 `a95eba4`，Material/Information 总耗时分别由 1555.0762/1459.5990 ms 的两轮均值降至 953.1901/875.7506 ms，约下降 38.7%/40.0%。checksum 完全一致。

代价是每次数组变量发布增加 ArrayData 分配：两个数组 Delay 场景的 C++ `operator new` 从每次 Run 13 次增至 2018 次，请求字节数由 64000 增至 8084000。当前负载下，消除重复求值和广播的收益明显大于分配成本；广播微基准说明该成本不能忽略。

标量场景未观察到一致回退：离散 RK4 由阶段一两轮 186.5642/185.2606 ms 变为 175.3959/178.9042 ms；连续 DELAY3 由 188.0947/188.5876 ms 变为 184.1661/189.3234 ms。其差异主要用于后续热点观察，不归因为数组优化的普遍收益。

## 正确性与寿命验证

- 24 层嵌套宏数组参数、ExecutionStack 扩容、数组计算不修改共享输入。
- 下标/切片、维度重排、广播、Apply-to-All、时间函数、随机函数和 Delay 数组参考序列。
- 非 const 修改从 Run 取出的数组副本仍不会反写 Run；历史采样值保持不变。
- Release 与 Debug 均构建 `all-test-targets`，18/18 CTest 和完整 XMILE corpus 通过。

## 后续建议

保留堆句柄与通用视图作为当前候选，是否提交由审核决定。后续先复用 Delay 速率工作区，再根据 profile 判断是否降低数组发布分配；不在本阶段引入 Arena 或更复杂的编译期临时槽管理。
