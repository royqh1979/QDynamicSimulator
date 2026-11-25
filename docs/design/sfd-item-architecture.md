# SFD 核心模型与画布 Item 架构

## 1. 状态与目标

本文记录当前待审查的目标设计，不代表下列重构已经完成。现有实现已有稳定 ID、Document、命令、ChangeNotification、Scene 增量同步、端点/拐点句柄和基础自动布线；高级端口分配和更多路径优化按后续阶段实施。

目标是明确核心元素、画布元素、连接线和控制句柄的类体系，以及核心文档与 QGraphicsScene 的互找和一致性维护方式，避免双向指针、重复可写状态、循环更新和撤销失效。

## 2. 总体原则

采用“核心模型无 Qt + 视图层继承 QGraphicsItem + 算法与几何使用组合”的混合设计：

- 核心文档不包含 QGraphicsItem、QObject 或画布对象指针。
- 实体和连接线作为场景对象时，继承 QGraphicsItem/QGraphicsObject。
- 吸附点默认是几何数据，仅在显示或交互时创建临时句柄。
- 拐点在文档中保存为路径值，仅在选中连接线时显示控制句柄。
- 布线器、吸附算法、空间索引和样式通过组合使用，不与图元继承耦合。
- Document 是唯一事实来源，QGraphicsScene 是可交互投影，不是另一个对等可写模型。
- 双方通过稳定 ID 互找，不保存跨层对象指针，不使用名称或容器下标作为标识。

### 2.1 变更术语

- **ChangeRecord**：一个对象的一条变化，保存 undo/redo 所需的 before/after 内存值。
- **ChangeSet**：一次原子操作包含的全部 ChangeRecord，由命令持有，用于 undo/redo；不是整个 Document 或 Scene 的副本。
- **ChangeNotification**：Document 修改成功后发给 Scene 的差异通知，只负责同步，不进入撤销栈。
- **Recovery Copy**：自动保存到磁盘的文档恢复副本，与上述内存变更结构无关。

本文不使用含义过宽的“快照”描述 undo/redo 数据，也不把 ChangeNotification 当作撤销记录。

## 3. 文档模型

### 3.1 Document 组成

```text
Document
├── core::Model                  可计算、可仿真的语义变量
│   ├── Stock                    Vensim Level / XMILE stock
│   ├── Flow                     Vensim Rate / XMILE flow
│   ├── Auxiliary                XMILE aux
│   ├── Constant                 变量分类，XMILE 使用 aux
│   └── Data（数据）             变量分类，XMILE 使用 aux
└── DiagramDefinition           当前视图的图形对象与布局
    ├── Connector                Vensim Arrow / XMILE connector
    ├── Cloud                    流量源/汇 / XMILE view cloud
    ├── Comment                  Vensim Sketch Comment / XMILE text_box
    ├── Route                    Flow 与 Connector 的路径
    └── 位置、端口和其他视图状态
```

| 对象 | 在 Document | 在 Model | 在 DiagramDefinition | 说明 |
| --- | :---: | :---: | :---: | --- |
| Stock、Flow、Auxiliary、Constant、Data | 是 | 是 | 只保存其布局 | 具有名称和方程等模型语义 |
| Arrow / Connector | 是 | 否 | 是 | 保存 from/to 和 Route；不是可计算变量 |
| Cloud | 是 | 否 | 是 | 只表示流量的源/汇 |
| Comment / Annotation | 是 | 否 | 是 | 只用于展示；作为 Arrow Junction 时可接 Connector |

因此，“不属于 Model”不等于“不属于 Document”。Arrow、Cloud、Comment 都由 Document 持有，参与统一 ID、撤销/重做和持久化，但不进入仿真变量索引。

- `core::Model` 供仿真引擎使用，不包含 Cloud、Comment 或 Connector。
- `DiagramDefinition` 描述 SFD 布局及视图对象。
- Flow 是 Model 中的实体，不是 Connector；FlowEndpoints 只是该实体的源/目标属性。
- 两者及所有画布图元共用同一个 `EntityId` 空间。
- Flow 端点可以是 Model 中的 Stock 或 DiagramDefinition 中的 Cloud，端点合法性由 Document 聚合层校验。
- 选中、悬停、拖动预览等临时状态只存在 UI。
- 当前缩放和滚动位置属于工作区状态，不属于模型语义。

### 3.2 稳定标识

```cpp
using EntityId = std::uint64_t;
using PortId = std::uint32_t;
```

实体、流量和连接共用同一个 EntityId 空间和同一个 ID 生成器，不再设置 ConnectionId 或独立连接 ID 序列。ID 由文档统一分配，在对象整个生命周期内不变。禁止以下做法：

- 使用变量名作为永久 ID。
- 使用 vector 下标作为外部引用。
- 在模型与视图之间保存裸指针。
- 依赖 Item 地址重建连接或撤销操作。

复制粘贴和跨文档合并时重新分配 ID，并通过“旧 ID→新 ID”映射重建内部连接。

ChangeSet 不需要 UUID，也不经过 XMILE save/load。EntityId 保持 uint64；生成器在 Document 生命周期内始终单增，删除、undo/redo 都不得使其回退，也不根据当前存活对象最大 ID 重算。普通 XMILE 打开、复制和跨文档粘贴均分配新 ID；命令通过 ChangeSet 中的 before/after 内存值恢复原 ID。未来若出现跨文档分布式合并或离线协同时，再评估 UUID；它不替代端点映射、级联恢复或复制时分配新 ID 的规则。

