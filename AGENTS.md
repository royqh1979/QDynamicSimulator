# QDynamicSimulator — Agent 规则与文档索引

## 1. 项目约定

- 使用 C++17、Qt 5 Widgets 和 CMake，不使用 QML/Qt Quick。
- 画布基于 QGraphicsView/QGraphicsScene。
- 项目许可证为 GPL v3；引入第三方组件前确认许可证兼容性。
- 原生模型采用 ZIP 容器封装的标准 XMILE 与独立专有 XML，不在 XMILE 主体中混入专有扩展。
- 权威构建说明见 `README.md` 和本机环境文档，不凭空创造构建步骤。

## 2. 架构边界

- `src/core/` 是纯 C++ 模型、文档与仿真核心库，不依赖 Qt UI；新增功能默认不引入 Qt 依赖。
- `src/io/` 是独立 QtCore I/O 库，负责 XMILE XML 与 ZIP 容器读写，不依赖 Widgets。
- `src/app/` 是 Qt Widgets 桌面应用，通过 `QDS::Core` 和 `QDS::IO` 使用核心能力，不在 UI 层实现仿真算法。
- 各模块源文件分别登记到各自的 `CMakeLists.txt`，构建目标保持分层。
- 图表库在原型压测后再确定，不提前引入 Qt Charts 或 QCustomPlot 依赖。

## 3. 文档规则

- `AGENTS.md` 只保存长期规则和顶层索引，不堆叠具体设计、profile 数据或本机命令。
- 根目录保留需求、构建和项目入口文档；内部设计、性能和环境文档放入 `docs/` 子目录。
- `docs/design/` 保存现状、约定、算法和实施方案，按模块组织。
- `docs/profile/` 保存性能基线、报告、结果与配套工具，按模块组织。
- `docs/env/template/` 不含机器专有路径；`docs/env/local/` 保存本机实际配置，默认不提交。
- 设计文档描述现状和约定，具体修改记录写入 git 提交信息，不放进设计文档。
- 本文件使用中文；代码注释和 git 提交信息使用英文；其它文档语言随已有文档。
- 完成用户可见功能或修复时更新 `NEWS.md` 当前版本节，不记录开发过程中临时引入又修复的问题。
- 修改目录职责、构建选项、测试命令或上述规则时，同步更新本索引及相关文档。

## 4. 代码与资源约定

- 使用 Qt 惯用法：signals/slots、用户可见字符串 `tr()`、QObject 父子所有权。
- 新代码与所在文件现有命名、结构和行尾风格保持一致。
- 不提交密钥、凭据或本机专有配置；执行外部工具和不可信代码前必须进行参数与转义检查。

### Lambda / 槽寿命

- `connect()`、`QTimer::singleShot()` 等以 lambda 作槽时必须传入合适的 QObject context，且连接寿命不得超过其捕获对象寿命。
- 不得用 `&` 引用捕获局部变量供长命连接使用；需要写回局部变量时，使用与局部变量同生命周期的局部 QObject context，或在返回前显式 disconnect。
- 按值捕获的裸指针必须明显比 context 长寿，优先捕获值类型或 shared_ptr。

## 5. 构建与测试规则

- 测试使用 Qt Test，CTest 接入根目录构建。
- 任何新增功能必须同时提供完整单元测试和可重复运行的性能 profile；覆盖正常路径、边界、错误、撤销/重做（如适用）及持久化（如适用），未经测试和 profile 验证不得视为完成。
- 完整回归必须先 Release 后 Debug，分别构建 `all-test-targets` 并执行 `ctest --output-on-failure`；两种构建均通过才算完成。
- 测试按被测组件职责放入对应目录和测试目标，不得因链接方便混放。
- 调试形成的有效验证场景必须保留为正式断言，不得修完后删除。
- 不得因测试不符合当前实现或预设而丢弃场景；先判断是否存在新合法场景或实现 bug。夹具错误只能修正错误部分，并保留原场景或增加等价回归测试。
- 测试/profile 配套脚本和工具必须随对应资源入库，并在专门文档记录用途、依赖、输入输出和运行命令，不得只放临时目录或用完即弃。
- 性能测量直接运行 Release 可执行文件，不在 gdb 下计时；优先使用扣除子阶段后的 self-time，不能把 inclusive 时间相加。
- 性能优化逐项实施：独立分支→设计文档→KISS/范围审核→测试保护→严格范围编码→专项和全量回归→同场景 profile→审核迭代→结果总结与提交。多个方案分别建分支比较；合并到 `master` 和推送仍需明确授权。
- 核心引擎实现时同步建立自动化验证，至少覆盖解析解基准、积分阶数、不变量与黄金文件回归。
- 与 Vensim 比对时必须对齐积分方法和步长。

## 6. 环境与工具索引

- 构建指南：`README.md`。
- 通用本机环境模板：`docs/env/template/local-development.md`。
- 本机实际环境：`docs/env/local/AGENTS.local.md`（gitignore，不存在时先复制模板）。
- 本机文件不得提交；`R:` 内存盘内容重启后失效，构建目录需重新配置。

## 7. 设计与 Profile 索引

- `docs/design/architecture.md`：总体设计及全部模块设计文档索引。
- `docs/design/sfd-editor.md`：SFD 完整功能清单与实施计划。
- `docs/design/sfd-item-architecture.md`：核心模型、Item 类体系和同步设计。
- `docs/profile/README.md`：性能基线、报告、优化结果与工具索引；开始性能工作后建立。

## 8. Git 与安全规则

- 大的计划性改动必须在独立分支进行，不直接在 `master` 上工作。
- 修复 bug 后先报告根因和方案，得到确认再提交；提交信息使用英文。
- 合并到 `master` 或推送前必须获得明确授权，不得擅自执行。
- 不得提交密钥、`.env`、SSH 私钥或本机专有配置；构建产物不进入仓库。
