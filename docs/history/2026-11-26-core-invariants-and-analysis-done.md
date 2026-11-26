# Core 不变性审计与 Analysis 重构计划

创建时间：2026-11-26 11:30:29 +0800

状态：已完成并合并；Document 封装与不可变 AST 阶段通过全量验证，Formula/Analysis 归属由 2026-11-26-formula-analysis-restructure-done.md 继续记录

## 1. 背景与已经确定的约束

当前 Core 中一些长期保存的对象、AST 和分析结果使用公开字段，文档宣称的不可变性、名称索引一致性和端点约束只能依赖调用方自觉，无法由 C++ 接口保证。

本次规划以以下结论为前提：

- Document 是模型事实和对象生命周期的权威来源，ModelAnalysis 是只读派生结果。
- AST 只保存语法结构、名称、字面量和 SourceSpan，不保存 EntityId、函数目标、宏定义或其它 Resolve 结果。
- 分析和求值按需使用现有实体、维度、函数和宏索引；必要的随机查询必须有统一接口。
- 不使用 `vector + EntityId→下标` 作为需要增删或长期维护的主要关联结构，也不以“vector 元素指针 + 索引”规避下标问题。
- 宏不做文本或 AST 级联展开；非递归、直接递归和间接递归采用统一调用语义。
- 不做机械的全量 `struct` 到 `class` 替换。只有需要保护不变性、所有权或生命周期的类型才封装。
- Core 独立于 UI，但可以使用 QtCore；容器在语义合适时优先使用 Qt 容器，不要求 Core 保持纯 C++，仍不得依赖 Qt Widgets 或画布对象。

## 2. 总体目标

### 2.1 Core 不变性审计与封装

审计 `src/core/include/core/` 中的全部类型，区分：

- Document 长期持有并具有身份、命名、端点或索引约束的对象；
- 创建后不可变的公式 AST；
- 带查询关系和一致性要求的 Analysis 结果；
- 没有内部不变量的纯值对象、编辑输入输出 DTO 和求值配置。

对需要保护的类型使用私有状态和明确的构造规则，提供必要的 const 访问接口。只有存在真实修改需求时才提供非 const 接口，并确保修改经过能够维护不变量的所有者或受控方法。

### 2.2 Analysis 详细设计与重构

在实现前单独细化 Analysis 的：

- Document 与 ModelAnalysis 的对应关系及各自生命周期；
- Element、数组逐元素公式、Macro 和诊断的稳定标识；
- 主存储、查找复杂度、遍历顺序及删除语义；
- Parser、Resolver、Analyzer、Evaluator 之间按需传递的数据；
- 构建期可变与完成后只读之间的边界；
- 宏定义指针的替代方式及宏签名键；
- 当前全量重分析和未来增量修改的失效边界。

关联容器应直接承担主存储和查找职责，不再平行维护下标索引。是否需要稳定输出顺序由输出边界处理，不为遍历顺序额外复制一套存储。

## 3. 方法

### 3.1 先形成完整审计表

逐一头文件先列出类型的逻辑目的及“历史事实/当前状态/构建输入”分类，再记录所有者、不变量、生命周期和公开接口；现有读写方只用于评估迁移影响。审计至少覆盖 Element、Model、Diagram、Dimension、Macro、Identifier、ElementRange、Document、ChangeSet、Validation、formula 子系统及其实现内部类型。

审计结果先写入本计划并同步当前设计文档，经审查后再按模块实施。不得只处理 AST 而遗漏 Document 非 const 查找和公开字段对名称索引的绕过。

### 3.2 按不变量而非关键字设计接口

- 纯值对象和输入 DTO 可以继续使用公开 struct。
- Document 内对象、AST 和 ModelAnalysis 使用私有状态及只读访问。
- 修改 Document 内对象必须经过 Document 或明确的受控接口。
- Parser/Analyzer 的可变构造过程与公开只读结果分离；Builder 只存在于实现边界内。
- 不返回可绕过约束的非 const 容器、子节点指针或内部引用。
- 不为没有调用需求的字段增加成套 getter/setter。
- **尽量使用 const 和 immutable 保证不变量是核心设计原则，但必须权衡构建与使用的复杂度。** 对 ChangeSet 等需要逐步收集内容的对象，专项比较独立 Builder、不可逆 freeze() 两阶段对象、私有修改方法加受限友元，以及一次性构造函数；不能为了形式上的不可变制造难以使用的接口，也不能为了方便让已发布结果继续公开可变。