### 3.3 DocumentElement 基类与对象分类

所有文档对象继承同一个不依赖 Qt 的核心基类。基类只提供身份和对象种类，不把名称、方程、位置或路径强加给所有对象：

```cpp
enum class ElementKind {
    Stock, Flow, Auxiliary, Constant, Data,
    Connector, Cloud, Comment
};

class DocumentElement {
public:
    virtual ~DocumentElement() = default;
    EntityId id() const;
    ElementKind kind() const;
};

class ModelElement : public DocumentElement {
    // 语义变量共有的名称、方程、单位和说明。
};

class ViewElement : public DocumentElement {
    // Cloud、Comment、Connector 等视图对象。
};

class Flow final : public ModelElement {
    FlowEndpoints endpoints;
};

class Connector final : public ViewElement {
    Endpoint from;
    Endpoint to;
};
```

Stock、Auxiliary、Constant、Data 可继续共用 `Variable : ModelElement`，由 kind() 区分；Flow 因具有端点和独立路径语义而拆分。Cloud 和 Comment 继承 ViewElement，不是 Variable，也不进入仿真变量集合。

`Route`、`Vec2`、`Endpoint` 和 `PortGeometry` 仍是值对象，不需要继承 DocumentElement。选中状态和控制句柄也不是 DocumentElement。

### 3.4 统一注册与类型访问

Document 提供唯一通用查找接口：

```cpp
DocumentElement *Document::find(EntityId id);
const DocumentElement *Document::find(EntityId id) const;
DiagramItem *DiagramScene::item(EntityId id) const;
```

不提供 findFlowById、findConnectorById、findCloudById 等平行接口。调用方首先通过统一接口取得对象；只有确实需要类型专属能力时，才根据 kind() 使用 Flow、Connector 等接口。id() 只负责身份，kind() 只负责对象分类，两者不承担对方职责。

所有权与索引规则：

- Document 使用 `unordered_map<EntityId, unique_ptr<DocumentElement>>` 唯一拥有全部核心对象；ID 查找与所有权使用同一个容器，不再维护非拥有注册表。
- Model 和 DiagramDefinition 只提供按 ElementKind 过滤的非拥有视图，不持有对象所有权，也不保存对象副本。
- 添加、删除、清空和恢复只能经过 Document API，不允许外部直接修改元素容器。
- Scene 的 `QHash<EntityId, DiagramItem *>` 是画布投影索引，与 Document 使用同一 ID，但不拥有核心对象。

统一迭代采用只读范围范式：

```cpp
for (const DocumentElement &element : document.elements()) { ... }
for (const ModelElement &element : document.model().elements()) { ... }
for (const ViewElement &element : document.diagram().elements()) { ... }
```

三个范围都提供一致的 `begin()/end()`、`empty()` 和迭代器只读访问。Model/DiagramDefinition 的范围只是 Document 元素范围的过滤投影，不重新复制对象。迭代期间不得添加或删除元素；编辑命令先收集 EntityId，再执行修改，避免迭代器失效。

### 3.5 名称、nameKey 索引与重名判断

Stock、Flow、Auxiliary、Constant、Data 都可能进入公式，因此全部参与当前模型的语义变量命名空间。Cloud、Comment 和 Connector 不进入公式，不需要唯一名称，也不参与名称到对象的查询：Cloud 和 Connector 只使用 EntityId，Comment 另保存用户正文 `content`，不能把正文当作名称。

每个语义变量同时保存：

- `displayName`：保留用户输入的大小写、空格和 UTF-8 字符，用于画布、属性面板和 XMILE 保存。
- `nameKey`：由 displayName 派生，只用于名称索引、重名判断和方程引用解析；不作为显示文本，也不独立持久化，加载时重新计算。
- 方程保留用户可读的名称形式，名称解析器将方程标识符转换为 nameKey 后查找同一 ModelElement；改名命令负责同步更新引用，不能只改声明。

内部规范化采用简单、稳定的项目规则：去除首尾空白；ASCII A-Z 转为小写；内部空白与下划线折叠为单个下划线；其他 UTF-8 内容保持不变。核心只使用纯 C++ 实现该规则，不引入 ICU，也不宣称内部规则等同于完整 XMILE Unicode 规则。

名称必须支持中文及其他合法 UTF-8 字符。displayName 和 nameKey 均使用有效 UTF-8；规范化只能按 ASCII 字节和已识别空白处理，不能逐字节修改多字节字符。方程词法分析、名称索引、搜索、复制和 XMILE 读写都使用同一 UTF-8 约定，中文不需要转拼音或英文别名。

XMILE 3.2.2 的完整等价规则由 I/O 导入适配器处理，包括 Unicode 大小写、非断行空格和引号形式。导入器生成内部 nameKey 时，必须同步改写变量声明、stock inflow/outflow、connector from/to 和方程引用；只修改变量名会产生悬空引用。完整 Unicode/UCA 处理如需 ICU，只允许进入 I/O 适配层，不进入纯 C++ 核心。

Document 集中维护唯一的名称索引：

```cpp
std::unordered_map<std::string, EntityId> m_nameIndex; // nameKey -> ModelElement
```

由于 nameKey 在当前模型内唯一，索引值是单个 EntityId，不使用 multimap。`findByName` 先规范化输入再执行平均 O(1) 查找；自动命名也通过该索引检查候选名称，不遍历全部变量。该索引是语义命名和公式解析所需的基础索引，不属于“经过 profile 后才引入”的 Stock 流入/流出派生索引。

