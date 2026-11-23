# 总体架构

## 当前结构

- `qdynamic-simulator`：Qt Widgets 可执行程序，负责 SFD 画布、变量编辑和后续交互。
- `qds-core`（别名 `QDS::Core`）：独立纯 C++ 静态库，承载变量、连接、方程与后续仿真能力。
- `qds-io`（别名 `QDS::IO`）：QtCore I/O 静态库，负责标准 XMILE XML 和 ZIP deflate 容器读写。
- 应用只能依赖核心库与 I/O 库的公开头文件，核心库不得依赖 Qt 或反向依赖应用。

## 后续模块划分

核心库内部按模型结构、方程编译、积分器、运行结果、实验分析和文件互操作划分职责。XMILE/ZIP 读写属于核心或独立 I/O 模块，不得依赖 Widgets。桌面应用后续按画布、方程编辑、属性面板、结果可视化和工作区组织。

M1 先交付 SFD 建模、方程编辑、Euler/RK4、折线图和文件保存，并同步建立计算正确性验证。蒙特卡洛敏感性分析和 Vensim 导入属于后续里程碑，不在初始骨架中预设实现依赖。

## 模块设计索引

- [SFD 查看与编辑器设计](sfd-editor.md)：完整功能清单、连接线/布线子系统和实施计划。
- [SFD 核心模型与画布 Item 架构](sfd-item-architecture.md)：Item 类体系、稳定 ID 互找、Document 单一事实来源、ChangeSet 同步及撤销设计。