### 3.3 Analysis 容器与生命周期单独论证

重点比较直接关联容器、排序关联容器及其它方案的所有权、查找、增删、引用稳定性和输出顺序。语义合适时优先评估 QHash、QMap、QVector、QSet 等 Qt 容器，而不是默认新增 STL 容器。不得把下标索引替换为仍依赖 vector 地址稳定性的指针索引。

明确区分“名称到定义的 Resolve 查询”和“EntityId 到 ElementAnalysis 的结果查询”，二者都不得写回 AST。Profile 用于测量实际成本，不再把必要查找接口视为可有可无的优化。

### 3.4 小步验证

每个实施步骤独立分支、独立范围，依次完成：

1. 更新本计划和相关当前设计文档；
2. 保留或补充正常、边界、错误及生命周期测试；
3. 对涉及 Analysis/Evaluation 热路径的步骤运行可重复 profile，比较同场景结果；
4. Release 后 Debug 全量回归；
5. 自我审核并停止，等待用户审查；
6. 只有获得明确授权后才提交、合并或推送。

## 4. 初步步骤

1. **Core 类型审计与复核**：形成完整清单和分类，并审查是否过度封装或遗漏内部类型。
2. **Document 与实体不变量**：细化局部/全局约束、失败原子性、ID、容器和非 const 访问边界。
3. **AST 不可变设计**：细化构造、只读访问、错误节点和所有权规则。
4. **ArrayValue 与变更记录**：分别保护数组值约束，审查并优先简化 ChangeRecord/ChangeSet 状态。
5. **Analysis 详细设计**：先论证独立存在的必要性，再细化保留结果的关联主存储、宏键、诊断对应、Builder 和失效规则。
6. **分模块实施**：按审查通过的顺序，每次只处理一个明确边界，不同时混入宏功能或仿真执行器扩展。

步骤顺序可以随审计证据调整，但不得省略审查和回归门禁。

## 5. 非目标

- 不引入第二套平行 Document、AST 或 Analysis 模型。
- 不改变 QDS/XMILE 持久化格式和既有模型语义。
- 不在本计划中实现完整仿真执行器、命名模型或外部 include。
- 不为了形式整齐给纯值对象增加无意义封装。
- 不预先引入未经验证的缓存、对象池或复杂增量框架。
- 不在本次审计中单独进行全量 STL 容器替换；只在对应模块重构时采用合适的 Qt 容器。std::unique_ptr、std::optional、std::variant 等非容器能力不作机械替换。

## 6. 验收边界

- 需要长期保护的状态不能通过公开字段或非 const 容器被绕过修改。
- AST 不包含 Resolve 结果，创建后不能通过公开接口改变语法结构。
- ModelAnalysis 的所有权、对应关系、查询和生命周期明确，不使用下标派生索引。
- 名称解析和按 ID 获取分析结果具有统一接口，调用方不再各自线性扫描。
- 现有功能、诊断和持久化回归通过；涉及求值路径的重构具有同场景性能数据。
- 每个阶段完成文档、测试、profile（适用时）、Release/Debug 和审核后才进入下一阶段。

## 7. 后续需要细化的问题

- ChangeSet 等逐步构建的不可变对象采用 Builder、不可逆 freeze() 还是私有修改方法加受限友元；需要比较编译期保护、运行时状态、接口复杂度、复制/移动和撤销栈使用方式。freeze() 方案必须明确冻结后的修改如何失败、能否移动/复制及是否允许解冻；友元方案必须限制友元范围和生命周期，不能让友元在发布后保留写通道。
- Document 的非 const `find()` 是否应完全私有，以及内部编辑如何访问受控对象。
- **查找结果的生命周期**：当前 find() 返回 `const DocumentElement *`，调用方可能在 Document 删除、清空、移动或替换后继续使用悬空指针。Analysis 专项前需比较返回指针、`unique_ptr` 引用、稳定句柄/ID 及受控访问回调等方案。`unique_ptr&` 只能改变所有权的表达，不能自动防止悬空引用，也不能据此默认延长对象生命周期。
- ModelElement、Flow、Connector 等对象的只读接口与 Document 修改接口。
- AST 在语法错误时是否允许不完整节点，以及对应的构造不变量。
- ModelAnalysis 的关联键、宏重载/默认参数签名和诊断稳定排序。
- Analysis Builder 的最小接口及与 analyzeDocument() 的关系。
- 全量重分析作为当前实现时，如何保留未来按定义失效的空间而不提前实现缓存。
- **Analysis 是否有必要作为独立结果长期存在**：既然 Analysis 与 Document 中的元素一一对应，且 AST 不保存 Resolve 结果，需要论证是否在元素/宏创建或公式修改时直接建立并更新其 AST，由 Document 内的对应对象持有，从而消除平行 Analysis 容器和一致性索引。后续细化时必须比较这种聚合方式与独立 ModelAnalysis 在语法错误、全局依赖、跨元素诊断、Document 职责、撤销/重做、持久化及求值生命周期上的差异，不能仅因“一一对应”就预设合并。

