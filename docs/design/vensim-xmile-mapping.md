# Vensim、XMILE 与本程序概念对应

## 1. 目的与依据

本文先定义概念，再约束模型和界面命名。Vensim 是交互与语义参照，XMILE 是持久化参照；二者都不能用本项目临时发明的名称替代。

依据：

- Vensim [Sketch Tools](https://www.vensim.com/documentation/ref_sketch_tools.html)：Level、Arrow、Rate、Sketch Comment 等工具。
- Vensim [Rate Class](https://www.vensim.com/documentation/22930.html)：Rate 表示进出 Level 的流，包含阀门、管线和必要的源/汇云团。
- Vensim [Adding Auxiliaries and Arrows](https://www.vensim.com/documentation/20335.html)：Arrow 可连接 Rate 名称或阀门，二者在结构上是同一个变量。
- Vensim [Variable Types](https://www.vensim.com/documentation/ref_variable_types.html)：Level、Constant、Data、Auxiliary 等变量分类。
- Vensim [Sketch Comment](https://www.vensim.com/documentation/22940.html)：注释属于视图展示，不是模型结构；只有作为 Arrow Junction 时才可接入箭头。
- OASIS [XMILE 1.0](https://docs.oasis-open.org/xmile/xmile/v1.0/os/xmile-v1.0-os.html)：模型变量包括 stock、flow、aux；stock 通过 inflow/outflow 引用 flow，视图使用 connector、cloud、text_box 等对象。

## 2. 元素对应表

| 当前名称/类型 | Vensim 概念 | XMILE 表示 | 目标处理 |
| --- | --- | --- | --- |
| Stock | Level | model `<stock>` + view `<stock>` | 保留；语义变量，具有初始值及 inflow/outflow |
| Flow | Rate；其图形中心为 Valve | model `<flow>` + view `<flow>` | 一个流量实体直接拥有路径和端点；阀门不是另一个变量 |
| Auxiliary | Auxiliary | model/view `<aux>` | 保留；语义变量 |
| Constant | Constant / Parameter | 通常使用 `<aux>`，没有独立 constant 标签 | 保留为变量分类，不冒充 XMILE 独立元素 |
| Exogenous | Data；Glossary 中 Exogenous 指由数据给定的变量 | 通常使用 `<aux>` | 界面宜称“数据/外生变量（Data）”，作为变量分类 |
| Cloud | Rate 的 source/sink 云团 | view `<cloud>`，不是 model 变量 | 移出语义变量集合，成为视图对象 |
| Annotation | Sketch Comment；可为 Arrow Junction | view `<text_box>` 等 | 移出语义变量集合，不拥有模型方程 |
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
├── Model                  Stock、Flow、Auxiliary 等语义变量
└── DiagramDefinition      位置、路径、Cloud、Comment、Connector、视图状态
```

- Constant 和 Data/Exogenous 可作为语义变量的分类属性，XMILE 读写仍映射为 aux。
- Cloud 和 Comment 使用统一 EntityId，但不进入 Model 的变量索引。
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

## 5. 后续设计审查清单

- 每个面板名称是否能在上表中找到明确对应。
- 每个对象是否明确属于 Model 或 DiagramDefinition。
- 是否误把 Cloud、Comment 当作可计算变量。
- 是否存在 MaterialFlow 或“依赖线”等平行概念。
- 是否把同一 Rate 的阀门、名称或管线拆成多个语义 ID。
- XMILE 往返后是否保留标准语义，并将本项目额外外观信息限制在独立 workspace 中。
