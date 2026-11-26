# QString 迁移、指针纪律与 Document 视图边界计划

创建时间：2026-11-26 15:14:05 +0800

状态：QString 迁移及 Release/Debug 全量验证完成，等待审核；Document 视图边界尚未实施

## 1. 目标

1. 全部 C++ 源码、测试和 profile 的文本类型统一为 QString，禁止 std::string 和 std::string_view。
2. QByteArray 只允许用于 I/O 的原始字节及编码转换，Core 和业务逻辑不使用 QByteArray。
3. 为 Document::find 等 raw pointer 查询建立明确的短期使用纪律，暂不修改现有返回类型。
4. 讨论并设计 Document 不再持有或重复持有仅属于视图的内容；I/O 与视图侧直接交换所需数据。本项不在当前 QString 实现中修改。

## 2. QString 与位置语义

- 公式、名称、方程、诊断、宏和界面文本全部使用 QString。
- SourceSpan 使用与 QString 一致的字符位置，当前 Qt 5 接口采用 int；不做 UTF-8 字节偏移或 QByteArray 映射。
- XMILE/QDS 的 UTF-8 编解码集中在 I/O。XML 文本写入时由 Qt 处理，业务层不感知字节表示。
- 容器继续优先使用语义合适的 QVector、QHash、QMap、QSet；std::optional、std::variant、std::unique_ptr 等非容器能力不作机械替换。
- 数值解析使用 QString 的受控转换并检查成功状态，不把文本转回 std::string 后调用 strtod。
- 迁移必须保持中文、空格、转义名称、公式语法和持久化内容不变。诊断位置使用 QString 字符位置，不要求兼容旧的 UTF-8 字节偏移数值。

## 3. Raw pointer 使用纪律

find() 返回的指针只能在当前调用栈上即时读取或传给同步调用：

- 不保存为成员变量，不放入长期容器，不供异步任务或长命 lambda 持有。
- 不把对象地址当作身份，身份始终是 EntityId。
- Document 修改、删除、移动、清空或替换后，旧指针一律视为失效，需要时重新获取。
- 既有指针类型可以暂不修改，但文档和新增代码必须遵守该纪律；unique_ptr 引用不被视为自动解决生命周期问题的方案。

## 4. Document 视图边界问题

需要论证把 zoom、滚动位置、Route 控制点、字体和其它仅视图状态移出 Document 的目标结构，以及 save/load/import/export 如何直接读取或修改视图侧数据。

讨论至少覆盖：

- Document 中 Model、Connector/Cloud/Comment 身份、Flow 端点引用与纯视图状态之间的依赖；
- I/O 不依赖 Qt Widgets 的约束，以及通过独立视图 DTO/接口交换数据的方式；
- DocumentController、DiagramScene 与 QDS/XMILE 读写的同步方向；
- 撤销/重做、恢复副本、复制粘贴、打开文件及视图重建；
- 不形成 Document 与 View 两套各自独立修改的权威状态。

本项只有讨论结论写入当前设计后才能实施，不与 QString 迁移混合。

## 5. 分步实施

1. 将 QString、QByteArray 和 raw pointer 规则写入 AGENTS.md，并同步当前设计。
2. 迁移 Core 公共值对象、Document、Model、Dimension、Macro、Validation 和 ChangeSet 的文本接口。
3. 迁移公式 Lexer/Parser、Analysis、Evaluation 和 Function；SourceSpan 直接采用 QString 字符位置。
4. 迁移 I/O，所有 QByteArray 仅限于原始字节/XML/压缩及编码边界。
5. 迁移应用、测试和 profile；保持既有测试场景并补充 QString 中文/位置/转换回归。
6. 每个明确阶段完成专项测试、适用 profile、Release 后 Debug 回归和审核；不自动提交、合并或推送。

## 6. 验收

- C++ 源码中不存在 std::string 或 std::string_view。
- QByteArray 不出现在 Core 和业务逻辑中。
- 现有公式、中文名称、诊断位置、QDS/XMILE、撤销/重做和界面行为保持一致。
- 新代码遵守 raw pointer 使用纪律；相关文档明确失效条件。
- 公式和 Core profile 使用相同场景记录结果，不以迁移名称掩盖性能变化。
- Release 后 Debug 全量测试通过。

## 7. 记录

- 2026-11-26 15:14:05 +0800：建立总体计划；明确 QString、I/O 限定 QByteArray、raw pointer 栈上短期使用及 Document 视图边界待讨论事项。

## 8. 验证与 profile 记录

- C++ 源码、测试和 profile 中无 std::string、std::string_view、fromStdString/toStdString、c_str() 或 std::to_string 残留。
- Core 中无 QByteArray。QByteArray 仅出现在 XML/ZIP 文件 I/O 和应用对外 MIME 数据边界。
- SourceSpan 使用 QString 字符位置；中文公式测试的结束位置从旧 UTF-8 字节数 12 明确更新为字符位置 8。
- Release 和 Debug 完整 CTest 均为 12/12；应用、全部测试和 profile 目标构建成功。

本机 `profile-core.exe`：create 0.64203 μs/op、connect 38.2102 μs/op、lookup 0.004043 μs/op、name_lookup 0.220619 μs/op、move 49.0405 μs/op、validate 0.21774 μs/op、remove 48.8212 μs/op。

本机 `profile-formula.exe 1000`：lex/parse 4.8743 μs/公式、Document 分析 33.2331 μs/公式、标量求值 2.8306 μs/公式、数组分析 5.8804 μs/逐元素公式、数组求值 9.1732 μs/公式、数组初始化列表求值 1.1092 μs/公式、宏分析 14.8074 μs/公式、宏求值 0.6935 μs/公式。

与上一阶段同场景结果相比，Document 分析、数组分析和标量求值出现明显增时。该数据只作为后续性能分析的基线，不在本迁移中声称优化或自动开始性能改造。

- 2026-11-26 16:19:58 +0800：完成 QString/QByteArray 边界迁移、raw pointer 纪律文档、Release/Debug 回归及 Core/公式 profile；记录性能增时，等待审核。