索引与对象所有权一样只能由 Document 修改。创建、改名、删除、restore、clear 和文档重置必须同步维护；改名先检查目标键，再原子替换旧键和新键，失败时保留原对象和索引。撤销/重做恢复修改前的 displayName 并恢复对应索引，不重新生成名称。

规则：

- 创建、改名、粘贴和 XMILE 加载都使用同一个 `IdentifierNormalizer` 与 nameKey 索引。
- `nameKey` 为空或与同模型内其他 ModelElement 冲突时拒绝创建/改名；改名对象自身不视为冲突。
- 只改变大小写或等价显示形式且 nameKey 不变时允许，并保持 ID 不变。
- XMILE 加载遇到冲突应报错并保留原 Document，不得静默改名，否则方程引用会改变含义。
- 后续引入子模型时，唯一性作用域扩展为 XMILE 定义的模型/whole-model 命名空间，不把当前单模型规则硬编码成全局假设。

验收至少覆盖：`Flow` 与 `flow` 冲突、`birth_rate` 与 `birth rate` 冲突、连续空白、仅大小写改名、撤销恢复、索引与对象同步、XMILE 冲突加载失败，以及 Cloud/Comment 与变量同名不冲突。中文场景覆盖创建、重名、改名、方程引用、搜索、复制、撤销和 XMILE 往返，例如 `库存`、`流入 速率` 与 `流入_速率`。

### 3.6 默认名称与序列化 uid

每个语义变量类型使用独立、单调递增的命名计数器，生成 `Stock1`、`Flow1`、`Auxiliary1` 等默认 displayName。命名计数器不是 EntityId，也不表示对象身份；删除对象不回收编号，编号允许不连续。

候选名称仍需查询 nameKey 索引，因为用户改名、XMILE 导入和粘贴都可能提前占用某个候选名称。若冲突则继续递增，直到取得未使用的名称；无需扫描全部对象。加载文档后计数器可从 1 开始并跳过已占用名称。undo/redo 恢复原 displayName，不重新运行自动命名。

XMILE `uid` 只是文件内显示对象的引用标识，不替代内部 EntityId。XMILE 1.1 第 5.1.3 节规定 uid 在一次文件内唯一且线性递增，但不要求跨多次读写稳定。保存时可生成文件内 uid，加载时建立“uid → 新 EntityId”映射；不得把 uid 当作撤销身份或跨文档永久身份。

标准 `<text_box>` 等显示对象在标准允许且需要被引用时使用 uid。XMILE 1.1 没有标准 `<cloud>` 标签，Cloud 的位置和 Flow 对 Cloud 的引用只完整保存在原生 QDS `document.xml` 中，不使用标准 XMILE Cloud 或 ZIP workspace。Stock 的 `<inflow>`/`<outflow>` 始终引用 Flow 名称，不引用 Cloud uid。

## 4. 画布 Item 类体系

所有文档图元继承 `DiagramItem : QGraphicsObject`，基类提供纯虚 `EntityId id() const`。VariableItem 与 RoutedItem 不再为了读取 ID 互相转换；RoutedItem 只增加路径几何与端点交互。控制句柄不是文档图元，不继承 DiagramItem。

共同基类使用 QGraphicsObject 是为了保留 VariableItem 信号并避免 QObject 多继承；连接和流量的额外对象开销纳入统一 Item profile。

### 4.1 实体 Item

实体需要选择、移动、信号和属性交互，继承 QGraphicsObject：

```text
QGraphicsObject
└── DiagramItem
    ├── VariableItem           Model 中非路由语义变量的投影
    ├── ViewObjectItem         Cloud / Comment 的投影
    └── RoutedItem
        ├── ArrowItem          DiagramDefinition::Connector 的投影
        └── FlowItem           Model::Flow 的投影
```

`DiagramItem` 的公共职责：

- 保存 `EntityId`，不保存 Entity 指针。
- 处理选择、悬停、拖动和错误状态。
- 显示名称及必要的编辑状态。
- 通过独立 `center()` 虚函数提供几何中心，默认等于 `pos()`；FlowItem 返回路径中点作为阀门中心，RoutedItem 不使用 `setPos()` 移动。
- 提供端口布局和边界交点查询；所有连接都以 center 为定位参考，但实际端点是路径与实体边界的交点。
- 选中路由对象时，端点句柄直接放在路径首尾边界交点，拖动句柄只重连，不修改实体中心。
- RoutedItem 通过统一 `updateLayout()` 声明旧/新几何失效；boundingRect 必须联合 `geometryPath().boundingRect()` 与 `controlPointRect()`，并覆盖阀门、文字和辅助线，确保远离曲线实际控制点的手柄也纳入失效区域。创建连接与端点拖动使用不同预览对象，取消、重连和文档重置时按各自生命周期清理，避免残影。
- 根据文档变化刷新显示缓存。

子类只实现真正不同的外形、端口和交互。Constant、Data 与 Auxiliary 如果行为一致，可共用 VariableItem，并通过绘制策略区分外观。

不采用“DiagramItem 包含一个 QGraphicsItem 属性”的适配方式，因为这仍需转发场景坐标、选择、事件、绘制、所有权和变换，没有实际收益。

### 4.2 连接线 Item

```text
DiagramItem
└── RoutedItem
    ├── ArrowItem
    └── FlowItem
```