## 8. 细化记录

- 2026-11-26 11:30:29 +0800：建立总体目标、约束、方法和初步步骤；尚未开始类型审计或实现。
- 2026-11-26 11:39:27 +0800：记录“AST 直接随 Document 元素/宏持有、Analysis 是否仍需独立存在”的问题；推迟到不变性审计和封装完成后细化。
- 2026-11-26 11:44:20 +0800：完成全部 Core 公共类型的首轮封装审计，形成不变量、DTO、现有合理封装及分步迁移清单；等待审计审查，尚未修改实现。
- 2026-11-26 11:49:41 +0800：按客观复核修订审计：补记内部 Token/Lexer/Parser/Evaluator/Selection；区分局部约束、全局约束和失败原子性；取消 ChangeSet 的预设封装结论；将 ModelAnalysis 封装设为“独立存在仍成立”后的条件任务，并调整实施顺序。
- 2026-11-26 11:56:22 +0800：将审计标准改为逻辑语义优先，重写全类型分类。明确 ChangeRecord/ChangeSet 是完成后不可变的历史事实，ChangeNotification 是发布后不可变消息；区分当前可编辑状态、只读投影和构建期可变对象，并同步当前设计。
- 2026-11-26 12:47:46 +0800：记录“优先 const/immutable，但权衡构建与使用便利性”为核心原则；为 ChangeSet 等逐步构建对象保留 Builder、不可逆 freeze()、私有修改加受限友元等方案，待专项比较，不预设实现。
- 2026-11-26 12:49:42 +0800：架构规则调整为 Core 可使用 QtCore、语义合适时优先 Qt 容器；继续禁止 Qt Widgets/UI 依赖。既有 STL 容器只在相关重构中逐步迁移，不做无收益的全量替换。
- 2026-11-26 13:05:33 +0800：完成 Document 内对象封装实施设计，明确局部/全局约束、私有非 const 方法、公共只读查找、apply/restore 失败原子性和 Workspace 校验；进入本阶段实现。
- 2026-11-26 13:22:05 +0800：记录 find() 返回指针的悬空生命周期风险，以及指针、unique_ptr 引用、稳定句柄/ID、受控访问回调等待比较方案；当前 Document 重构完成后再讨论。
- 2026-11-26 13:45:08 +0800：记录 Document 封装实现、失败原子性测试和 Core profile；Debug 全量通过，Release 的剪贴板场景因系统资源占用待重跑。
- 2026-11-26 14:05:17 +0800：系统剪贴板恢复后，Release 受阻场景及完整 CTest 均通过；Document 封装阶段完成 Release/Debug 全部门禁。
- 2026-11-26 14:09:58 +0800：完成不可变纯语法 AST 实施设计，明确完整节点不变量、部分错误 AST、只读子节点访问、nameKey 求值和 SELF 名称化；进入实现。
- 2026-11-26 15:10:53 +0800：完成不可变 AST、FormulaAnalysis 和按名称求值迁移；新增上下文复用回归，profile-formula 及 Release/Debug 全量回归通过，等待审核。

## 9. Core 封装审计（2026-11-26）

### 9.1 方法：逻辑语义优先

审计首先判断对象在逻辑上代表什么，而不是现有代码是否修改它。现有读写方只用于估算迁移范围，不能作为“应当可变”的依据。分类覆盖全部公共类型以及实现文件内部的 Token、Lexer、Parser、Evaluator 和局部 Selection。

