# Vensim、XMILE 与本程序概念对应

## 1. 目的与依据

本文先定义概念，再约束模型和界面命名。Vensim 是交互与语义参照，XMILE 是持久化参照；二者都不能用本项目临时发明的名称替代。

依据：

- Vensim [Sketch Tools](https://www.vensim.com/documentation/ref_sketch_tools.html)：Level、Arrow、Rate、Sketch Comment 等工具。
- Vensim [Rate Class](https://www.vensim.com/documentation/22930.html)：Rate 表示进出 Level 的流，包含阀门、管线和必要的源/汇云团。
- Vensim [Adding Auxiliaries and Arrows](https://www.vensim.com/documentation/20335.html)：Arrow 可连接 Rate 名称或阀门，二者在结构上是同一个变量。
- Vensim [Variable Types](https://www.vensim.com/documentation/ref_variable_types.html)：Level、Constant、Data、Auxiliary 等变量分类。
- Vensim [Sketch Comment](https://www.vensim.com/documentation/22940.html)：注释属于视图展示，不是模型结构；只有作为 Arrow Junction 时才可接入箭头。
- OASIS [XMILE 1.1](https://xmile.systemdynamics.org/1.1/xmile-v1.1.html)：模型变量包括 stock、flow、aux；stock 通过 inflow/outflow 引用 flow，视图使用 connector、text_box 等对象。1.1 没有标准 `<cloud>` 显示对象；本地保存的 1.0 规范仅用于旧文件兼容核对。

## 2. 元素对应表

| 当前名称/类型 | Vensim 概念 | XMILE 表示 | 目标处理 |
| --- | --- | --- | --- |
| Stock | Level | model `<stock>` + view `<stock>` | 保留；语义变量，具有初始值及 inflow/outflow |
| Flow | Rate；其图形中心为 Valve | model `<flow>` + view `<flow>` | 一个流量实体直接拥有路径和端点；阀门不是另一个变量；XMILE 可存在未连接 flow |
| Variable | Vensim Variable 工具，覆盖 Auxiliary/Constant/Data | model/view `<aux>` | 统一为 ElementKind::Variable；常量和数据角色由实际公式判断，不保留平行元素类型 |
| Cloud | Rate 的 source/sink 云团 | XMILE 1.1 无标准 `<cloud>`；不是 model 变量 | 移出语义变量集合，成为只有 EntityId 的视图对象；布局与端点绑定保存在 workspace |
| Annotation | Sketch Comment；可为 Arrow Junction | view `<text_box>` 等 | 移出语义变量集合，不拥有模型方程；只有 content，不需要唯一名称 |
| “依赖线” / Information | Arrow；变量间可称因果箭头 | view `<connector>` | 更名“箭头”；不使用“依赖线”作为正式术语 |
| “物料流” / MaterialFlow | 没有独立于 Rate 的此类工具 | stock 的 `<inflow>`/`<outflow>` 与 view flow | 删除独立类型；物料关系属于流量实体 |
| Annotation Connection | 普通 Arrow 接入 Arrow Junction；不是另一种标准连接 | 仍是 `<connector>` | 不保留独立连接类型；由端点对象决定是否具有模型结构意义 |

## 3. 核心结论

### 3.1 流量与箭头是不同对象

- Rate 改变 Level 的存量，具有速率方程，连接 Level 或源/汇云团。
- Arrow 表示变量间的因果/信息关系，可指向 Rate 的名称或阀门，但不传输物料。
- 因而面板分隔符后的两个入口应是“流量（Rate）”和“箭头（Arrow）”，不是“物料流”和“依赖线”。
- Vensim 的 Rate 图形虽然由管线、阀门和必要云团组成，但这些是同一个 Rate 的可视几何，不应映射成多个可独立存在的核心连接对象。

### 3.2 XMILE 没有“依赖线”类型

XMILE connector 是视图对象，通过 from/to 指向其他对象。标准没有本项目的 Information/Annotation 枚举。目标模型只保留一种 Connector/Arrow；连接是否具有因果结构意义由端点类型决定，注释或普通展示连接不得参与方程依赖和仿真。

### 3.3 语义模型与视图对象必须分层

当前 `Model::variables()` 同时容纳 Cloud 和 Annotation，会导致视图对象进入名称检查、方程检查和仿真依赖。目标边界：

```text
Document
├── Model                  Stock、Flow、Variable 等语义变量
└── DiagramDefinition      位置、路径、Cloud、Comment、Connector、视图状态
```

- XMILE 读写将 Variable 映射为 aux；常量或数据角色由实际公式和所用函数决定。
- Cloud、Comment 和 Connector 使用统一 EntityId，但不进入 Model 的变量索引，也不需要 displayName/nameKey；Comment 保存 content。
- Stock、Flow、Variable 需要唯一 displayName，并由 Document 集中维护 `nameKey → EntityId` 索引供重名判断和公式解析。
- XMILE uid 仅用于文件内显示对象引用，加载时映射为新 EntityId，不作为内部永久身份。Cloud 在 XMILE 1.1 中没有标准标签，其布局与端点引用只能进入 workspace。
- Flow 的端点校验属于 Document 聚合层，因为端点既可能是 Model 中的 Stock，也可能是 DiagramDefinition 中的 Cloud。
- Connector 属于 DiagramDefinition；只有两端均为语义变量时才表达因果结构。
- 所有文档对象仍共用一个 EntityId 生成器，语义/视图分层不产生第二套 ID 空间。

## 4. 对当前目标设计的修正

1. 删除 ConnectionType::MaterialFlow、Information、Annotation 的三分类，改为单一 Connector 类型。
2. 流量直接保存 FlowEndpoints 和 Route；不生成任何物料 Connector。
3. FlowItem 提供阀门和名称两个 Arrow 吸附位置，但二者解析到同一个流量 EntityId。
4. 流量只支持 Straight 和 Bezier、默认 Straight，是本项目已确认的编辑子集；不得声称 Vensim Rate 本身只支持这两种形状。
5. 阀门始终垂直于路径切线，也是本项目已确认的显示规则；Vensim 自身提供多种阀门外观选项，不把该规则误写为 XMILE 要求。
6. 本项目已确认使用拖拽橡皮线创建 Rate/Arrow；Vensim 文档中的单击起点/终点方式仅作概念参照，不覆盖本项目交互决定。
7. Rate 可从空白开始并在空白结束；每个空白端自动创建真正的 Cloud，Flow 稳定保存 Stock/Cloud EntityId，不以 0 表示画布开放端。
8. 未被 Stock 引用或没有视图的 XMILE Flow 仍须保留在 Model，并由导入器补建可见 Flow 及必要 Cloud；所有语义变量都必须出现在当前视图中。
9. 语义变量默认名称按类型递增生成，并通过集中式 nameKey 索引排除冲突；Cloud/Comment 不参与该命名体系。

### 4.1 Stock 初值与外部公式边界

内部 Stock 独立保存 `initialEquation`，FlowEndpoints 是净流入关系的唯一权威。Vensim Level 的 `INTEG(inflows - outflows, initial)` 在使用时派生；XMILE Stock `<eqn>` 是仅初始化求值的初始方程，只在导入/导出适配层映射。普通 ModelElement::equation 不保存 Stock 初值。XMILE 同名 `<eqn>` 的上下文歧义是外部格式弱点，QDS 使用显式 `<initial>`，不得据此建立第二份可写净流入公式。

### 4.2 Vensim Sketch 工具与变量分类

Vensim 内置 Sketch 工具集只提供一个 `Variable` 工具，用于创建 Constant、Auxiliary 和 Data；具体变量类型由方程和使用方式决定。这与官方 Variable Types 文档一致：Auxiliary 是动态计算变量，Constant 是不随时间改变的输入，二者不使用不同积分或求值算法。

因此，本项目已删除 Auxiliary、Constant、Data 三种平行 ElementKind，统一为 `ElementKind::Variable`，面板、工具、画布外形和模拟编译均使用同一类型。数值常量仅由实际公式识别，用于结果列表过滤等显示决策。Vensim 的 Existing Variable、Shadow Variable、Merge、Control、Reference Modes 等完整工具集也不因这份清单自动进入当前最小范围。

Vensim Sketch 工具默认 sticky，并支持键盘选择；本项目当前创建操作结束后返回选择工具。该差异与既定的拖放创建、左键拖拽连线一样，不随参考资料自动改变。

## 5. 后续设计审查清单

- 每个面板名称是否能在上表中找到明确对应。
- 每个对象是否明确属于 Model 或 DiagramDefinition。
- 是否误把 Cloud、Comment 当作可计算变量。
- 是否存在 MaterialFlow 或“依赖线”等平行概念。
- 是否把同一 Rate 的阀门、名称或管线拆成多个语义 ID。
- XMILE 导入/导出是否保留标准语义并明确降级项，本项目额外语义和外观是否完整保存在原生 QDS `document.xml` 中。
