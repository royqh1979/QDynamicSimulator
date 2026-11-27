# SimulationRun nameKey 查询边界重构计划

创建时间：2026-11-27T21:46:36+08:00

## 目标与问题

统一 SimulationRun 后，结果变量元数据、直接原因和数值序列已经自洽，但视图仍需读取运行副本 EntityId 查询结果，导致内部 ID 泄露到公开查询路径。目标是对外统一使用 nameKey，EntityId 仅用于运行副本内部执行和元数据映射。

## 范围与方法

- `SimulationRun::series/arraySeries/causes` 对外接收并返回 nameKey；EntityId 版本私有化，`SimulationRunner` 作为 friend 标记执行边界。
- `RunVariableInfo::id()` 私有化，仅允许 SimulationRun 完成名称映射；公开构造和 withMetadata 保持可用，不增加应用层 friend。
- Graph/Table、Causes Strip、Runs Compare、测试和 profile 全部改用 nameKey，不保留绕过接口。
- 不改数值结果、RunId、modelRevision、历史原因、加载状态或积分语义。

## 验收

- 对外无 EntityId 结果查询入口，视图不再读取 RunVariableInfo.id。
- nameKey 可查询标量/数组结果和直接原因，未知 nameKey 返回空指针或空列表。
- 原结果不可变性、历史原因和多 Run 行为不变。
- 全部测试与 profile 通过，Release 后 Debug 全量回归通过；完成后等待用户审核，不自动提交、合并或推送。