采用三条判别规则：

1. **历史事实、已完成的分析或计算结果**：构造/发布完成后不可变；后续编辑产生新结果，不改写旧事实。
2. **文档的当前可编辑状态**：允许变化，但只能由所有者通过受控操作修改或整体替换；身份字段仍可单独保持不可变。
3. **输入、配置和构建过程**：允许在明确的构建阶段可变，但不能把构建期可变性泄漏到已完成结果。

只读投影不等于 immutable。ElementRange 等对象自身不修改，但其引用的 Document 仍可能变化。业务上可编辑也不等于对象字段必须可变，Document 可以用新的不可变定义值替换旧值。

### 9.2 完成后逻辑上不可变的对象

| 类型 | 逻辑目的 | 不可变边界 |
| --- | --- | --- |
| ChangeRecord | 一个对象从 before 到 after 的历史事实 | 构造完成并进入 ChangeSet 后不可修改；kind 等派生信息在构造时确定 |
| ChangeSet | 一次完整、原子的编辑操作 | 提交给命令/撤销栈后不可增删或改写记录；收集阶段使用独立构建过程 |
| ChangeNotification | 已发生文档变化的一次发布消息 | 发布完成后不可修改，接收方不能更改共享事实 |
| FormulaNode 及全部派生节点 | 一条公式的语法事实 | Parser 创建后，kind、span、名称、运算符和子节点结构均不可变 |
| FormulaAnalysis、FormulaIssue | 一条公式的解析结果及诊断事实 | Parser 返回后不可变；公式变化时创建新结果 |
| ModelAnalysis、ElementAnalysis、MacroAnalysis、DependencyGraph | 某次 Document 内容对应的派生分析结果 | 若独立 Analysis 继续存在，发布后不可变；Document 变化时替换或重新生成，不原地修改 |
| ModelIssue | 一次模型诊断的事实 | 生成后不可变，UI 只能读取和定位 |
| FunctionSignature | 内置函数的静态定义 | 注册后不可变，参数上下限和类别不能分别修改 |
| ArrayValue | 一次计算得到的数组值 | 构建完成后维度、shape、values 不可修改，并由构造保证长度/乘积一致 |
| EvaluationResult | 一次求值的结果或错误 | 返回后不可变，成功值和错误状态在构造时确定 |

SourceSpan 作为 AST 或诊断的组成部分时随所属结果不可变。不可变类型可以使用 mutable Builder 完成构造，但 Builder 不属于已完成结果，也不能在结果发布后保留可修改它的通道。

### 9.3 当前可编辑状态与只读投影

| 类型 | 逻辑目的 | 修改规则 |
| --- | --- | --- |
| Document | 当前完整文档及全部长期状态的所有者 | 是可变聚合根；所有修改经过受控 API，并保证全局一致性 |
| DocumentElement | 文档对象的身份和分类 | id/kind 在对象生命周期内不可变，即使对象的其它属性可编辑 |
| ModelElement、Variable | 当前模型变量及其公式、命名、单位等状态 | 只读公开；由 Document 受控修改，名称和公式索引不得被绕过 |
| Flow、Connector | 当前模型/视图中的连接关系 | 端点可编辑，但只能由 Document 校验并整体更新 |
| Comment | 当前注释内容 | 内容可编辑，但只能由 Document 受控修改 |
| Dimension、DimensionElement | 当前数组维度定义 | 不是历史事实；编辑时可以整体替换定义值，但替换必须经过 Document 的引用和冲突检查 |
| MacroDefinition、MacroParameter | 当前自定义函数定义 | 可以编辑或替换，不因此要求旧定义可被外部逐字段修改 |
| DiagramDefinition、Route、FontStyle、Workspace | 当前视图的布局、样式和工作区状态 | 允许由所有者更新或替换；输入必须校验，存储状态只读暴露 |
| Model | Document 模型变量的只读门面 | 不拥有独立状态，不是不可变模型快照 |
| ElementRange / Iterator | Document 当前元素集合的只读投影 | 不拥有对象；Document 增删时投影失效，不能用“不可变”描述其底层集合 |

Document 内对象不提供公开的逐字段 setter。对象自身的局部约束由私有非 const 方法维护，名称索引、跨元素端点和级联关系等全局约束由 Document 维护。Document 的公开修改入口负责协调两层约束。

