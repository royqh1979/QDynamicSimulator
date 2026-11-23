# SFD 核心模型与画布 Item 架构

## 1. 状态与目标

本文是已确认的目标设计。稳定 ID、Document、命令、ChangeSet 和 Scene 增量同步已实现；端口句柄、控制点交互和完整布线策略仍按后续阶段实施。

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

## 3. 文档模型

### 3.1 Document 组成

```text
Document
├── core::Model          语义模型：变量、方程、连接语义
└── DiagramDefinition    图形模型：位置、端口、路径、注释和视图
```

- `core::Model` 供仿真引擎使用。
- `DiagramDefinition` 描述 SFD 布局。
- 两者使用同一套 `EntityId` 和 `ConnectionId`。
- 选中、悬停、拖动预览等临时状态只存在 UI。
- 当前缩放和滚动位置属于工作区状态，不属于模型语义。

### 3.2 稳定标识

```cpp
using EntityId = std::uint64_t;
using ConnectionId = std::uint64_t;
using PortId = std::uint32_t;
```

ID 由文档统一分配，在对象整个生命周期内不变。禁止以下做法：

- 使用变量名作为永久 ID。
- 使用 vector 下标作为外部引用。
- 在模型与视图之间保存裸指针。
- 依赖 Item 地址重建连接或撤销操作。

复制粘贴和跨文档合并时重新分配 ID，并通过“旧 ID→新 ID”映射重建内部连接。

### 3.3 核心值对象

核心元素优先采用“统一结构 + 类型枚举”，不建立无收益的深层继承：

```cpp
struct Vec2 {
    double x;
    double y;
};

struct Entity {
    EntityId id;
    EntityType type;
    std::string name;
    std::string equation;
};

enum class ConnectionType {
    MaterialFlow,
    Information,
    Annotation
};

struct Endpoint {
    EntityId entityId;
    PortId portId;
    Vec2 offset;
};

enum class RouteMode {
    Straight,
    Orthogonal,
    Bezier
};

struct Route {
    RouteMode mode = RouteMode::Orthogonal;
    std::vector<Vec2> controlPoints;
    bool manual = false;
    bool locked = false;
};

struct Connection {
    ConnectionId id;
    ConnectionType type;
    Endpoint from;
    Endpoint to;
    Route route;
};
```

只有语义和行为确实差异明显时，才在核心层拆分 Stock、Flow 等子类。

## 4. 画布 Item 类体系

### 4.1 实体 Item

实体需要选择、移动、信号和属性交互，继承 QGraphicsObject：

```text
QGraphicsObject
└── EntityItem
    ├── StockItem
    ├── FlowItem
    ├── AuxiliaryItem
    ├── CloudItem
    └── AnnotationItem
```

`EntityItem` 的公共职责：

- 保存 `EntityId`，不保存 Entity 指针。
- 处理选择、悬停、拖动和错误状态。
- 显示名称及必要的编辑状态。
- 提供端口布局和边界交点查询。
- 向编辑器提交交互请求。
- 根据文档变化刷新显示缓存。

子类只实现真正不同的外形、端口和交互。Constant、Exogenous 与 Auxiliary 如果行为一致，可共用 VariableItem，并通过绘制策略区分外观。

不采用“EntityItem 包含一个 QGraphicsItem 属性”的适配方式，因为这仍需转发场景坐标、选择、事件、绘制、所有权和变换，没有实际收益。

### 4.2 连接线 Item

```text
QGraphicsItem
└── ConnectionItem
```

`ConnectionItem` 保存 `ConnectionId`，负责：

- 根据端点和 Route 生成 QPainterPath。
- 绘制线、箭头、选中状态和标签。
- 实现比实际线更宽的 shape()，方便鼠标命中。
- 响应端点移动和路径变化。
- 管理选中时显示的控制句柄。

它不直接修改连接关系，不承担自动避障、吸附决策和撤销命令职责。数量较多且无需信号时使用 QGraphicsItem；确有信号需求时才改为 QGraphicsObject。

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

PortHandle 负责显示、接收鼠标事件并转发给 ConnectionEditor。使用 `ItemIgnoresTransformations` 保持屏幕大小恒定。不要为每个实体的所有端口永久创建 Item。

### 4.4 拐点和控制句柄

拐点保存在 `Route::controlPoints` 中。只有连接线被选中时，才创建对应控制句柄：

```text
QGraphicsObject
└── ControlPointHandle
```

句柄保存 ConnectionId、控制点索引、场景坐标和控制点类型。折点、贝塞尔锚点和贝塞尔手柄通过类型区分。

不为所有连接线的全部拐点永久创建 Item。句柄可作为位于场景原点、使用场景坐标的 ConnectionItem 子 Item；其他情况由 ConnectionEditingController 管理为独立覆盖层 Item。

## 5. 互找机制

### 5.1 文档索引

Document 提供按 ID 查询：

```cpp
Entity *findEntity(EntityId id);
Connection *findConnection(ConnectionId id);
```

内部可使用 `unordered_map<EntityId, Entity>`，或使用 ID 到容器下标的索引。后者必须保证删除、移动和排序后同步维护索引。

### 5.2 Scene 索引

```cpp
class DiagramScene : public QGraphicsScene {
public:
    EntityItem *entityItem(EntityId id) const;
    ConnectionItem *connectionItem(ConnectionId id) const;

private:
    QHash<EntityId, EntityItem *> m_entityItems;
    QHash<ConnectionId, ConnectionItem *> m_connectionItems;
};
```

### 5.3 双向查找流程

