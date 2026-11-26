# 性能 Profile 索引

所有 profile 使用 Release 可执行文件直接运行，不在调试器中计时。构建 `all-profile-targets`，在源码根目录执行：

```bash
cmake --build <RELEASE_BUILD_DIR> --target all-profile-targets --parallel
<RELEASE_BUILD_DIR>/tests/profile/profile-core.exe 10000
<RELEASE_BUILD_DIR>/tests/profile/profile-editor.exe 1000
<RELEASE_BUILD_DIR>/tests/profile/profile-icons.exe
<RELEASE_BUILD_DIR>/tests/profile/profile-formula.exe 1000
<RELEASE_BUILD_DIR>/tests/profile/profile-simulation.exe 1000
```

输出为 CSV：`operation,count,total_ms,per_operation_us`。核心 profile 覆盖创建、连接、ID/nameKey 查找、移动和删除；编辑器 profile 覆盖命令/Scene 创建、连接、移动、撤销/重做、空格平移与窗口焦点状态重置、Cloud 轮廓与边界、空白 Flow 原子创建循环、Flow 双线路径、箭头、名称区域及中心边界几何、直角自动布线点属性、Connector 端点线型、PropertyPanel 刷新、Comment 正文编辑、QDS 保存/打开、XMILE 导出/导入及 SIR 真实模型连续导入。

结果记录：

- [核心与编辑器架构基线](sfd-item-architecture.md)。
- [公式分析性能基线](formula-analysis.md)。
- [仿真设置与引擎性能](simulation.md)。
- [SVG 图标验证与性能](icons.md)。