封装只阻止外部绕过，不自动保证多字段操作的失败原子性。实施前必须为 apply/restore 等多字段路径明确“先完整校验再提交”或其它可验证的失败语义，并用测试覆盖中途失败不得留下部分更新；不能把访问控制误认为事务保证。

### 9.4 输入值与构建期可变对象

| 类型 | 可变性的逻辑理由与边界 |
| --- | --- |
| ElementData | 用于组装编辑、Capture/Apply/Restore 和复制映射，是可变 DTO；一旦作为 before/after 收入 ChangeRecord，只能按 const 值读取 |
| SourceSpan | 独立构造时是简单值；放入 AST/诊断后随所属结果不可变 |
| Vec2、RoutePoint、FlowEndpoints | 用于组装位置、路径点和端点输入；合法性由接收方校验 |
| Route、FontStyle、Workspace | 可作为编辑输入值组装；写入 Document 后，存储状态只能通过受控替换改变 |
| ArrayElementEquation | 用于组装逐元素方程；写入 ModelElement 前校验下标和互斥关系 |
| EvaluationScope、EvaluationContext | 一次求值的输入配置，由调用方在调用前组装 |
| Token、求值局部 Selection | 单次解析/求值中的短生命周期局部值 |
| Lexer、Parser、Evaluator、Parser/Analyzer/ChangeSet Builder | 逻辑上是构建过程，允许私有可变状态；调用完成后不充当不可变结果的修改通道 |

同一类型可以在输入阶段可变、在不可变结果内部只读，不需要为此立即复制平行类型。关键是所属对象不暴露非 const 访问，而不是仅凭类型关键字判断。无效输入可以存在于 DTO 中，但 Document 必须拒绝把无效值写入长期状态。

### 9.5 存储与角色一致性结论

- ChangeRecord/ChangeSet 的不可变性由历史事实语义直接确定，不再是可选优化。构造时应校验 id、kind、before/after 和操作类型的组合，并优先消除 kind 等可派生重复状态；撤销/重做只能读取同一记录。
- ChangeNotification 虽不是撤销历史，也是发布后不应被接收方修改的消息事实。构建期组装与发布后只读仍需分开。
- ElementData 同时作为可变编辑 DTO 和记录中的状态值时，不需要立即复制出另一套 ElementState；ChangeRecord 私有持有并仅提供 const 访问即可保护历史记录。
- Document 当前另有 `vector<Dimension> + unordered_map<nameKey, size_t>`。由于当前维度不可删除或重命名，下标暂时稳定；但若引入维度编辑，必须先重新设计主存储，不能继续依赖下标迁移。
- Document 的宏定义目前按 vector 顺序保存，findMacro 线性扫描；名称集合只用于冲突判断。宏查询及 MacroAnalysis 的关联方式留到 Analysis 专项统一设计。
- 当前内置函数注册表已经是静态 `unordered_map<std::string, FunctionSignature>`，无需再建立平行函数索引。

### 9.6 分步迁移边界

1. **Document 内对象封装**：先书面明确局部/全局不变量和失败原子性，再处理 ModelElement、Flow、Connector、Comment，收窄非 const find()；保持 ElementData 和现有编辑 API，不改变持久化语义，并补齐 Workspace 等输入校验。
2. **AST 与 FormulaAnalysis 封装**：建立真正不可变的纯语法 AST，移除 ReferenceNode.entityId，并迁移分析/求值的名称查询；宏调用行为不在这一阶段扩展。
3. **ArrayValue 封装**：保护维度、shape 和数据长度不变量，保持现有求值结果不变。
4. **变更事实与结果对象**：实现完成后不可变的 ChangeRecord/ChangeSet 和 ChangeNotification，消除可派生重复状态；同时落实 FormulaIssue、ModelIssue、EvaluationResult 等结果对象的只读边界。
5. **Analysis 专项**：在上述不变量修改完成后，论证 Document 内直接持有 AST 与独立 ModelAnalysis 的方案，再决定需要保留的结果类型及其封装、主存储、宏键、Builder、诊断和失效边界。

每一步都必须先细化本计划和当前设计文档，再实现、补充不变量/生命周期测试、执行适用的同场景 profile、Release 后 Debug 全量回归并等待审核。后续步骤不因上一阶段完成而自动开始。

