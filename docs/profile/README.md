# 性能 Profile 索引

所有 profile 使用 Release 可执行文件直接运行，不在调试器中计时。构建 `all-profile-targets`，在源码根目录执行：

```bash
cmake --build <RELEASE_BUILD_DIR> --target all-profile-targets --parallel
<RELEASE_BUILD_DIR>/tests/profile/profile-core.exe 10000
<RELEASE_BUILD_DIR>/tests/profile/profile-editor.exe 1000
<RELEASE_BUILD_DIR>/tests/profile/profile-icons.exe
<RELEASE_BUILD_DIR>/tests/profile/profile-formula.exe 1000
<RELEASE_BUILD_DIR>/tests/profile/profile-simulation.exe 1000
<RELEASE_BUILD_DIR>/tests/profile/profile-delay.exe 100 1000 16
<RELEASE_BUILD_DIR>/tests/profile/profile-array.exe 100000 16
<RELEASE_BUILD_DIR>/tests/profile/profile-array.exe 100000 4
<RELEASE_BUILD_DIR>/tests/profile/profile-array-operations.exe 1000000 16
<RELEASE_BUILD_DIR>/tests/profile/profile-array-operations.exe 1000000 4
<RELEASE_BUILD_DIR>/tests/profile/profile-mdl.exe 50
<RELEASE_BUILD_DIR>/tests/profile/random-samples.exe 20000 > random-samples.csv
python tests/profile/check_random_samples.py random-samples.csv
```

输出以 CSV 为主；MDL profile 另含模型元素数和 warning 数。核心 profile 覆盖创建、连接、ID/nameKey 查找、移动和删除；编辑器 profile 覆盖命令/Scene 创建、连接、移动、撤销/重做、空格平移与窗口焦点状态重置、Cloud 轮廓与边界、贝塞尔原始路径裁剪与缓存、Run results 表格绑定与加载刷新、Qt Charts 大结果集绘制/缩放/PNG、空白 Flow 原子创建循环、Flow 双线路径、箭头、名称区域及中心边界几何、直角自动布线点属性、Connector 端点线型、PropertyPanel 刷新、Comment 正文编辑、QDS 保存/打开、XMILE 导出/导入及 SIR 真实模型连续导入。生命周期场景覆盖真实 MainWindow 销毁、Controller/Scene 交替销毁顺序、选中连接线时文档重置和普通 Scene 中独立删除连接端点；`profile-editor 1000` 各执行 100 次。

结果记录：

- [核心与编辑器架构基线](sfd-item-architecture.md)。
- [公式分析性能基线](formula-analysis.md)。
- [仿真设置与引擎性能](simulation.md)。
- [SVG 图标验证与性能](icons.md)。
- [Vensim MDL 导入性能](vensim-mdl.md)。
- [UI 对象生命周期性能与压力验证](object-lifetime.md)。
- [仿真计算热点审查](simulation-hotspots.md)。
- [Delay 分场景热点分析](delay-hotspots.md)。
- [随机函数、统计分布与独立 SciPy 校验](random-functions.md)。
- [延迟、平滑、Fixed 与数组状态性能](delay-functions.md)。
- [数组堆句柄与元素访问性能](array-values.md)。

`profile-delay` 的三个参数依次为运行次数、步数和数组元素数；输出编译/运行分阶段 CSV、状态数、checksum 和 C++ 分配计数。`profile-array` 的两个参数依次为求值迭代数和数组大小，分别覆盖身份读取、嵌套宏、广播、普通数组二元和单元素下标。`profile-array-operations` 使用相同参数，进一步测量 ArrayData 创建、句柄/variant 复制、可变访问和完整结果发布。

`random-samples` 输出 C++ 原始 MT uint32 及五种分布样本；`check_random_samples.py` 使用 NumPy `RandomState` 对照原始 MT，并以 SciPy CDF/KS 和 Poisson 卡方检验验证分布。Poisson 不使用仅适用于连续分布的 `scipy.stats.kstest` 直接判定。
