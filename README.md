# QDynamicSimulator

QDynamicSimulator 是一款跨平台系统动力学建模与仿真软件。项目采用 C++17、Qt 5 Widgets 和 CMake，以 XMILE 为原生模型格式，许可证为 GPL v3。

当前已实现最小 SFD 编辑闭环：创建存量、流量和辅助变量，移动、框选、删除与连线，编辑名称和方程，以及打开和保存 XMILE 文件。保存文件为 ZIP deflate 容器，内部包含标准 `model.xmile`；也可直接打开未压缩的 XMILE XML。尚未实现仿真。完整需求见 [requirement.md](requirement.md)。

## 基本操作

- 选择“存量”“流量”或“辅助变量”工具，在画布上单击创建元素。
- 选择“连线”工具，依次单击起点和终点；右键取消当前连线。
- “选择”工具支持拖动与框选，滚轮缩放，Delete 删除选中元素。
- Ctrl+Z 撤销、Ctrl+Y 重做，覆盖创建、删除、移动、连线、重命名和方程编辑。
- 右侧属性面板编辑名称与方程；Ctrl+O 打开，Ctrl+S 保存。

## 环境要求

- CMake 3.16 或更高版本
- 支持 C++17 的编译器
- Qt 5.12 或更高版本（Widgets 模块）

## 构建

```bash
cmake -S . -B R:/build-qdynamic-simulator-qt5-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=<QT_PREFIX>
cmake --build R:/build-qdynamic-simulator-qt5-release --parallel
```

如 CMake 无法自动找到 Qt，请在配置时通过 `CMAKE_PREFIX_PATH` 指定 Qt 安装目录。

## 目录结构

```text
src/
├── app/                 Qt Widgets 应用、主窗口与 SFD 画布
├── core/                独立于 UI 的纯 C++ 核心库
└── io/                  基于 QtCore 的 XMILE/ZIP 读写
docs/design/             架构与模块设计文档
.github/workflows/       跨平台 CI
```

## 许可证

本项目依据 [GNU General Public License v3](LICENSE) 发布。
