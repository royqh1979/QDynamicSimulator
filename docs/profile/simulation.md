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

运行 Release `profile-editor.exe 1000`。`results_table_bind` 每轮将真实 NamedRun 与 Stock nameKey 绑定到新建 SimulationTableModel，读取行数和末时刻值；模型只共享不可变 Run，不复制完整矩阵。`results_dock_refresh` 反复切换同一 Run 的加载状态，覆盖 Datasets、Run/Variable 选择器、状态文本和表格的同步刷新；不包含 Worker 或积分时间。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| results_table_bind | 1000 | 2.6501 | 2.6501 |
| results_dock_refresh | 1000 | 35.3193 | 35.3193 |

真实 MainWindow 测试覆盖首次成功显示、Workbench 与画布单选双向同步、表格末时刻可见、最新 Run 优先、加载状态不删除结果、历史名称、旧模型提示、文档重置清理及无显式选择时优先 Stock。163 条中文翻译全部完成，本次 profile stderr 无警告。

## Qt Charts 大结果集

固定场景由一次 Core 运行生成 100000 个样本和 10 个变量，再构造 10 个共享同一不可变结果的 NamedRun，避免把重复积分计入图表时间。`chart_build_render_100000x10` 包含 10 条 QLineSeries 的创建、批量点输入、坐标轴和真实 Widget 渲染；`chart_zoom_reset_100000x10` 执行 10 次放大、事件处理和完整范围复位；PNG 使用实际渲染 QImage 写盘。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| chart_build_render_100000x10 | 1 | 594.989 | 594989 |
| chart_png_100000x10 | 1 | 10.1786 | 10178.6 |
| chart_zoom_reset_100000x10 | 10 | 4102.64 | 410264 |

这些结果用于发现异常和后续同场景回归，不设置当前不需要的极限门槛。MainWindow 集成测试另覆盖 Graph/Table、已加载 Run 数量、图例显隐、常量值坐标余量、滚轮、拖动、双击复位和文档清空；profile stderr 无警告。
