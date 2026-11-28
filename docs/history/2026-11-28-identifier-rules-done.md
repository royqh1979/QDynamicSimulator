# 标识符与保留名称规则重构计划

创建时间：2026-11-28 11:12:00 +0800

状态：已完成并合并；规则、专项测试、profile 与 Release/Debug 全量回归均通过

## 目标与问题

当前名称检查主要覆盖空名称和模型元素、Dimension、Macro 之间的重名，没有统一的内置函数保留规则。变量、宏或 Lookup 可以与内置函数同名，保存成功后却因内置优先而永不执行，形成无效定义。

目标是在底层 Identifier 模块提供唯一保留名称判断，并让创建、重命名、恢复和持久化导入共用该规则，为后续统一内置/Macro/Lookup 调用解析提供明确语义。

## 规则

- 所有判断使用 normalizeIdentifier 后的 nameKey，保持大小写不敏感以及空格/下划线等价。
- 已登记的内置函数和零参数求值名称属于保留名称，不能被普通 Variable、Lookup、Macro 或 Dimension 覆盖。
- `TIME STEP`、`INITIAL TIME`、`FINAL TIME`、`SAVEPER`、`NOISE SEED` 等可定义控制/特殊变量不因本规则被误禁；它们不与内置求值名称冲突。
- DimensionElement 使用 `Dimension.Element` 限定名称，不与全局内置函数冲突，本阶段不增加额外禁用。
- 保留名称检查应用于 Document 插入/重命名/恢复、Dimension 定义和 Macro 定义；所有路径使用同一函数。
- 现有合法中文、空格、下划线和带引号特殊名称不变。

## 步骤

1. 在 Identifier 中提供保留名称查询，内置函数注册表仍是函数语义来源，不复制名称列表。
2. 将检查接入 Document 各创建/恢复/重命名入口及 Macro/Dimension 定义。
3. 更新同名 TIME 测试，增加内置变量/Macro/Lookup/Dimension 拒绝和控制变量允许的回归。
4. 运行现有 Core/编辑器/XMILE 语料、适用 profile、Release 后 Debug 全量回归。
5. 审核后提交合并，再进入统一调用解析；不把调用解析混入本阶段。

## 验收

- 内置同名 Variable/Lookup/Macro/Dimension 在所有创建、重命名和恢复路径均被拒绝，原对象保持有效。
- 合法控制变量、中文、空格和下划线名称保持可用。
- 既有模型和 XMILE corpus 不出现未审查的兼容性回归。
- 规则只有 Identifier 一处权威实现，后续统一调用解析可直接复用。

## 完成结果

Identifier 新增唯一 `isReservedIdentifier()`，直接复用内置函数注册表。Document 插入/重命名/恢复、Dimension 和 Macro 定义均使用该规则；控制变量、中文、空格和下划线行为保持。

专项测试覆盖 Variable、Lookup、Macro、Dimension、restore 失败原子性及控制变量允许。既有时间函数测试曾把输出变量命名为保留的 step/pulse/ramp，已改为明确的 `*_output` 并加入重命名成功断言，原函数场景和预期值未删除。

`reserved_identifier_lookup` 的 100000 次查询为 8.8011 ms；每次仅一次规范化和一次内置表查找，没有额外重复解析。Release 与 Debug 全量 CTest 均通过。后续统一调用解析直接依赖本规则。
