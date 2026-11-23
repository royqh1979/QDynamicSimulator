# QDynamicSimulator — Agent 规则与文档索引

## 1. 项目约定

- 使用 C++17、Qt 5 Widgets 和 CMake，不使用 QML/Qt Quick。
- 画布基于 QGraphicsView/QGraphicsScene。
- 项目许可证为 GPL v3；引入第三方组件前确认许可证兼容性。
- 原生模型采用 ZIP 容器封装的标准 XMILE 与独立专有 XML，不在 XMILE 主体中混入专有扩展。
- 用户可见字符串使用 `tr()`，QObject 对象遵循父子所有权和 Qt 惯用法。

## 2. 架构边界

- `src/core/` 是纯 C++ 仿真与模型核心库，不依赖 Qt UI；新增功能默认不引入 Qt 依赖。
- `src/io/` 是独立 QtCore I/O 库，负责 XMILE XML 与 ZIP 容器读写，不依赖 Widgets。
- `src/app/` 是 Qt Widgets 桌面应用，通过 `QDS::Core` 和 `QDS::IO` 使用核心能力，不在 UI 层实现仿真算法。
- 各模块源文件分别登记到各自的 `CMakeLists.txt`，构建目标保持分层。
- 图表库在原型压测后再确定，不提前引入 Qt Charts 或 QCustomPlot 依赖。

## 3. 文档规则

- 根目录保留需求、构建和项目入口文档。
- 内部设计、约定与实施方案放入 `docs/design/`。
- 设计文档描述当前设计与约定，具体修改记录写入 git 提交信息。
- 本文件使用中文；代码注释和 git 提交信息使用英文。
- 修改目录职责、构建方式或上述规则时，同步更新相关文档。

## 4. 构建与验证

```bash
cmake -S . -B R:/build-qdynamic-simulator-qt5-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=<QT_PREFIX>
cmake --build R:/build-qdynamic-simulator-qt5-release --parallel
```

- 构建产物放在 `R:` 内存盘；重启后需重新 configure。
- 本机工具链和 Qt 路径见 `docs/env/local/AGENTS.local.md`（不提交）；其它机器复制 `docs/env/template/local-development.md` 后填写。
- 核心引擎实现时同步建立自动化验证，至少覆盖解析解基准、积分阶数、不变量与黄金文件回归。
- 测试采用 Qt Test 与 CTest，并按被测模块组织；核心引擎不得依赖 UI 测试环境。
- 与 Vensim 比对时必须对齐积分方法和步长。
- 构建产物、本机配置、密钥和凭据不得提交。