```text
核心 ID → 画布对象
DocumentController → DiagramScene::entityItem(id)

画布对象 → 核心对象
EntityItem::entityId() → Document::findEntity(id)
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
DiagramController / ConnectionEditor
   ↓
QUndoCommand
   ↓
DocumentController 修改 Document
   ↓
产生 ChangeSet
   ↓
DiagramScene 按 ChangeSet 增量更新 Item
```

约束：

- EntityItem 不直接修改名称、方程或持久化位置。
- ConnectionItem 不直接修改连接关系。
- ControlPointHandle 不直接修改 Route。
- Scene 不能绕过命令体系写文档。
- 核心文档不直接调用 Qt 信号或操作 Item。

### 6.2 ChangeSet

核心保持纯 C++，Qt 侧 DocumentController 在修改成功后发出变化通知：

```cpp
struct ChangeSet {
    std::vector<EntityId> addedEntities;
    std::vector<EntityId> updatedEntities;
    std::vector<EntityId> removedEntities;
    std::vector<ConnectionId> addedConnections;
    std::vector<ConnectionId> updatedConnections;
    std::vector<ConnectionId> removedConnections;
    bool documentReset = false;
};
```

`DiagramScene::apply(ChangeSet)` 的顺序：

1. 删除受影响的连接 Item。
2. 删除实体 Item。
3. 创建新增实体 Item 并登记索引。
4. 创建新增连接 Item 并登记索引。
5. 更新已有实体和连接 Item。
6. 刷新相关端口和路径。

打开文件或整体替换模型时使用 `documentReset` 并完整重建 Scene；普通编辑不做全场景重建。

### 6.3 拖动预览与提交

拖动元素、端点或拐点时使用“预览 + 提交”：

- 拖动中仅更新 Item、相关连接预览和吸附参考线。
- 不逐像素写文档，不逐像素创建撤销命令。
- 鼠标释放时生成一条命令，一次性提交最终值。
- 操作被取消时，根据文档原值恢复 Item。

当前 `VariableItem::itemChange()` 直接逐次写核心模型的实现，应在重构时移除。

### 6.4 撤销与重做

命令保存修改前后的值对象，不保存对象指针：

```cpp
class RenameEntityCommand : public QUndoCommand {
    EntityId m_id;
    std::string m_before;
    std::string m_after;
};
```

- redo() 应用 after，undo() 应用 before。
- 两者均通过 DocumentController 并产生 ChangeSet。
- Scene 不区分普通编辑、撤销和重做。
- 删除实体的命令同时保存实体和关联连接的完整快照，以便整体恢复。
- 复合编辑通过 QUndoCommand 子命令或事务合并为一次撤销单元。

## 7. 生命周期规则

创建实体：

```text
Document 新增 Entity
→ Scene 创建 EntityItem
→ 加入实体索引
→ 创建依赖该实体的 ConnectionItem
```

删除实体：

```text
先删除关联 ConnectionItem 并移出连接索引
→ 删除 EntityItem 并移出实体索引
→ 确认不存在悬空连接
```

复制粘贴：

- 为所有粘贴对象分配新 ID。
- 建立旧 ID 到新 ID 的映射。
- 按映射重建内部连接。
- 指向未复制对象的连接不粘贴，或按明确规则保留为外部引用。

QPointer 不能替代上述索引和生命周期规则。

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

具体策略包括 StraightRouter、OrthogonalRouter 和 BezierRouter。ConnectionEditor 通过组合使用 ConnectionRouter、SnapService 和 ObstacleIndex。

SnapService 统一处理网格、端口、元素边界、对齐参考线和现有路径吸附，返回 SnapResult，不负责创建图元。

一次拖动拐点的流程：

```text
ControlPointHandle
→ ConnectionEditor 计算候选位置
→ SnapService 执行吸附
→ ConnectionItem 显示预览
→ 鼠标释放
→ MoveControlPointCommand
→ Document 更新 Route
→ ChangeSet
→ ConnectionItem 更新路径
```

## 9. 状态归属

| 状态 | 权威位置 |
| --- | --- |
| 名称、方程、单位 | core::Model |
| 元素类型、连接语义 | core::Model |
| 元素坐标、端口、路径 | DiagramDefinition |
| 当前缩放、滚动位置 | 工作区状态 |
| 当前选中项、悬停项 | DiagramScene/编辑器 |
| 拖动中的预览路径 | ConnectionEditor |
| 自动布线缓存 | UI/路由层，可随时重建 |
| 仿真结果 | Run 数据，不属于编辑文档 |

## 10. 最小目标类集合

```text
核心：
  Document
  Entity
  Connection
  Route
  PortGeometry

图元：
  EntityItem : QGraphicsObject
  StockItem / FlowItem / AuxiliaryItem / CloudItem
  ConnectionItem : QGraphicsItem
  PortHandle : QGraphicsObject
  ControlPointHandle : QGraphicsObject

控制器与算法：
  DocumentController
  DiagramController
  ConnectionEditor
  SnapService
  ConnectionRouter
  StraightRouter / OrthogonalRouter / BezierRouter
  ObstacleIndex
```

## 11. 一致性检查与验收

开发期提供以下检查：

- Scene 实体数量与 Document 当前视图实体数量一致。
- 每个 EntityItem 的 ID 都能在 Document 找到。
- 每个 ConnectionItem 的 ID 都能找到 Connection。
- 每条连接的端点实体均存在。
- 所有索引无重复、无失效 ID。
- 普通编辑、撤销和重做后均满足上述约束。
- 打开→编辑→保存→重开后，语义和布局一致。

最终约束概括为：ID 负责互找，Document 负责保存，Command 负责修改，ChangeSet 负责同步，Scene 负责显示。
