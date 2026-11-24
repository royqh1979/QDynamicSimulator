# 核心与编辑器架构性能基线

## 方法

- 工具：`profile-core 10000`、`profile-editor 1000`。
- 构建：MinGW GCC 8.1.0，Qt 5.15.18 静态 Release。
- 环境：Windows 11，R: 内存盘。
- 指标：总耗时和每操作耗时；每次操作使用 `QElapsedTimer` 直接测量，不累计包含子阶段的 inclusive 时间。

## 核心索引结果

首次实现使用线性 ID/名称查找，创建 10000 个元素耗时 251123 ms。加入 ID、连接和名称哈希索引后，同一工具结果：

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| create | 10000 | 2736.21 | 273.621 |
| connect | 9999 | 8.3341 | 0.833493 |
| lookup | 100000 | 0.4054 | 0.004054 |
| move | 10000 | 0.0539 | 0.005390 |
| remove | 10000 | 1838.21 | 183.821 |

## 编辑器命令、Scene 与 XMILE 结果

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| editor_create | 1000（七类实体） | 6.1844 | 6.1844 |
| editor_connect | 572（合法依赖） | 0.6083 | 1.0635 |
| editor_move_two | 2 | 0.0501 | 25.0500 |
| editor_undo_redo_macro | 2 | 0.0243 | 12.1500 |
| xmile_save | 1000 | 7.8244 | 7.8244 |
| xmile_load | 1000 | 17.6013 | 17.6013 |

加入网格、动态场景范围和布局操作后的同规模结果：创建 17.3789 ms、合法连接 62.8327 ms、两元素移动 0.2712 ms、移动宏撤销/重做 0.5258 ms、1000 元素对齐 127.552 ms、XMILE 保存 7.5173 ms、加载 20.1304 ms。

## 布线 Profile

使用空间索引和候选直角路径自动避障后，1000 实体、572 条合法连接的结果：连接创建 102.461 ms，572 条路径编辑 139.103 ms，路径几何重建 0.1417 ms，两元素移动 0.5536 ms，移动宏撤销/重做 1.0457 ms，XMILE 保存 8.4397 ms、加载 19.9862 ms。profile 同时覆盖所有七类实体、路径控制点及 Scene 析构。

加入独立工作区 XML、剪贴板和导航后：1000 元素粘贴 1418.19 ms，100 次全量名称搜索共 35.4486 ms，XMILE 保存 55.3179 ms、加载 38.4462 ms。10000 元素静态校验为 9.2665 ms。

最小交互增量使用真实 Qt 拖放/场景鼠标事件测试。1000 次橡皮线创建、更新和销毁耗时 13.1585 ms；同次 1000 实体创建为 17.6761 ms。

连接线右键菜单增量覆盖 checkable、互斥、当前类型和切换行为。1000 次菜单及三个互斥动作的创建耗时 28.9842 ms。

箭头修复后，572 条路径的几何与箭头联合重建耗时 0.4197 ms；四方向、斜向和边界端点均纳入单元测试。

贝塞尔控制辅助线覆盖单选、多选、框选、取消选择、控制点端点和真实视图残影测试；1000 次控制点预览及辅助线几何更新耗时 1.3478 ms，1000 次单选/多选可见性切换耗时 2.1714 ms。

## 统一 DocumentElement 与 Flow 重构结果

本轮使用统一 Document 所有权、ChangeSet 命令、单一 Connector 和独立 FlowItem。1000 个六类非 Flow 对象、667 条合法 Arrow 的结果：创建 78.0574 ms，连接 132.537 ms，路径编辑 226.327 ms，路径几何 0.5123 ms，粘贴 208.25 ms，XMILE 保存 14.3013 ms、加载 281.25 ms。1000 次 Flow 阀门几何计算 9.0869 ms，1000 次 Shift 拖动 7.4203 ms。10000 个 Stock 的创建、连接、10 轮 ID 查找、移动、校验和删除分别为 5630.97 ms、1434.44 ms、1.2469 ms、1911.04 ms、51.3532 ms 和 1991.14 ms。

这些结果属于新模型结构的新基线，不与旧七类变量模型直接比较。当前未引入 Stock 流入/流出派生索引；后续优化须先依据 profile。

## nameKey、Cloud 与空白 Flow 重构结果

本轮使用独立 `DocumentFile`（QDS 单 XML ZIP）与 `XmileFile`（未压缩 XMILE XML）、集中式 nameKey 索引及空白端 Cloud 原子创建。工具与上述静态 Release 环境相同。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| create | 10000 | 3.0439 | 0.304390 |
| connect | 9999 | 351.859 | 35.1894 |
| lookup | 100000 | 0.4349 | 0.004349 |
| name_lookup | 100000 | 8.7816 | 0.087816 |
| move | 10000 | 502.221 | 50.2221 |
| validate | 10000 | 9.5828 | 0.958280 |
| remove | 10000 | 453.671 | 45.3671 |
| editor_create | 1000 | 21.6095 | 21.6095 |
| editor_connect | 667 | 123.211 | 184.724 |
| route_edit | 667 | 210.212 | 315.160 |
| editor_paste | 1000 | 40.9090 | 40.9090 |
| connector_line_style | 1000 | 0.0568 | 0.0568 |
| flow_pipeline_geometry | 1000 | 1.4258 | 1.4258 |
| flow_arrow_geometry | 1000 | 0.4226 | 0.4226 |
| blank_flow_create_undo_redo | 1000 | 17.2425 | 17.2425 |
| comment_content_edit | 1000 | 417.708 | 417.708 |
| property_panel_refresh | 1000 | 162.383 | 162.383 |
| qds_save | 1000 | 9.2367 | 9.2367 |
| qds_open | 1000 | 16.6436 | 16.6436 |
| xmile_export | 1000 | 6.5520 | 6.5520 |
| xmile_import | 1000 | 21.5143 | 21.5143 |
| xmile_sir_import | 100 | 9.5488 | 95.4880 |

`blank_flow_create_undo_redo` 每轮实际执行“创建两个 Cloud 和一个 Flow → undo 删除全部 → redo 恢复全部 → undo 回到空文档”，并断言 Scene 与 Document 同步。QDS profile 逐变量断言原生往返后的 ElementKind 一致，覆盖 Auxiliary、Constant 和 Data；XMILE profile 单独测量未压缩导出与导入，不要求保留专有分类。`comment_content_edit` 覆盖正文 ChangeSet、Scene 文本刷新和最终值校验；`flow_pipeline_geometry` 验证每次返回两条 Flow 路径，`property_panel_refresh` 测量独立属性 Widget 的完整刷新。属性面板输入及 undo 同步由编辑器单元测试覆盖。结果只用于后续同环境回归比较，不与不同 Qt 链接方式或旧数据结构的结果直接比较。
