# 仿真 Profile

## 仿真设置与持久化

运行 Release `profile-simulation.exe 1000`，源码根目录作为工作目录。当前阶段只测不可变 SimulationSettings 的受控替换及真实 QDS/XMILE 文件往返，不包含积分器。

场景使用 start=2、stop=12、DT=0.25、SAVEPER=0.5、RK4、Month；XMILE 因标准没有 SAVEPER 字段，重导入后的 SAVEPER 必须等于 DT=0.25。每轮均比较完整设置，非法 DT=0 的写入必须失败且不改变原值。

```bash
cmake --build <RELEASE_BUILD_DIR> --target profile-simulation --parallel
<RELEASE_BUILD_DIR>/tests/profile/profile-simulation.exe 1000
```

本机结果：

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| settings_validate | 2000 | 0.0168 | 0.0084 |
| settings_qds_roundtrip | 1000 | 676.712 | 676.712 |
| settings_xmile_roundtrip | 1000 | 652.723 | 652.723 |

QDS 与 XMILE 场景校验和均为 12.25。该校验和来自固定 stop+DT，仅用于防止错误结果被忽略，不作为跨格式 SAVEPER 相等的依据。

## 内存运行副本

`simulation_document_copy_xml` 对指数模型执行 1000 次 `DocumentFile::toXml/fromXml`，不使用文件或 ZIP，逐次校验 SimulationSettings 和副本变量。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| simulation_document_copy_xml | 1000 | 32.0992 | 32.0992 |

该通道复用 QDS 字段维护，AST 在副本中重建；运行变量元数据完全来自副本，不建立原模型双重 ID 映射。

## 标量编译与 Euler

同一 Release 可执行文件增加固定指数模型：初始 Stock=1、Flow=Stock、DT=0.01、1000 步。编译与运行各执行 1000 次，保存 1001000 个样本点；每次运行校验样本数和最终 Stock，结果查询单独使用已完成的 Run，不重复计时仿真。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| simulation_compile | 1000 | 1.7741 | 1.7741 |
| simulation_euler | 1000 | 386.03 | 386.03 |
| simulation_euler_step | 1000000 | 386.03 | 0.38603 |
| simulation_result_lookup | 1000 | 0.0015 | 0.0015 |

Euler 与结果查询校验和均为约 2.09592e+07。时间由整数步序号派生，1000 步只产生 1001 个样本，避免浮点累计造成终点重复。

## RK4

同一指数模型改用 RK4，1000 次运行共 100 万步、400 万次阶段求值；每次验证 Completed 和 1001 个样本，最终 Stock 与 exp(10) 对齐。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| simulation_rk4 | 1000 | 2429.52 | 2429.52 |
| simulation_rk4_step | 1000000 | 2429.52 | 2.42952 |
| simulation_rk4_evaluation | 4000000 | 2429.52 | 0.607379 |

RK4 校验和约为 2.20265e+07。Euler 与 RK4 分列，不用不同方法的校验和互相替代；四阶收敛和阶段失败原子性由单元测试覆盖。

## 最小结果 Dock

运行 Release `profile-editor.exe 1000`。`results_table_bind` 每轮将同一真实 SimulationRun 的两个显示副本与 Stock nameKey 绑定到新建 SimulationTableModel，校验 Time+两个 Run 列、排序时间并集去重、行数和末时刻值；模型只共享不可变 Run，不复制完整矩阵。不同采样网格及缺失单元格由编辑器集成测试覆盖。`results_dock_refresh` 反复切换同一 Run 的加载状态，覆盖 Datasets、Run/Variable 选择器、状态文本和表格的同步刷新；不包含 Worker 或积分时间。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| results_table_bind | 1000 | 4.5362 | 4.5362 |
| results_dock_refresh | 1000 | 35.3193 | 35.3193 |
| results_show_current | 1000 | 1.0143 | 1.0143 |

`results_show_current` 另覆盖按 RunId 选择此次运行并切换到 Graph 的调用路径；自动显示是否允许发生由 simulationRevision 和真实 MainWindow 集成测试验证。

真实 MainWindow 测试覆盖首次成功显示、Workbench 与画布单选双向同步、表格末时刻可见、最新 Run 优先、加载状态不删除结果、历史名称、旧模型提示、文档重置清理及无显式选择时优先 Stock。170 条中文翻译全部完成，本次 profile stderr 无警告。

## Qt Charts 大结果集

固定场景由一次 Core 运行生成 100000 个样本和 10 个变量，再构造 10 个共享同一不可变结果的 SimulationRun，避免把重复积分计入图表时间。`chart_build_render_100000x10` 包含 10 条 QLineSeries 的创建、批量点输入、坐标轴和真实 Widget 渲染；`chart_zoom_reset_100000x10` 执行 10 次放大、事件处理和完整范围复位；PNG 使用实际渲染 QImage 写盘。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| chart_build_render_100000x10 | 1 | 594.989 | 594989 |
| chart_png_100000x10 | 1 | 10.1786 | 10178.6 |
| chart_zoom_reset_100000x10 | 10 | 4102.64 | 410264 |

这些结果用于发现异常和后续同场景回归，不设置当前不需要的极限门槛。MainWindow 集成测试另覆盖 Graph/Table、已加载 Run 数量、图例显隐、常量值坐标余量、滚轮、拖动、双击复位和文档清空；profile stderr 无警告。