ArrowItem 与 FlowItem 都保存 EntityId 并复用 RoutedItem 的路径能力。FlowItem 直接对应流量实体，不生成物料 Connector。RoutedItem 负责：

- 根据端点和 Route 生成 QPainterPath。
- 通过虚函数 `linePaths()` 提供实线路径：ArrowItem 使用基类的一条路径，FlowItem 返回两条平行偏移路径；Flow 使用 16 像素箭头并在末端截断一个箭头长度，避免双线进入箭头。绘制、命中和箭头仍由基类统一负责。
- 绘制线、箭头、选中状态和标签。
- 实现比实际线更宽的 shape()，方便鼠标命中。
- 响应端点移动和路径变化。
- 管理选中时显示的控制句柄。

连接线固定在实体上方、控制句柄下方。贝塞尔辅助线及控制句柄仅在单独选中一条曲线时显示，多选和区域选择过程中隐藏。左键从空白或不可移动 RoutedItem 开始时都进入同一个 area-selecting 状态，不能只在空白按下时设置；释放后清除该状态、按最终选择同步句柄并刷新最后一次框选区域。路径预览必须先调用 `prepareGeometryChange()` 通知旧区域，再替换路由数据，确保拖动时旧轨迹被清除。

它不直接修改核心连接关系，不承担自动避障、吸附决策和撤销命令职责。Flow 默认使用直线；Arrow 默认使用带两个自动相对控制点的贝塞尔曲线，也可显式选择直线或直角自动布线。

Connector 的端点合法性由 Document 聚合层统一校验，create、restore、reconnect 共用同一规则：Comment 只能作为一端 Junction，禁止 Comment→Comment。ArrowItem 在普通端点间绘制单实线，在 Comment 与另一端之间绘制虚线；该显示规则不改变 Connector 的因果或展示语义。

画布连接工具与端点句柄共用 `connectionEndpointAt()` 命中规则：按场景叠放顺序选择普通实体或 FlowItem，排除 ArrowItem 和控制句柄；普通实体拖动仍使用独立的 `nodeAt()`，不因 Flow 可连接而改变 RoutedItem 的选择/框选行为。命中只产生候选 EntityId，具体组合仍交由 Document 校验，Cloud、Comment→Comment、重复连接等非法操作不会创建命令。橡皮线和路径布局都以端点的 `center()` 为参考，因此 Flow 作为源或目标时使用阀门中心而不是 Item 的 `pos()`。

连接创建和端点重连拖动期间，Scene 用独立 `m_endpointTarget` 跟踪当前命中项。DiagramItem 的 endpoint-target 状态与 `isSelected()` 无关：选中保持蓝色实线，命中提示使用橙色虚线，Flow 的管线和阀门采用同一提示。命中变化只调用 Item `update()`，不修改选择集合或 Document；移到空白、切换目标、释放、右键/工具切换取消及文档重置时清除。`DiagramPalette.h` 集中保存选中、命中、实体、路径、文字、句柄和画布颜色常量，后续引入参数化主题时只替换这一入口，不在各绘制函数散布颜色字面值。

### 4.2.1 Flow 实体与路径

Flow 是 ModelElement，直接持有 FlowEndpoints，不是 Connector：

```cpp
struct FlowEndpoints {
    EntityId from = 0;
    EntityId to = 0;
};

class Flow final : public ModelElement {
public:
    const FlowEndpoints &endpoints() const;
};
```

稳定文档中的 Flow 端点只能是 Stock 或 Cloud，不使用 EntityId 0 表示画布上的空白端。Rate 工具允许从任意空白位置开始并在任意空白位置结束；释放时，每个空白端都创建一个真正的 Cloud，已有 Cloud 则直接复用。由此支持 Stock→Stock、Stock→Cloud、Cloud→Stock 和 Cloud→Cloud，不需要增加“隐式端点”类型。“隐式 Cloud”只描述由工具自动创建的交互方式，提交后仍是普通 Cloud。

创建 Cloud、Flow、位置和 Route 必须属于同一个 ChangeSet；Esc、右键取消或创建失败时不留下任何对象。首版中自动创建的 Cloud 提交后与其他 Cloud 采用相同生命周期，删除 Flow 时不擅自级联删除 Cloud，避免误删共享或用户继续使用的云团；删除 Stock/Cloud 时仍必须级联删除关联 Flow。若以后需要由 Flow 独占并回收 Cloud，必须先明确所有权，不能仅按当前连接数猜测。

XMILE 中未被任何 Stock 引用的 Flow 仍是语义变量，导入时不得丢弃；但它不能成为编辑器中不可见的模型对象。若原文件没有对应视图或可用路径，导入器应补建默认 Flow 图形，并为开放端补建 Cloud，因此未被 Stock 引用的 Flow 可表示为 Cloud→Cloud。EntityId 0 只允许出现在导入解析的临时数据中，提交完整 Document 前必须解析为有效 Stock/Cloud；稳定文档不保存 `{0, 0}` 或半连接 Flow。

所有具有名称、方程或初值的语义变量都必须在当前单视图中具有可见投影。XMILE 的 model 变量定义与 view 显示对象仍是两个层次；“必须显示”通过导入时补齐视图实现，不能通过删除没有 view 的 ModelElement 实现。缺少整个 view 时采用确定性自动布局；只缺少部分变量时把缺失变量补入当前视图，避免公式静默消失。