## 10. Document 内对象封装实施设计（2026-11-26）

### 10.1 范围

本阶段只处理 Document 当前状态对象及其修改边界：ModelElement、Variable、Flow、Connector、Comment、Document 查找和 Workspace 输入校验。ElementData 仍是可变 DTO；不改变 QDS/XMILE、AST、Analysis、ChangeSet、宏调用或容器实现。

### 10.2 局部约束与接口

| 类型 | const 公开访问 | 受控非 const 修改 |
| --- | --- | --- |
| DocumentElement | 既有 id()/kind() | 无；身份永远不变 |
| ModelElement | displayName()、nameKey()、equation()、dimensions()、elementEquations()、unit()、comment() | 私有名称和属性修改方法；公式/维度修改方法维护总体与逐元素公式互斥 |
| Variable | 继承 ModelElement | 不增加公开修改入口 |
| Flow | endpoints() | 私有 setEndpoints()，由 Document 校验后调用 |
| Connector | from()、to() | 私有 setEndpoints()，由 Document 校验后调用 |
| Comment | content() | 私有 setContent()，由 Document 调用 |
| Document | const find(EntityId) 及现有只读门面 | 可变对象仅通过私有 findMutable() 访问；公开语义修改方法维护全局关系 |

ModelElement 的私有修改至少区分名称、整体公式、维度集合、逐元素公式和完整公式数据恢复，避免通过通用字段写入制造中间非法状态。Document 是这些私有方法的唯一协调者；不以广泛 friend 直接写字段代替局部约束方法。

### 10.3 Document 全局约束

Document 继续负责：

- displayName/nameKey 规范化、唯一性及公式引用更新；
- 数组维度存在性、下标完整性、总体/逐元素公式的模型级校验；
- Flow/Connector 端点合法性、自环和重复连接检查；
- Flow 重连后的中心位置更新；
- Route、FontStyle、Workspace 的输入合法性；
- 级联删除、Capture/Apply/Restore 和 ID 单增规则。

本阶段不迁移 Dimension 的下标索引，也不改变宏存储；这些问题已有独立记录。

### 10.4 失败原子性

`apply()` 在任何长期状态修改前完成全部可预期语义校验：元素存在与类型、名称冲突、数组数据、Flow/Connector 端点与重复连接、字体和 Route。校验复用各单独修改操作使用的内部判定，避免 apply 与 rename/reconnect 等接口规则分叉。

校验通过后只执行不会返回语义失败的对象更新。任何语义失败必须保持 Document 的长期内容、名称索引和布局与调用前一致。内存分配异常不在本阶段额外承诺强异常保证。

`restore()` 同样先校验再创建和登记对象。失败时允许消耗一个单调递增 ID，因为 ID 不回退是既定约束，但不得留下对象、名称或布局记录。

`Workspace` 仍是输入值对象；Document::setWorkspace 返回 bool，并在写入前要求 zoom 为正且有限、center 分量有限。无效输入返回 false 且原 Workspace 不变。

### 10.5 验证场景

- 所有实体读取接口与现有编辑、复制、持久化、undo/redo 回归一致。
- 非法改名、数组数据、端点、重复连接、字体和 Route 的 apply 失败时，逐项比较调用前后的完整 ElementData。
- 合法 apply 仍能一次更新全部字段，并保持引用和布局。
- 非法 Workspace 不改变原值，合法 Workspace 正常保存。
- 现有全部单元测试和适用 Core profile 回归；Release 后 Debug 全量构建测试。

### 10.6 本阶段验证记录

- 新增 applyFailureIsAtomic 和 workspaceValidation，直接运行均通过。
- Release 下应用、全部测试及 profile 目标构建成功；CTest 除 editor-commands 中既有 pngExportAndClipboard 外其余通过。该场景单独重跑仍因 Windows OpenClipboard 被后台程序占用而失败，未修改剪贴板逻辑。
- Debug 全部 12 项 CTest 通过。
- `profile-core.exe` 本机记录：create 0.40636 μs/op、connect 43.6458 μs/op、lookup 0.003981 μs/op、name_lookup 0.089897 μs/op、move 57.5518 μs/op、validate 0.31431 μs/op、remove 53.1704 μs/op。仅作同场景记录，不据此声称优化。