## Causes Strip

`causes_strip_refresh` 使用真实 Stock/Flow 运行结果，每轮清空并重新选择 Workbench Stock，重建 Stock 与其直接 Flow 两条曲线，覆盖运行副本原因映射、变量排序、历史原因并集和图表生命周期。测试另覆盖 Flow 公式原因、常量原因、不同 Run 原因集合、当前 `charts()` 与 `deleteLater` 旧对象的区分，以及可见图表的时间轴缩放、平移和复位同步。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| causes_strip_refresh | 100 | 482.178 | 4821.78 |

该结果使用 Release `profile-editor.exe 100`，不包含 Worker 或积分时间，不设置额外性能门槛。

## Runs Compare

`runs_compare_bind` 每轮绑定两个相同真实 SimulationRun，按 nameKey 对齐数值 Constant，验证单行结果和零差值；单元测试另覆盖修改、增加和删除常量、旧/新 Run 表头、缺失值、单位、非 Constant 排除以及少于两个已加载 Run 时的空表。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| runs_compare_bind | 100 | 0.0869 | 0.869 |

该结果使用 Release `profile-editor.exe 100`，不包含 Worker 或积分时间。

## 数组执行

固定 16 个数组元素的 Stock，初值均为 1，Flow 为 `Stock * N`，Euler、DT=0.01、1000 步。每轮检查 Completed、1001 个采样点和每个元素的独立递推结果，数组通过 `arraySeries()` 读取；不把首元素作为校验和。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| simulation_array_euler | 1000 | 50655.5 | 50655.5 |
| simulation_array_element_step | 16000000 | 50655.5 | 3.16597 |

共保存 16016000 个数组元素值，逐元素期望使用 `pow(1 + i*DT, steps)` 计算，校验和匹配。该结果使用 Release `profile-simulation.exe 1000`，用于后续同场景回归，不设置额外性能门槛。

## Lookup 求解器

固定独立 Lookup 为两点 continuous 恒等映射，Variable 公式为 `"Rate Table"(TIME)`；1000 次运行共执行 100 万时间步，每次校验 Completed、1001 个样本和最终输出。结果：

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| simulation_lookup_solver | 1000 | 172.677 | 172.677 |
| simulation_lookup_evaluation | 1000000 | 172.677 | 0.172677 |

校验和为 10000。三种模式的端点、域内/域外行为、独立/内联、数组、QDS/XMILE 往返和非法定义均由专项测试覆盖；SDXorg lookups、lookups_xscale 与 lookups_inline 使用其 output.tab 做逐时刻参考比对，xpts/ypts 的 sep 变体及 XMILE corpus 全量通过。Lookup 图形编辑器和 Vensim MDL 文本 Lookup 不在本阶段。热点复查另发现无宏模型每次 Lookup 调用仍执行一次 readMacro，已记录为后续统一调用解析候选，见 [仿真计算热点审查](simulation-hotspots.md)。

## 纯时间输入函数

固定 Document 同时包含 STEP、PULSE、PULSE TRAIN 和 RAMP，1000 次运行共 100 万时间步；每步求值四个函数并校验样本数和最终值。结果：

| 操作 | 数量 | 总耗时 ms | 每时间步 μs |
| --- | ---: | ---: | ---: |
| simulation_time_input_functions | 1000 | 1055.05 | 1055.05 |
| simulation_time_input_evaluations | 1000000 | 1055.05 | 1.05505 |

最终值校验和为 6000。定向测试覆盖跳变点、零宽/短脉冲、repeat<=0、数组广播及 Euler/RK4 相同序列；Vensim input_functions 使用 output.csv 做全变量逐时刻比对。MDL 的 PULSE TRAIN 多字名称由导入器转换为内部 `pulse_train`。热点复查发现通用 timeFunction 在纯标量调用也建立 QVector 参数容器，已记录为后续候选，见 [仿真计算热点审查](simulation-hotspots.md)。

## 执行优化后的基线对比

完成固定数组布局、引用预绑定、Stock/Flow 布局对齐和 Context 复用后，使用与 [仿真计算热点审查](simulation-hotspots.md) 相同的原始基线，在相同冷却条件下复测最终实现两轮。下表同时列出总耗时和归一化耗时，不以单次异常或不同负载替代基线。

| 操作 | 原始总耗时 ms | 优化后两轮总耗时 ms | 原始归一化 | 优化后两轮归一化 | 两轮总耗时降幅 |
| --- | ---: | ---: | ---: | ---: | ---: |
| simulation_euler | 448.134 | 313.911 / 311.886 | 0.448134 μs/步 | 0.313911 / 0.311886 μs/步 | 30.0% / 30.4% |
| simulation_rk4 | 1725.38 | 1450.11 / 1451.38 | 1.72538 μs/步 | 1.45011 / 1.45138 μs/步 | 16.0% / 15.9% |
| simulation_array_euler | 15085.3 | 9544.53 / 9594.27 | 15.0853 μs/数组步；0.942829 μs/元素步 | 9.54453 / 9.59427 μs/数组步；0.596533 / 0.599642 μs/元素步 | 36.7% / 36.4% |

优化前后样本数、Euler/RK4/数组 checksum 均一致；Release 与 Debug 全量 CTest 均通过。开发过程中的热态异常、各阶段独立结果和 gprof 调用次数变化保留在热点审查文档中，不纳入上述最终轮次的选择性平均。