Flow.from/to 是唯一权威关系。当前不引入 Stock 流入/流出派生索引，查询直接遍历 Flow；只有 profile 证明存在性能瓶颈时，才在 Document/Model 层统一增加集中式派生索引，不把索引分散保存在 Stock 对象中。nameKey 索引是另一项基础语义索引，按第 3.5 节始终维护。

所有 RoutedItem 的 Route 按统一 EntityId 存入 DiagramDefinition。Flow 只接受 Straight/Bezier，默认 Straight，不运行直角自动避障；Arrow 默认由 `defaultConnectorRoute()` 生成 Bezier 路径，可显式选择 Straight 或 Orthogonal。

RoutePoint 使用相对坐标：`relative.x` 是沿源→目标方向的比例，`relative.y` 是沿路径法线的像素偏移，场景坐标仅在绘制和拖动时派生。`automatic=true` 表示自动布线点，`false` 表示用户手动点。自动贝塞尔点与手动点一样显示控制句柄和辅助线并可编辑；任意控制点新增、移动或删除时，整条曲线的全部控制点成组转为手动。正交自动布线点仍隐藏。QDS 同时保存相对值和 automatic 属性；恢复未手动编辑的贝塞尔 Connector 时按当前 `defaultConnectorRoute()` 重建派生点，保留 locked，手动路径原样恢复。

端点拖动预览直接由原 RoutedItem 计算，不复制图元、不隐藏原图元，也不修改 Document。预览端点可位于空白或非法目标上，此时不校验端点约束；释放后才校验并提交，非法释放恢复原路径。预览期间保留两个端点句柄，隐藏全部中间控制句柄和贝塞尔辅助线；统一由 `syncControlPoints()` 在开始、有效提交和取消时切换可见性并恢复选中曲线的句柄，不能只更新预览路径。

删除 Stock/Cloud 时必须级联删除所有以其为端点的 Flow 及相关 Arrow。Flow 和 Arrow 只有在两端对象一并复制时才可粘贴，不允许保留外部端点。

### 4.2.2 阀门几何

FlowItem 的阀门中心位于路径中点，属于派生几何，不另存可能冲突的坐标。设中点为 C、单位切线为 T、单位法线为 `N = (-T.y, T.x)`，两个相对三角形为：

```text
C + 12N - 10T，C，C + 12N + 10T
C - 12N - 10T，C，C - 12N + 10T
```

阀门对称轴沿法线，始终垂直于路径切线，底边与切线平行。切线由中点两侧采样计算，覆盖水平、垂直、斜线和贝塞尔曲线；路径或切线退化时不绘制阀门，避免除零和 NaN。后续支持阀门偏移时应增加明确的路径参数，不创建独立阀门实体。

FlowItem 的名称使用独立 `labelRect()` 计算，`boundingRect()` 必须同时包含路径、阀门和完整名称区域。路径或端点变化时通过 `prepareGeometryChange()` 同时作废旧路径和旧名称区域，不能只扩大路径边界，否则移动关联实体会留下文字残影或裁切名称。

### 4.2.3 Flow 的 XMILE 边界

目标版本为 XMILE 1.1，仅用于未压缩 XML 的导入/导出。Flow 名称和方程写入标准 `<flow>`，Stock 使用 `<inflow>`/`<outflow>` 引用 Flow；不生成物料 Connector。Constant/Data 在标准 XMILE 中写为 aux，普通 aux 导入为 Auxiliary。XMILE 1.1 没有标准 `<cloud>`，不得输出该标签冒充标准内容。

导入纯 XMILE 时，Stock 的 inflow/outflow 决定 Flow 语义端点，Stock 位置决定已知端点布局；`x,y`、`pts`、`angle` 只作为布局参考。Flow 的 `x,y` 是阀门参考中心，Connector 的 `x,y` 只是通用显示位置，均不得变成 Route 固定点。`pts` 首尾可用于估计缺失 Cloud 的位置，中间点不得直接写成固定控制点。导入后 Route.manual=false，路径由端点自动重算；Arrow 可包含默认贝塞尔的 automatic 相对点，但不得包含 XMILE 派生的手动固定点。

没有视图或路径时生成确定性的默认布局。名称、端点、视图补齐和索引建立必须属于同一事务，失败时保留原 Document。导出不得把贝塞尔控制手柄冒充标准锚点，也不得声称保留 Cloud 图形、Constant/Data 分类、Comment Connector、私有路径和工作区语义。

真实互操作验证首先使用 `3rdparty/test-models/samples/SIR/SIR.xmile`，要求全部 Flow/Connector 端点有效、无孤立实体、无固定 Route 点。CMake 选项 `QDS_ENABLE_XMILE_CORPUS_TESTS` 默认 OFF；修改 XMILE 导入功能时显式开启，遍历 `3rdparty/test-models` 下全部 XMILE 文件执行同族结构验证，不得静默跳过文件。多模型/模块及多 Stock 同向端点必须明确报不支持，禁止部分导入；损坏 XML 必须明确拒绝，并在测试输出中分类记录。公式引用变量的孤立判断暂缓并在测试输出中列出，待公式依赖分析实现后恢复；这些文件仍验证导入、端点和 Route。

### 4.2.4 原生 QDS 单 XML 格式