系统剪贴板恢复后，pngExportAndClipboard 单独重跑通过，Release 完整 CTest 也达到 12/12；本阶段 Release/Debug 门禁均已通过。

## 11. 不可变纯语法 AST 实施设计（2026-11-26）

### 11.1 范围

本阶段处理 FormulaNode 全部派生节点、FormulaAnalysis，以及因删除 ReferenceNode.entityId 而必须迁移的分析/求值名称查询。保留当前语法、数组语义和既有宏展开行为；统一宏运行期调用属于后续独立阶段，不在本次混入。

### 11.2 AST 不可变接口

- FormulaNode 使用私有 kind/span，只提供 const getter。
- 各派生节点的名称、字面量、运算符和位置均为私有状态。
- 单一子节点通过返回 `const FormulaNode *` 的 getter 访问。
- CallNode、SubscriptNode、ArrayLiteralNode 不公开 `vector<unique_ptr<FormulaNode>>`。只提供元素数量及按位置返回 `const FormulaNode *` 的访问，避免 const unique_ptr::get() 仍泄漏可变裸指针。
- 节点构造完成后没有字段写入、可变子节点或可变容器访问。公式变化时创建并替换整棵 AST。
- FormulaAnalysis 私有持有 AST 和 issues，只提供 root()、issues() 等只读访问；作为值可以整体移动/替换，但不提供修改现有节点的通道。

节点 const 指针只在所属 FormulaAnalysis 存活且未被整体替换时有效。本阶段不改变这一所有权关系，也不使用 shared_ptr 延长节点寿命。

### 11.3 错误与构造不变量

当前 Parser 只在必需子节点均非空时构造 Unary/Binary/Conditional/Range 等节点，也不会把空参数或空下标放入集合。因此所有已创建节点都必须结构完整，不引入可空子节点状态。

语法失败时允许返回带 issues 的部分 AST；空公式允许 root()==nullptr 且无 issues。部分 AST 自身仍满足节点级不变量，调用方不能把它误认为语义完整公式。ArrayLiteral 只在至少已有首项时构造。

### 11.4 按需 Resolve 与求值

- 删除 ReferenceNode.entityId 及 Formula.h 对 Model.h 的依赖。
- 依赖分析通过 Document 的 nameKey 索引直接生成 EntityId 引用，不回写 AST。
- VariableReader 改为接收 nameKey，并由调用方使用当前 Document/值索引查询。
- EvaluationScope 的 self 改为当前元素的 nameKey，SELF 与普通变量共用同一读取入口，避免为了 SELF 保留按 ID 的平行读取通道。
- 内置函数继续使用现有静态哈希注册表；宏仍按 Document 定义查询。任何 EntityId、函数或宏目标都不写入 AST。
- 本阶段保留现有 std 容器迁移范围，不单独进行全量 Qt 容器替换；后续 Analysis 主存储设计优先评估 Qt 容器。

### 11.5 验证与 profile

- 覆盖所有节点 getter、组合节点计数、空公式、语法错误和部分 AST 的结构不变性。
- 同一 AST 在不同名称/值上下文中求值，不携带旧实体绑定；变量、SELF、数组和宏既有回归保持。
- 验证 Document 改名后的重新分析和按名称求值，不需要修改 AST。
- 运行 profile-formula 的 lex/parse、Document 分析、标量/数组/宏求值，记录名称查询纳入求值后的同场景结果。
- Release 后 Debug 全量回归；完成后停止等待审核。

### 11.6 本阶段验证记录

- 新增同一 AST 在不同名称值上下文中的求值回归，并验证节点数和 SourceSpan 不变；静态断言保证集合访问只返回 const FormulaNode 指针。
- 公式、求值、数组、宏专项测试通过；Release 和 Debug 完整 CTest 均为 12/12。
- `profile-formula.exe 1000` 本机结果：lex/parse 2.7208 μs/公式，Document 分析 5.393 μs/公式，标量求值 0.4345 μs/公式，数组分析 1.1064 μs/逐元素公式，数组求值 9.5887 μs/公式，数组初始化列表求值 1.2965 μs/公式，宏分析 11.0405 μs/公式，宏求值 0.6334 μs/公式。结果包含按 nameKey 查询，不据此单独声称优化。
