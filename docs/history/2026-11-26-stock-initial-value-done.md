# Stock 初值与积分语义重构计划

创建时间：2026-11-26 23:48:38 +0800

状态：已完成并合并；实现、测试、profile 与 Release/Debug 全量回归均通过

## 目标与问题

当前 Stock 复用 Variable，ModelElement::equation 同时被用于保存 XMILE 初值，导致界面把 Population=1000 显示为普通 Equation，也使 Vensim Level 的 INTEG 语义无处表达。不能通过改标签或兼容分支掩盖这一模型问题。

## 目标模型

- 新增 `Stock : ModelElement`，由 Document 以 unique_ptr 唯一持有。
- Stock 独立持有不可变 `Formula initialEquation`，提供只读 `initialEquation()/initialFormula()`。
- 普通 Equation/Formula 不再用于 Stock 初值；Document::setEquation 对 Stock 拒绝，使用 setInitialEquation。
- FlowEndpoints 仍是唯一净流入结构权威。Vensim 的 `INTEG(inflows - outflows, initial)` 在使用时派生，不保存为与初值/端点平行的可写公式。
- XMILE Stock `<eqn>` 映射 initialEquation；QDS 使用独立 `<initial>` 字段，开发阶段不保留旧字段兼容层。
- Stock 的逐数组元素初值沿用现有 ArrayElementFormula 集合，但数组仿真仍明确不支持。
- Capture/Apply/Restore、重命名、复制粘贴和 undo/redo 使用独立 initialEquation 文本；AST 不持久化。
- 初始化分析/编译读取 initialFormula；运行期 Stock 状态不反复执行初值公式。

## XMILE 边界评审

XMILE 的 Stock `<eqn>` 是仅在开始求值的初始方程，这一字段命名存在工程弱点：与普通变量的运行公式同名但语义不同，完整动力学需要同时读取初值和 inflow/outflow，Vensim 互操作必须拆分/组装 INTEG，容易导致平行状态和误用。XMILE 的状态分离有可取之处，但内部模型不照搬其标签歧义。QDS 使用显式 `<initial>`，外部 `<eqn>` 只在 XMILE 适配层出现。

## 不变性与生命周期

- Stock 与 Variable 都是 DocumentElement，由同一 ID 容器管理。
- Stock 初值修改时整体替换 Formula，旧 AST 不修改。
- 外部 raw pointer 仍遵守当前栈同步使用纪律。
- 不保存 Vensim 派生 INTEG 文本作为第二权威状态。

## 迁移与验收

1. 新增 Stock 类型和独立初值编辑 API，迁移 Document 创建/恢复/修改/复制。
2. 迁移 Analysis、SimulationCompiler/Runner 和属性面板；Stock 显示 Initial value 与只读 Inflows/Outflows。
3. 迁移 QDS/XMILE、现有测试及 Rabbit 场景，保留全部数值参考验证。
4. 验证 Stock 普通 setEquation 被拒绝、初值 undo/redo、改名引用、复制粘贴、QDS/XMILE、初始环、Euler/RK4。
5. 执行适用 profile、Release 后 Debug 全量回归、自审；完成后文件改名为 `-done`。

## 完成结果

实现提交为 `7883fa3 Add simulation controls and stock initial values`，已进入 master。本计划当时已完成全部验收，但文件仍误留在等待审核状态；本次补充完成标记，无剩余实现事项。