I/O 职责按格式拆分为独立类和源码文件：`DocumentFile` 负责 QDS/ZIP，`XmileFile` 只负责未压缩 XMILE 导入/导出，不共享格式兼容层。原生文件使用 `.qds` 后缀，ZIP 容器中只有一个 `document.xml`。XML 根元素为 `<qds-document version="1">`，内部以 `<model>`、`<diagram>` 和可选 `<workspace>` 分区；代码模型与视图分层不要求拆成多个文件。开发阶段不读取旧版 model.xmile/workspace.xml ZIP，也不为其设置兼容层。

`model` 保存 Stock、Flow、Auxiliary、Constant、Data 及方程、单位、说明和 Flow 端点；`diagram` 保存 Cloud、Comment、Connector、位置和 Route。所有对象使用文件内 uid 引用，加载时重新分配 EntityId 并建立 uid→EntityId 映射。displayName 持久化，nameKey 加载时派生；Comment 保存 content，Cloud 不使用名称。

原生读取采用两阶段构建：先建立非路由对象，再按映射建立 Flow、Connector。原生往返必须完整保留 ElementKind、重复 Comment、Cloud、端点、路径模式、控制点和锁定状态。ZIP 中缺少 document.xml、XML 损坏或引用无效时加载失败并保留原 Document。XMILE 导入通道遇到 ZIP 直接报错，不尝试猜测旧格式。

### 4.3 吸附点和端口

逻辑端口是几何数据，不继承 QGraphicsItem：

```cpp
struct PortGeometry {
    PortId id;
    Vec2 localPosition;
    PortDirection direction;
    ConnectionCapability capability;
};
```

由 PortLayout 计算端口集合和边界交点。只有悬停、连线或编辑时才创建可见句柄：

```text
QGraphicsObject
└── PortHandle
```

PortHandle 负责显示、接收鼠标事件并转发给 ArrowEditor。使用 `ItemIgnoresTransformations` 保持屏幕大小恒定。不要为每个实体的所有端口永久创建 Item。

### 4.4 拐点和控制句柄

拐点保存在 `Route::controlPoints` 中。只有连接线被选中时，才创建对应控制句柄：

```text
QGraphicsObject
└── ControlPointHandle
```

句柄只保存统一 EntityId、控制点索引、场景坐标和控制点角色，不再携带 Connector/Flow 所有者类型。折点、贝塞尔锚点和贝塞尔手柄通过角色区分。

不为所有连接线的全部拐点永久创建 Item。句柄可作为位于场景原点、使用场景坐标的 ArrowItem 子 Item；其他情况由 ConnectionEditingController 管理为独立覆盖层 Item。

## 5. 互找机制

### 5.1 文档索引

Document 对所有对象只提供统一按 ID 查询：

```cpp
DocumentElement *find(EntityId id);
const DocumentElement *find(EntityId id) const;
```

内部使用 `unordered_map<EntityId, unique_ptr<DocumentElement>>` 同时承担所有权和统一查找。Model 与 DiagramDefinition 不再拥有对象，只提供过滤范围；添加、删除、清空和恢复由 Document 原子完成。

### 5.2 Scene 索引

```cpp
class DiagramScene : public QGraphicsScene {
public:
    DiagramItem *item(EntityId id) const;

private:
    QHash<EntityId, DiagramItem *> m_items;
};
```

### 5.3 双向查找流程

```text
核心 ID → 画布对象
DocumentController → DiagramScene::item(id)

画布对象 → 核心对象
DiagramItem::id() → Document 按统一 ID 查询
```

Item 使用具名 ID 成员，不依赖 `QGraphicsItem::data()` 中的无类型数据。

## 6. 一致性维护

### 6.1 单向修改流程

所有文档修改使用同一路径：

```text
用户操作
   ↓
Item / Handle 产生交互请求
   ↓
DiagramController / ArrowEditor
   ↓
QUndoCommand
   ↓
DocumentController 修改 Document
   ↓
产生 ChangeNotification
   ↓
DiagramScene 按 ChangeNotification 增量更新 Item
```

约束：

- DiagramItem 不直接修改名称、方程或持久化位置。
- ArrowItem 不直接修改连接关系。
- ControlPointHandle 不直接修改 Route。
- Scene 不能绕过命令体系写文档。
- 核心文档不直接调用 Qt 信号或操作 Item。

### 6.2 ChangeNotification

核心保持纯 C++，Qt 侧 DocumentController 在修改成功后发出变化通知：

```cpp
struct ChangeNotification {
    std::vector<EntityId> added;
    std::vector<EntityId> updated;
    std::vector<EntityId> removed;
    bool documentReset = false;
};
```

`DiagramScene::apply(ChangeNotification)` 不再按对象类别使用平行通道：

1. 按 removed 中由 Document 计算的依赖顺序删除图元：先控制句柄和 RoutedItem，后端点图元；每项只通过统一 Scene 索引查找。
2. added 先按“非路由对象→Flow→Connector”排序，再调用 Document::find(id) 创建图元；不能依赖 unordered_map 或通知原始顺序，因为 Flow 端点图元必须先存在，Connector 又可能以 Flow 为端点。
3. 对 updated 调用 Document::find(id) 并刷新已有图元。
4. 按依赖顺序刷新受影响端口和路径：先更新直接连接移动对象的 Flow，再更新直接连接移动对象或这些 Flow 的 Arrow，最后同步控制句柄。不能只刷新直接连接移动对象的 Arrow，否则 Stock→Flow→Arrow 链上的 Arrow 会使用旧阀门中心。

打开文件或整体替换模型时使用 `documentReset` 并完整重建 Scene；普通编辑不做全场景重建。

### 6.3 拖动预览与提交

