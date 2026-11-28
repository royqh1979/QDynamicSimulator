# Lookup 模型与求解器设计

## 1. 范围与依据

本阶段实现 Lookup 的核心数据模型、标量/数组求值、编译依赖、QDS 持久化及 XMILE `<gf>` 导入/导出。不包含 Lookup 图形化编辑器、拖放面板、专用属性页或其它 UI 改造；现有通用名称和布局投影可以复用。Vensim MDL 的括号 Lookup 文本解析及 WITH LOOKUP 语法另行实施，不因本阶段而声称已支持。

参考 `docs/references/xmile-1.1.html` 的 Graphical Functions：Lookup 支持 xscale+ypts 或 xpts+ypts；continuous、extrapolate、discrete 三种模式具有不同域内/域外规则。XMILE 同时允许独立命名 `<gf name>` 和 aux/flow 内联 `<gf>`，内联形式的 eqn 是 Lookup 输入表达式。

## 2. 不可变值对象

`LookupDefinition` 是纯值对象，构造完成后不可变：

```text
LookupDefinition
├─ kind：Continuous / Extrapolate / Discrete
├─ x：递增 double 容器
└─ y：与 x 等长的 double 容器
```

构造约束：

- x/y 等长且至少两个点，全部数值有限；
- x 严格递增，不允许重复或逆序；
- Discrete 最后两个 y 必须相同，符合 XMILE 对阶梯函数末段的约束；
- 非法定义不能进入 Document 或发布到 SimulationPlan。

`evaluate(x)` 规则：

- Continuous：域内相邻点线性插值，域外取最近端点；
- Extrapolate：域内线性插值，域外使用相应端最末/最初两点线性外推；
- Discrete：域内取不大于输入的最近 x 对应 y，域外取最近端点；
- 输入 NaN/Inf 沿用现有仿真非有限值失败规则，不转换成 0。

LookupDefinition 可作为值存入 ElementData、ModelElement 和 Plan，不使用指向 Document 内部定义的裸指针。

## 3. 独立与内联 Lookup

新增 `ElementKind::Lookup` 和 `Lookup : ModelElement`：

- 独立 Lookup 有统一 EntityId、displayName/nameKey、unit/comment 和布局，可被公式按名称调用并参与因果依赖；
- 独立 Lookup 本身没有输入，因而不产生独立数值时间序列，不进入初始化/运行期输出顺序；
- ModelElement 可携带可选内联 LookupDefinition，当前只允许 Variable 和 Flow。内联变量先计算自身 equation，再对结果应用 Lookup；数组逐元素应用；
- Stock 不允许内联 Lookup。内联定义不生成伪造的新模型变量，也不因为 UI 未提供专用编辑器而丢失；
- 独立 Lookup 在现有通用画布中只复用名称/布局显示；本阶段不提供专用编辑 UI。

`LookupDefinition` 在 ModelElement 和 ElementData 中以 `std::optional` 表示“是否存在内联/独立定义”；它只表达定义是否存在，不用于混淆查询中的缺失和空数值序列。

## 4. 编译与求值

- Analysis 将独立 Lookup 调用记录为调用方到该 Lookup 的依赖；无参数个数错误、未知名称和类型误用继续使用既有诊断通道。
- SimulationPlan 的 ReferenceBinding 增加 Lookup 类型，并以值保存其 LookupDefinition。普通调用直接读取 Plan，不在每次求值重新规范化名称或查找 Document。
- EvaluationContext 增加按名称和 double 输入查询 Lookup 的 reader；未知函数仍先经过原有宏/错误语义，Lookup 调用支持标量和数组逐元素输入。
- 内联 Variable/Flow 的求值顺序为 equation → LookupDefinition；数组对输出 values 逐元素应用。
- 独立 Lookup 不进入输出变量列表；调用它的 Variable/Flow 正常输出。
- Euler/RK4 每个实际求值点使用相同 Lookup 定义；Lookup 本身无状态，不需要历史槽或随机状态。

## 5. 持久化与 XMILE

### QDS

- 独立元素使用 `<lookup>`，内联 Variable/Flow 使用其子元素 `<lookup>`；
- kind 保存 continuous/extrapolate/discrete；x/y 分别保存等长数值列表；
- ElementData capture/apply/restore、复制粘贴和撤销/重做必须携带同一定义；
- 非法或截断列表明确拒绝，不生成部分定义。

### XMILE

- 独立 Lookup 导出为 `<gf name>`，内联定义导出为所属 aux/flow 的 `<gf>`；
- 优先使用 xpts+ypts 保留精确输入；导入 xscale+ypts 时按等间隔生成 x 容器；xpts/ypts 支持 `sep` 指定的单字符列表分隔符；
- kind 由 `<gf>` 的 type 属性映射，缺省为 continuous；不能识别的类型明确报错；
- yscale 属于显示范围，本阶段不进入核心计算模型；不把它误当作 y 数据；
- 保留既有方程、端点、布局和模型变量顺序，不为内联 Lookup 创建额外变量。

## 6. 测试与 profile

- 三种模式的端点、域内插值、域外夹取/外推、离散阶梯和非法定义；
- 独立 Lookup 调用、内联 aux/flow、数组逐元素、Euler/RK4、取消及非有限输入；
- 调用依赖、独立 Lookup 不单独输出、修改定义后重新编译生效；
- QDS 往返、复制粘贴、undo/redo、XMILE xscale/xpts、独立/内联和损坏输入；
- 开启 XMILE corpus，使用 SDXorg lookups、lookups_inline 等真实模型；
- 在 SimulationProfile 增加固定 Lookup 调用负载，比较 checksum、样本数、每步/每求值耗时，不设置超出需求的极限门槛。

## 7. 阶段边界

1. 核心值对象、Document 模型、编译/求值和测试；
2. QDS/XMILE 持久化、真实语料和 profile；
3. 全量 Release 后 Debug 回归后提交合并。

Lookup 图形编辑器、Vensim MDL Lookup 文本、WITH LOOKUP、Lookup Area/Invert/随机分布等扩展功能均不在本轮。
