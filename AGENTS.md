# QDynamicSimulator — Agent 规则与文档索引

## 1. 项目约定

- 使用 C++17、Qt 5 Widgets 和 CMake，不使用 QML/Qt Quick。
- 画布基于 QGraphicsView/QGraphicsScene。
- 项目许可证为 GPL v3；引入第三方组件前确认许可证兼容性。
- 原生模型使用 `.qds` 后缀，采用 ZIP 容器封装单个 `document.xml`，完整保存模型、布局和工作区；XMILE 仅用于导入/导出且始终是未压缩 XML，禁止作为 ZIP 处理。开发阶段不保留旧 ZIP 兼容层。
- 权威构建说明见 `README.md` 和本机环境文档，不凭空创造构建步骤。

## 2. 架构边界

- `src/core/` 是独立于 UI 的模型、文档与仿真核心库，可以使用 QtCore，不得依赖 Qt Widgets、QGraphicsItem 或其它 UI 类型；容器在语义合适时优先使用 Qt 容器，不因既有 STL 用法而继续扩大平行风格。
- `src/io/` 是独立 QtCore I/O 库，负责 XMILE XML 与 ZIP 容器读写，不依赖 Widgets。
- `src/app/` 是 Qt Widgets 桌面应用，通过 `QDS::Core` 和 `QDS::IO` 使用核心能力，不在 UI 层实现仿真算法。
- 各模块源文件分别登记到各自的 `CMakeLists.txt`，构建目标保持分层。
- 不得用局部补丁或兼容层凑合掩盖不合理设计；核心表示或职责不合理时应先重构，保证同一语义只有一种权威表达。
- 重构前先记录目标模型、迁移边界和验收场景，审查通过后再实施；不得为保留旧实现而复制平行模型。
- 结果图表使用官方 Qt Charts 模块，仅允许应用层依赖；保留可重复性能 profile 以发现异常和回归，不设置超出当前需求的极限性能门槛。实现前确认本机 Qt 5 Charts 与现有构建环境匹配。

## 3. 文档规则

- `AGENTS.md` 只保存长期规则和顶层索引，不堆叠具体设计、profile 数据或本机命令。
- 根目录保留需求、构建和项目入口文档；内部设计、性能和环境文档放入 `docs/` 子目录。
- `docs/design/` 保存现状、约定、算法和实施方案，按模块组织。
- `docs/history/` 只保存重构和性能优化的带时间戳计划及其后续细化记录；计划中的功能实现不进入 history，其当前设计与阶段计划写入 `docs/design/`，实际演进由 git log 记录。开始重构或性能优化前必须先建立计划，文件名以 `YYYY-MM-DD` 开头，文首记录完整本地创建时间；先写整体目标、范围、方法、步骤和验收边界，再随设计讨论逐步细化更新。计划完成后在文档名末尾追加 `-done`，计划放弃则追加 `-abandon`，并同步更新所有引用。
- `docs/history/` 是计划与演进记录，不替代当前权威设计；形成或修改设计结论时必须同步更新 `docs/design/`。
- `docs/profile/` 保存性能基线、报告、结果与配套工具，按模块组织。
- `docs/env/template/` 不含机器专有路径；`docs/env/local/` 保存本机实际配置，默认不提交。
- 设计文档描述现状和约定，具体修改记录写入 git 提交信息，不放进设计文档。
- 本文件使用中文；代码注释和 git 提交信息使用英文；其它文档语言随已有文档。
- 完成用户可见功能或修复时更新 `NEWS.md` 当前版本节，不记录开发过程中临时引入又修复的问题。
- 修改目录职责、构建选项、测试命令或上述规则时，同步更新本索引及相关文档。

## 4. 代码与资源约定

- 使用 Qt 惯用法：signals/slots、用户可见字符串 `tr()`、QObject 父子所有权。
- 新代码与所在文件现有命名、结构和行尾风格保持一致。
- 容器优先使用语义合适的 Qt 容器（如 QVector、QHash、QMap、QSet）；std::unique_ptr、std::optional、std::variant 等非容器能力不作机械替换。既有 STL 容器只在相关重构中逐步迁移，不单独进行无收益的全量替换。
- 文本统一使用 QString，禁止 `std::string` 和 `std::string_view`。QByteArray 只允许用于 I/O 的原始字节和编码转换，不得进入 Core 或业务逻辑；公式、词法位置和 SourceSpan 直接使用 QString 的字符位置，不做 UTF-8 字节映射。
- 源代码和测试源码不得直接包含中文字符；用户可见源字符串使用英文，中文兼容性测试数据使用 Unicode 转义，设计文档不受此限制。
- `translations/` 保存 Qt Linguist TS 源文件，构建通过 `qt5_create_translation` 生成并内嵌 QM；中文翻译只修改 TS，不在 C++ 源码中直接写入中文界面文本。
- 不提交密钥、凭据或本机专有配置；执行外部工具和不可信代码前必须进行参数与转义检查。

### Lambda / 槽寿命

- `connect()`、`QTimer::singleShot()` 等以 lambda 作槽时必须传入合适的 QObject context，且连接寿命不得超过其捕获对象寿命。
- 不得用 `&` 引用捕获局部变量供长命连接使用；需要写回局部变量时，使用与局部变量同生命周期的局部 QObject context，或在返回前显式 disconnect。
- 按值捕获的裸指针必须明显比 context 长寿，优先捕获值类型或 shared_ptr。
- Document::find 等接口返回的 raw pointer 只能在调用栈上即时使用；不得保存为成员、放入长期容器、供异步任务/长命 lambda 持有，Document 发生修改、移动、清空或删除后必须重新获取。该纪律不替代后续生命周期接口设计。

