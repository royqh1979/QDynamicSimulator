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

## 标量编译与 Euler

同一 Release 可执行文件增加固定指数模型：初始 Stock=1、Flow=Stock、DT=0.01、1000 步。编译与运行各执行 1000 次，保存 1001000 个样本点；每次运行校验样本数和最终 Stock，结果查询单独使用已完成的 Run，不重复计时仿真。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| simulation_compile | 1000 | 1.7741 | 1.7741 |
| simulation_euler | 1000 | 287.051 | 287.051 |
| simulation_euler_step | 1000000 | 287.051 | 0.287051 |
| simulation_result_lookup | 1000 | 0.0015 | 0.0015 |

Euler 与结果查询校验和均为约 2.09592e+07。时间由整数步序号派生，1000 步只产生 1001 个样本，避免浮点累计造成终点重复。
