# QDynamicSimulator

QDynamicSimulator 是一款跨平台系统动力学建模与仿真软件。项目采用 C++17、Qt 5 Widgets 和 CMake，以 QDS 为原生模型格式，支持 XMILE 导入/导出，许可证为 GPL v3。

当前已实现 SFD 编辑闭环：创建存量、流量、通用 Variable、云团和注释，移动、框选、删除与连线，编辑名称和方程，以及打开和保存 QDS 文件。`.qds` 是 ZIP deflate 容器，内部只有一个 `document.xml`；XMILE 1.1 仅作为未压缩 XML 导入/导出。当前还支持标量模型的 Euler/RK4 模拟、命名 Run、结果表格和多 Run 折线图。完整需求见 [requirement.md](requirement.md)。

## 基本操作

- 左侧“绘图元素”面板、工具按钮和菜单项使用内置极简 SVG 图标；顶层主菜单仅显示文字。面板通过分隔符区分实体和流量/箭头工具。
- “流量（Rate）”工具从存量/云团或任意空白位置拖出完整流量实体，空白端自动创建云团；“箭头（Arrow）”创建因果/信息连接，不再提供独立物料流或注释连接类型。
- 连线时左键按住源对象或空白位置，拖动橡皮线到目标后释放；右键取消，操作结束自动回到选择状态。Arrow 释放到空白不会创建对象。
- “选择”工具支持拖动、框选、对齐和等距分布；Shift 拖动限制为 X/Y 方向，Ctrl 拖动复制，Ctrl+Shift 复制且限轴；Delete 删除选中对象。
- 滚轮缩放，中键或空格拖动平移；视图菜单提供适应全部/选中、网格和吸附。
- 双击连接线添加拐点；选中后显示端点/拐点句柄，可拖动重连或调整路径，双击拐点删除。
- Flow 管线绘制为双实线，仅使用直线/贝塞尔路径，默认直线，沙漏形阀门垂直于路径切线；Arrow 绘制为单实线，支持直线、直角线和贝塞尔曲线，以及路径锁定和自动避障。
- 右键连接线可打开上下文菜单；“布线类型”子菜单以互斥勾选项显示当前类型，并提供重置、锁定和删除。
- 连接线显示在实体上方；仅单独选中一条贝塞尔曲线时，以清晰的深色虚线显示端点到控制点的辅助线，多选和框选过程中隐藏辅助线及控制句柄。
- Ctrl+Z 撤销、Ctrl+Y 重做，覆盖创建、删除、移动、连线、重命名、方程和路径编辑。
- 独立 `PropertyPanel` 属性组件编辑名称、方程、单位和说明；选中注释时编辑正文并同步画布，支持撤销/重做，可脱离主窗口独立测试；模型检查面板可定位错误。
- 支持复制、剪切、粘贴、Duplicate、最近文件、搜索定位、缩略导航图和定时恢复副本。
- Ctrl+O 打开，Ctrl+S 保存；打开和拖放均支持 `.qds` 与未压缩 `.xmile`，按内容选择加载通道。Save As 只生成 `.qds`，Export XMILE 生成未压缩 XML。
- 打开缺少视图或路径的纯 XMILE 时自动补齐可见变量、Flow 和 Cloud；原生 QDS 通过 uid 保持相同内容 Comment 的各自连接关系。
- Simulation 菜单设置起止时间、DT/SAVEPER 和 Euler/RK4，命名并运行模型；Run results 支持 Workbench Variable、Datasets 加载状态、时间表及多 Run 折线图。

## 环境要求

- CMake 3.16 或更高版本
- 支持 C++17 的编译器
- Qt 5.12 或更高版本（Widgets、Svg、Charts、LinguistTools 模块）

## 构建

```bash
cmake -S . -B R:/build-qdynamic-simulator-qt5-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=<QT_PREFIX>
cmake --build R:/build-qdynamic-simulator-qt5-release --parallel
```

如 CMake 无法自动找到 Qt，请在配置时通过 `CMAKE_PREFIX_PATH` 指定 Qt 安装目录。测试模型使用 SDXorg/test-models 子模块，首次检出或 CI 中执行 `git submodule update --init 3rdparty/test-models`。

构建会通过 `qt5_create_translation` 更新 `translations/qds_zh_CN.ts` 并生成 QM，随后内嵌为 `:/i18n/qds_zh_CN.qm`。系统语言为中文时自动加载简体中文界面，其它语言保留英文；无需额外安装 QM 文件。

## 测试

常规回归使用 `all-test-targets` 和 `ctest --output-on-failure`。`QDS_ENABLE_XMILE_CORPUS_TESTS` 默认 OFF；修改 XMILE 导入时，配置该选项为 ON 并运行 `io-xmile-corpus`，将验证 `samples/SIR/SIR.xmile` 和全部 67 个 SDXorg XMILE 文件。公式引用变量的孤立判断暂缓并在输出中列出，不支持结构和损坏 XML 必须明确拒绝。

## 目录结构

```text
src/
├── app/                 Qt Widgets 应用、主窗口与 SFD 画布
├── core/                独立于 UI 的核心库，可使用 QtCore
└── io/                  基于 QtCore 的 XMILE/ZIP 读写
docs/design/             架构与模块设计文档
translations/            Qt Linguist TS 源文件，QM 在构建目录生成并内嵌
3rdparty/test-models/    SDXorg 测试模型子模块
.github/workflows/       跨平台 CI
```

## 许可证

本项目依据 [GNU General Public License v3](LICENSE) 发布。
