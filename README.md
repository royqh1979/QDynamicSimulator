# QDynamicSimulator

QDynamicSimulator 是一款跨平台系统动力学建模与仿真软件。项目采用 C++17、Qt 5 Widgets 和 CMake，以 XMILE 为原生模型格式，许可证为 GPL v3。

当前已实现 SFD 编辑闭环：创建存量、流量、辅助变量、常量、外生变量、云团和注释，移动、框选、删除与连线，编辑名称和方程，以及打开和保存 XMILE 文件。保存文件为 ZIP deflate 容器，内部包含标准 `model.xmile`；也可直接打开未压缩的 XMILE XML。尚未实现仿真。完整需求见 [requirement.md](requirement.md)。

## 基本操作

- 从左侧可隐藏“绘图元素”面板拖入存量、流量、辅助变量、常量、外生变量、云团或注释。
- “物料流”工具创建符合存量/流量/云团规则的管线；“依赖线”和“注释箭头”分别表达计算依赖和注释关系。
- 连线时左键按住源实体，拖动橡皮线到目标实体后释放；右键取消，操作结束自动回到选择状态。
- “选择”工具支持拖动、框选、对齐和等距分布；Delete 删除选中元素或连接。
- 滚轮缩放，中键或空格拖动平移；视图菜单提供适应全部/选中、网格和吸附。
- 双击连接线添加拐点；选中后显示端点/拐点句柄，可拖动重连或调整路径，双击拐点删除。
- 布线菜单切换直线、直角线和贝塞尔曲线，支持路径锁定、自动避障和全图重新布线。
- Ctrl+Z 撤销、Ctrl+Y 重做，覆盖创建、删除、移动、连线、重命名、方程和路径编辑。
- 右侧属性面板编辑名称、方程、单位和说明；模型检查面板可定位错误。
- 支持复制、剪切、粘贴、Duplicate、最近文件、搜索定位、缩略导航图和定时恢复副本。
- Ctrl+O 打开，Ctrl+S 保存；ZIP 容器包含 model.xmile 和独立 workspace.xml。

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