拖动元素、端点或拐点时使用“预览 + 提交”：

- 拖动中仅更新 Item、相关连接预览和吸附参考线。
- 不逐像素写文档，不逐像素创建撤销命令。
- 鼠标释放时生成一条命令，一次性提交最终值。
- 操作被取消时，根据文档原值恢复 Item。

当前 `VariableItem::itemChange()` 直接逐次写核心模型的实现，应在重构时移除。

### 6.4 撤销与重做

命令持有一个 ChangeSet，ChangeSet 由本次原子操作的全部 ChangeRecord 组成，不保存对象指针：

```cpp
struct ChangeRecord {
    EntityId id;
    ElementKind kind;
    std::optional<ElementData> before;
    std::optional<ElementData> after;
};

class DocumentCommand : public QUndoCommand {
    ChangeSet m_changes;
};
```

- redo() 应用各记录的 after，undo() 应用 before。
- 创建、修改、删除可使用同一结构表达；删除时 after 表示不存在，创建时 before 表示不存在。
- 属性修改只保存受影响字段，结构删除保存被删除对象及级联对象所需的完整内存值。
- 两者均通过 DocumentController 并产生 ChangeNotification。
- Scene 不区分普通编辑、撤销和重做。
- 复合编辑把全部 ChangeRecord 收入同一个 ChangeSet，作为一次撤销单元。

## 7. 生命周期与所有权

### 7.1 两层各自拥有对象

| 层 | 拥有者 | 所有权形式 | 非拥有索引/引用 |
| --- | --- | --- | --- |
| 全部核心对象 | Document | `unordered_map<EntityId, unique_ptr<DocumentElement>>` | Model/DiagramDefinition 的过滤范围 |
| 画布图元 | QGraphicsScene | `addItem()` 接管 DiagramItem* | Scene 的 ID 索引、RoutedItem 的端点 Item* |
| 临时控制句柄 | QGraphicsScene | `addItem()` 接管 | Scene 当前句柄列表 |

Document 是唯一核心所有者，Model 与 DiagramDefinition 只是分类视图。Scene 索引不拥有图元。命令不得保存已删除对象指针；核心对象不得保存 QGraphicsItem/QObject；DocumentElement 的地址只在对象存活期间有效，查找结果不得缓存到删除之后。

### 7.2 创建

正常创建必须先提交核心对象，再同步画布：

```text
用户操作
→ DocumentController 创建命令
→ Document 分配统一 EntityId
→ Document 以 unique_ptr<DocumentElement> 创建并拥有对象
→ Document 统一容器完成 ID 与所有权登记
→ 提交核心修改并发出 ChangeNotification::added*
→ DiagramScene 创建对应 DiagramItem
→ QGraphicsScene::addItem() 接管图元
→ Scene 统一 ID 索引登记非拥有指针
```

依赖其他对象的 Connector/Flow 必须后于其端点对象创建。核心注册失败时回滚对象和 ID 注册，不得发出成功的 ChangeNotification；Scene 不同步半成品。

### 7.3 删除与级联删除

核心删除由 Document 统一计算完整的级联集合。删除 Stock 或 Cloud 时，必须同时包含所有以它为源或目标的 Flow、相关 Connector 及其 Route，不允许留下半条流量线。核心从统一容器移除并销毁对应 unique_ptr 对象，然后发出包含全部 ID 的 ChangeNotification。

Scene 按依赖顺序处理该 ChangeNotification：

```text
先隐藏并清理相关控制句柄
→ 删除相关 ArrowItem / FlowItem
→ 从 Scene 统一索引移除对应 ID
→ 删除端点 VariableItem / ViewObjectItem 并移除索引
→ QGraphicsScene 随图元析构释放对象
```

RoutedItem 的端点指针是非拥有引用，因此必须先删除或重绑相关 RoutedItem，再删除端点图元。任何一层都不得直接删除另一层对象。

### 7.4 撤销与重做

- 删除命令在核心对象仍存在时建立 ChangeSet：每条 ChangeRecord 保存类型、ID、名称/方程、端点、位置、Route 及级联对象的 before 值。
- 删除后，原核心指针和原图元指针立即失效，命令不得继续持有或复用它们。
- 撤销根据 ChangeSet 的 before 值重新创建核心对象并恢复相同 ID；ChangeNotification 驱动 Scene 创建全新的 DiagramItem。
- 重走删除时再次销毁本次创建的对象；ID 稳定不等于对象地址稳定。
- 复合操作作为一个撤销单元，所有 ChangeRecord 在首个对象删除前取得。

### 7.5 文档重置

打开文件或新建文档时：

```text
DocumentController 构造完整的新 Document
→ 旧 Document 析构并销毁全部核心对象，旧 ID 容器和分类范围随之失效
→ 发出 documentReset
→ Scene 先清除临时句柄
→ QGraphicsScene::clear() 销毁全部图元
→ Scene 清空统一 ID 索引
→ 按新 Document 创建全部图元并重建索引
```

不得让旧 Document ID 容器、旧 Scene 索引、控制句柄或命令引用跨文档重置继续存活。

### 7.6 复制粘贴

- 为所有粘贴对象分配新 ID。
- 建立旧 ID 到新 ID 的映射。
- 按映射重建内部 Flow/Connector 端点。
- Flow 与 Connector 都只有在其两端对象一并复制时才可粘贴；内部端点通过旧 ID→新 ID 映射重建。
- 不允许单独复制 Flow 并继续引用原端点，也不允许跨文档保留外部端点。
- 核心对象和 Scene 图元分别按上述创建流程生成，不复制任何跨层指针。

