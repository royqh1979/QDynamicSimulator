# 统一 SimulationRun 结果对象重构计划

创建时间：2026-11-27T21:24:25+08:00

## 目标与问题

当前 Core `SimulationRun` 只保存数值结果，应用层 `NamedRun` 另存 RunId、runName、modelRevision、变量元数据和直接原因。两层都表示一次运行，导致运行副本 EntityId、模型修订号与依赖关系被人为拆开，调用方必须理解两套 ID 和生命周期。

目标是由一个不可变 `SimulationRun` 自洽表示一次运行：RunId、runName、modelRevision、设置、状态、时间、标量/数组序列、诊断、结果变量元数据及直接原因均属于同一对象，不引入 Qt Widgets 或界面指针。

## 范围与方法

- `RunId` 和 `RunVariableInfo` 移入 Core；EntityId 继续只在所属运行副本内使用，nameKey 继续用于跨 Run 对齐和当前模型名称导航。
- `SimulationRun` 增加 RunId、runName、modelRevision、variables、causes 及只读访问接口。通过 `withMetadata()` 返回补齐身份的新对象，原结果不变，禁止发布后改写字段。
- 删除 NamedRun，Controller、Graph/Table、Causes Strip、Runs Compare、数组结果和 profile 全部改用统一对象，不保留平行结果模型。
- Qt 容器继续使用写时复制，统一对象的值拷贝不主动复制数值矩阵；不可变共享指针仍用于 Worker 和跨视图共享。
- 不改积分、公式求值、数组语义、Run 加载规则、模型修订推进规则或同名覆盖行为。

## 实施步骤

1. 更新 simulation 与 simulation-ui 的当前设计，记录字段归属、发布原子性和生命周期。
2. 扩展 Core 类型并迁移 SimulationController 的请求、发布和过期判断。
3. 迁移全部应用视图、测试与 profile，清除 NamedRun 及旧 `run()->series` 调用。
4. 增加统一身份、元数据不可变性、历史原因和数组结果回归。
5. 执行 Release 后 Debug 的 all-test-targets 与 CTest，并运行既有 simulation/editor profile。
6. 自审对象大小、值拷贝、调用点完整性及文档，完成后将本计划标记为 done。

## 验收边界

- 所有原 NamedRun 调用点使用统一 SimulationRun，旧类型和旧间接访问不再存在。
- RunId、runName、modelRevision、variables、causes 与数值结果在发布时一次形成，之后不能修改。
- 多 Run、历史名称、过期提示、Causes Strip、Runs Compare 和 arraySeries 行为保持不变。
- 全部新增或迁移测试和 profile 通过，Release 后 Debug 全量回归通过。
- 不自动提交、合并或推送；完成验证后等待用户审核。