## 5. 构建与测试规则

### 5.1 单功能门禁（强制执行）

- 一次只实现一个明确功能，不得在用户审核前开始下一项功能或扩展到其他改造。
- 每个功能必须依次完成：实现→完整单元测试→可重复性能 profile→Release 后 Debug 全量回归→自我审核。
- 上述检查完成后必须停止并等待用户审核；不得自动继续下一项，不得以 goal、自动续跑或既定计划绕过等待。
- 只有用户明确批准当前功能后，才可提交、合并；历史授权不构成后续功能的持续授权，授权被撤回后立即停止相应操作。
- 用户要求停止时，立即停止实现、测试、提交和合并等所有后续工作，并保留现场供用户审查。

### 5.2 测试与性能

- 测试使用 Qt Test，CTest 接入根目录构建。
- 任何新增功能必须同时提供完整单元测试和可重复运行的性能 profile；覆盖正常路径、边界、错误、撤销/重做（如适用）及持久化（如适用），未经测试和 profile 验证不得视为完成。
- 完整回归必须先 Release 后 Debug，分别构建 `all-test-targets` 并执行 `ctest --output-on-failure`；两种构建均通过才算完成。
- 修改 XMILE 导入功能时，除常规回归外必须显式开启 `QDS_ENABLE_XMILE_CORPUS_TESTS`，使用 `3rdparty/test-models` 全量 XMILE 语料验证；该选项默认 OFF，不得用跳过列表掩盖回归。
- 测试按被测组件职责放入对应目录和测试目标，不得因链接方便混放。
- 调试形成的有效验证场景必须保留为正式断言，不得修完后删除。
- 不得因测试不符合当前实现或预设而丢弃场景；先判断是否存在新合法场景或实现 bug。夹具错误只能修正错误部分，并保留原场景或增加等价回归测试。
- 直接运行 Qt Test 诊断问题时必须设置 `QT_FORCE_STDERR_LOGGING=1`，GUI 测试根据实际插件可用性选择 `QT_QPA_PLATFORM` 并设置合适字体目录；不得只保存结果文件而忽略 stderr 中的 qDebug/qWarning。静态 Qt 若仅提供 windows 插件，不得强制 offscreen。
- GUI 鼠标测试必须创建真实 View，并显式设置 button/buttons、localPos/screenPos 及需要的 scenePos/item pos；offscreen 下不得依赖 QCursor::pos()。坐标取整使用亚像素容差，控制句柄不得因加入选择集合而被同步逻辑隐藏。
- 窗口和面板测试验证用户可观察语义，不比较 Qt `saveState()` 等不透明布局字节，不依赖未明确约定的面板宽度、排列或像素位置。只断言显式设置的窗口尺寸、Dock 隐藏/浮动/角落归属及目标区域可见性，使正常布局调整不导致无关测试修改。
- 测试/profile 配套脚本和工具必须随对应资源入库，并在专门文档记录用途、依赖、输入输出和运行命令，不得只放临时目录或用完即弃。
- 性能测量直接运行 Release 可执行文件，不在 gdb 下计时；优先使用扣除子阶段后的 self-time，不能把 inclusive 时间相加。
- profile 不得只记录耗时：必须使用热点分布、调用次数或分阶段测量审查重复解析、分配、复制和不必要遍历，并记录结论。没有热点证据时不直接修改生产代码；仅有时间记录不视为完成性能审查。
- 性能优化逐项实施：独立分支→在 `docs/history/` 建立带时间戳的整体计划并逐步细化→同步当前设计文档→KISS/范围审核→测试保护→严格范围编码→专项和全量回归→同场景 profile→审核迭代→结果总结与提交。多个方案分别建分支比较；合并到 `master` 和推送仍需明确授权。
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
- `docs/design/formula-analysis.md`：公式 AST、语法分析、符号与依赖图设计。
- `docs/design/simulation.md`：仿真设置、执行计划、Euler/RK4、运行结果与正确性验证设计。
- `docs/design/simulation-ui.md`：模拟器设置、Worker 生命周期、结果查看与图表界面设计。
- `docs/history/`：带时间戳的重构/性能优化整体计划与逐步细化记录；开始此类工作前必须先建立。
- `docs/profile/README.md`：性能基线、报告、优化结果与工具索引；开始性能工作后建立。
- `docs/references/README.md`：参考文献及 `3rdparty/test-models` 测试模型子模块的来源、许可证和使用约定。

## 8. Git 与安全规则

- 大的计划性改动必须在独立分支进行，不直接在 `master` 上工作。
- 修复 bug 后先报告根因和方案，得到确认再提交；提交信息使用英文。
- 合并到 `master` 或推送前必须获得明确授权，不得擅自执行。
- 合并到 `master` 并确认工作区干净后，删除已合并且不再需要的本地分支；使用 `git branch -d`，不得用 `-D` 删除含未合并工作的分支。当前分支或仍含未合并依赖改动的分支不删除；删除远程分支需另行明确授权。
- 任务结束后删除本任务专用的临时构建、调试、profiler 和结果输出目录/文件，包括仓库外为本任务创建的目录；保留标准 Release/Debug 构建目录及正式文档/profile 记录。删除前确认路径为本任务创建且没有正在使用它们的进程。
- 不得提交密钥、`.env`、SSH 私钥或本机专有配置；构建产物不进入仓库。