QPointer 不能替代上述所有权、删除顺序和索引规则。

## 8. 算法与控制器的组合

自动布线使用独立策略接口：

```cpp
class ConnectionRouter {
public:
    virtual ~ConnectionRouter() = default;
    virtual Route route(
        const EndpointGeometry &from,
        const EndpointGeometry &to,
        const ObstacleIndex &obstacles,
        const Route &previousRoute) const = 0;
};
```

具体策略包括 StraightRouter、OrthogonalRouter 和 BezierRouter。ArrowEditor 通过组合使用 ConnectionRouter、SnapService 和 ObstacleIndex。

SnapService 统一处理网格、端口、元素边界、对齐参考线和现有路径吸附，返回 SnapResult，不负责创建图元。

一次拖动拐点的流程：

```text
ControlPointHandle
→ ArrowEditor 计算候选位置
→ SnapService 执行吸附
→ ArrowItem 显示预览
→ 鼠标释放
→ MoveControlPointCommand
→ Document 更新 Route
→ ChangeNotification
→ ArrowItem 更新路径
```

## 9. 状态归属

| 状态 | 权威位置 |
| --- | --- |
| 语义变量名称、方程、单位、类型 | core::Model |
| FlowEndpoints | core::Model::Flow；端点合法性由 Document 校验 |
| Connector 的 from/to | DiagramDefinition |
| Cloud、Comment 的内容和类型 | DiagramDefinition |
| 元素坐标、端口、路径 | DiagramDefinition |
| 当前缩放、滚动位置 | 工作区状态 |
| 当前选中项、悬停项 | DiagramScene/编辑器 |
| 拖动中的预览路径 | ArrowEditor |
| 自动布线缓存 | UI/路由层，可随时重建 |
| 仿真结果 | Run 数据，不属于编辑文档 |

## 10. 最小目标类集合

```text
核心：
  Document（统一 EntityId 所有权容器与 find(id)）
  DocumentElement
  ModelElement / ViewElement
  Model：Variable / Flow
  DiagramDefinition：Connector / Cloud / Comment / Route
  PortGeometry

图元：
  DiagramItem : QGraphicsObject（统一 id()）
  VariableItem / ViewObjectItem : DiagramItem
  RoutedItem : DiagramItem
  ArrowItem / FlowItem : RoutedItem
  PortHandle : QGraphicsObject
  ControlPointHandle : QGraphicsObject

控制器与算法：
  DocumentController
  PropertyPanel（独立属性 Widget，不依赖 MainWindow）
  DiagramController
  ArrowEditor
  SnapService
  ConnectionRouter
  StraightRouter / OrthogonalRouter / BezierRouter
  ObstacleIndex
```

## 11. 一致性检查与验收

开发期提供以下检查：

- Scene 实体数量与 Document 当前视图实体数量一致。
- 每个 DiagramItem 的 ID 都能在 Document 找到。
- 每个 FlowItem 的 ID 都对应 Model::Flow，而不是 Connector。
- 每个 ArrowItem 的 ID 都对应 DiagramDefinition::Connector。
- CloudItem、CommentItem 只对应 DiagramDefinition 对象，不出现在 Model::variables()。
- 每条 Connector 和 Flow 的端点对象均存在；稳定 Document 中 Flow 两端均为有效 Stock/Cloud，不存在 0 端点或半连接 Flow。
- Document::find(id) 能返回任意 Stock、Flow、Auxiliary、Connector、Cloud 或 Comment，且结果 kind() 与实际对象一致。
- 每个 DiagramItem::id() 均可通过 Document::find() 找到对应核心对象；每个可见核心对象均可通过 DiagramScene::item() 找到图元。
- 当前单视图中，每个 ModelElement 恰好有一个可见 DiagramItem；没有仅因原 XMILE 缺少 view/pts 而不可见的公式变量。
- nameKey 索引与 ModelElement 集合一一对应：每个非空 nameKey 只出现一次，索引目标 ID 正确且对象的 displayName 重新派生后等于该键；Cloud、Comment、Connector 不在索引中。
- 创建、改名、删除、restore、clear、undo/redo 和 XMILE 加载后，名称索引均满足上述约束；自动命名发生冲突时递增计数器且不扫描全部变量。
- 所有索引无重复、无失效 ID。
- 普通编辑、撤销和重做后均满足上述约束。
- 统一 ID 的 undo/redo 必须使用以下完整场景：创建 Stock、Cloud、Flow、Connector 时 ID 来自同一递增序列；删除 Stock 级联删除 Flow/Connector 后，undo 根据命令持有的 ChangeSet 恢复全部原 ID 和端点，redo 再删除；再次 undo 后新建对象使用大于历史最大值的 ID，不复用已恢复 ID。
- 在 undo 后执行新编辑会清空 redo 栈，随后创建/删除/撤销不得与 ChangeSet 中的 ID 冲突；复制粘贴和跨文档粘贴始终分配新 ID 并重建端点映射。
- 连续执行多轮删除→undo→redo 后，Document::find(id) 与 DiagramScene::item(id) 一一对应，无重复 ID、悬空端点或旧指针复用。
- 打开→编辑→保存→重开后，语义和布局一致。

最终约束概括为：ID 负责互找，Document 负责保存，Command 负责修改，ChangeNotification 负责同步，Scene 负责显示。
