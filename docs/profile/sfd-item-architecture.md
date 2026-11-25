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
| flow_arrow_geometry | 1000 | 0.4314 | 0.4314 |
| orthogonal_auto_route | 1000 | 1.8474 | 1.8474 |
| flow_label_geometry | 1000 | 7.1001 | 7.1001 |
| flow_center_boundary | 1000 | 9.9470 | 9.9470 |
| blank_flow_create_undo_redo | 1000 | 17.2425 | 17.2425 |
| comment_content_edit | 1000 | 417.708 | 417.708 |
| property_panel_refresh | 1000 | 162.383 | 162.383 |
| qds_save | 1000 | 9.2367 | 9.2367 |
| qds_open | 1000 | 16.6436 | 16.6436 |
| xmile_export | 1000 | 6.5520 | 6.5520 |
| xmile_import | 1000 | 21.5143 | 21.5143 |
| xmile_sir_import | 100 | 9.5488 | 95.4880 |

`blank_flow_create_undo_redo` 每轮实际执行“创建两个 Cloud 和一个 Flow → undo 删除全部 → redo 恢复全部 → undo 回到空文档”，并断言 Scene 与 Document 同步。QDS profile 逐变量断言原生往返后的 ElementKind 一致，覆盖 Auxiliary、Constant 和 Data；XMILE profile 单独测量未压缩导出与导入，不要求保留专有分类。`comment_content_edit` 覆盖正文 ChangeSet、Scene 文本刷新和最终值校验；`flow_pipeline_geometry` 验证每次返回两条 Flow 路径，`property_panel_refresh` 测量独立属性 Widget 的完整刷新。属性面板输入及 undo 同步由编辑器单元测试覆盖。结果只用于后续同环境回归比较，不与不同 Qt 链接方式或旧数据结构的结果直接比较。

## Connector 默认贝塞尔与依赖刷新

运行 `profile-editor 1000`，使用同上述静态 Release 环境。`sir_dependent_route_moves` 导入真实 SIR，定位 infectious 及其 Stock→Flow→Arrow 依赖链，每轮通过 DocumentController 移动 Stock，并读取全部相关 Arrow 的最新路径作为校验；不是只测量孤立几何函数。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| bezier_control_guide_updates | 1000 | 0.7701 | 0.7701 |
| automatic_bezier_edit_reset | 1000 | 1.1954 | 1.1954 |
| connector_drag_selection | 1000 | 42.4483 | 42.4483 |
| endpoint_preview_cancel | 1000 | 46.2102 | 46.2102 |
| bezier_guide_single_selection | 1000 | 4.5462 | 4.5462 |
| flow_center_boundary | 1000 | 9.1708 | 9.1708 |
| xmile_sir_import | 100 | 10.3845 | 103.845 |
| sir_dependent_route_moves | 1000 | 72.6264 | 72.6264 |

`automatic_bezier_edit_reset` 每轮恢复两个自动点、计算辅助线，再通过控制句柄位置变化预览编辑，并验证两个点成组转为手动；覆盖自动点显示和编辑路径，而非只测量几何函数。`connector_drag_selection` 通过 Scene 鼠标按下、移动和释放测量框选状态切换，校验拖动中辅助线数量为零、释放后恢复为两条且曲线保持选中；真实 Viewport 反向拖动和释放重绘由编辑器单元测试覆盖。`endpoint_preview_cancel` 使用真实 DiagramView 鼠标事件执行端点按下、空白拖动和取消，校验预览中无辅助线或中间控制句柄、取消后恢复两条辅助线；控制句柄启用 ItemIgnoresTransformations，不能用缺少 View 的 Scene 事件替代。

该结果用于后续同场景回归比较。当前两级依赖刷新直接遍历 Scene 图元，不引入额外派生索引；只有后续 profile 证明需要时才调整索引结构。

## Connector 指向 Flow 的端点命中

运行 `profile-editor 1000`，使用同上述静态 Release 环境。`connector_create_to_flow` 使用真实 DiagramView 鼠标按下、拖动和释放，从 Auxiliary 创建指向 Flow 阀门中心的 Connector；每轮断言 Document 端点和 Scene Arrow 端点，再在计时外 undo 清理。计时包含端点命中、橡皮线、命令和 Scene 增量同步，不包含坐标映射、结果断言和清理。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| connector_create_to_flow | 1000 | 21.5640 | 21.5640 |

编辑器回归同时覆盖 Connect 工具创建、端点句柄重连到 Flow、空白取消、Arrow 不可作为端点、undo/redo，以及 QDS 和 XMILE 往返后的 from/to。

## 端点命中高亮

运行 `profile-editor 1000`，使用同上述静态 Release 环境。`endpoint_target_highlight` 在一次真实 Connect 拖动中循环移动鼠标到 Flow、Stock 和空白，计时覆盖场景命中、目标切换及 Item 高亮状态更新；每轮断言只有当前目标处于高亮状态。坐标映射、初始按下和最终工具切换不计时。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| endpoint_target_highlight | 1000 | 5.7478 | 5.7478 |

单元测试使用真实 DiagramView 验证普通实体和 Flow 的橙色虚线像素、蓝色选中态的区别、目标切换、空白清除、释放/取消及重绘一致性；颜色断言与绘制代码共同引用 `DiagramPalette.h`，避免测试依赖重复字面值。

## 端点预览几何失效与依赖刷新

运行 `profile-editor 1000`，使用同上述静态 Release 环境。`flow_endpoint_preview_dependents` 使用真实 View 在两个 Stock 之间反复拖动并释放 Flow 目标端点，每次包含按下、预览、正式 reconnect 和 Scene 通知；Flow 外另有长距离 Auxiliary→Flow Connector，用于覆盖依赖路径刷新。每轮校验最终 Flow 端点、预览状态清除和 Arrow 末端与 Flow 边界一致。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| flow_endpoint_preview_dependents | 1000 | 47.1443 | 47.1443 |

回归测试直接收集 `QGraphicsScene::changed`，要求 Flow 预览覆盖依赖 Arrow 的旧/新实际绘制区域，并要求 Connector 跨越原 Stock 释放后覆盖完整预览边界；另比较连续拖动后的画面与强制全量重绘，防止只验证最终模型而遗漏像素残影。

## 内联名称编辑

运行 `profile-editor 1000`，使用同上述静态 Release 环境。`inline_rename` 每轮通过真实 View 双击 Stock、创建 QGraphicsProxyWidget 内联编辑器、输入名称并按 Enter 提交，再于计时外 undo 恢复。计时包含编辑器创建、场景交互、ChangeSet 和 Item 刷新，不包含撤销清理。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| inline_rename | 1000 | 5521.21 | 5521.21 |

回归覆盖 Stock 和 Flow、非法重名、Esc 取消、undo/redo 以及属性面板名称/方程/单位/说明的完整恢复。内联编辑器从 Scene 移除后延迟销毁，避免在 `editingFinished` 事件栈内同步删除发送者。

## 窗口布局保存与恢复

运行 `profile-editor 1000`，使用同上述静态 Release 环境。`window_layout_restore` 执行 100 次真实 MainWindow 关闭和重新构造：关闭时经 QSettings 写入 geometry/state，新窗口从同一 INI 文件恢复；校验恢复后的客户区尺寸与完整 Dock 状态。原窗口实际显示并等待窗口系统处理，计时包含关闭、QSettings 读写和新窗口构造。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| window_layout_restore | 100 | 13777.2 | 137772 |

单元测试使用隔离 QSettings 目录，覆盖窗口 geometry 原始字节、Dock 隐藏/浮动状态、稳定 objectName 和第二次启动恢复，不读写开发者真实配置。profile 的尺寸断言使用关闭前实际尺寸，避免不同平台窗口管理器对未显示窗口 frame 的调整影响结果。

## 元素字体编辑

运行 `profile-editor 1000`，使用同上述静态 Release 环境。`font_style_edit` 每轮通过 DocumentController 修改 Stock 的字体族和字号，校验 DiagramDefinition 与 Item 的 QFont 投影，再在计时外 undo。QDS profile 额外写入 Arial 18，并按名称查找加载后的 Stock 验证字体，避免依赖原生文件内 uid 重排。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| font_style_edit | 1000 | 1.5564 | 1.5564 |

单元测试覆盖核心 FontStyle、非法对象、负字号、capture/restore、Document 移动、属性面板、undo/redo、复制粘贴、QDS 往返和默认字体恢复。字体只进入原生 QDS；XMILE 行为不变。

## PNG 渲染、导出与剪贴板

运行 `profile-editor 1000`，使用同上述静态 Release 环境。渲染夹具包含 20 个 Stock/Auxiliary 和 19 条 Connector。`png_render` 执行 100 次完整内容渲染；`png_selection_render` 执行 100 次单元素选中渲染并校验其宽度小于完整模型且选择状态恢复；`png_file_export` 执行 20 次真实 PNG 写盘；`png_clipboard_copy` 使用选中图像执行 100 次系统剪贴板写入及完整图像读回校验，结束后恢复原剪贴板。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| png_render | 100 | 1911.10 | 19111.0 |
| png_selection_render | 100 | 4.5866 | 45.866 |
| png_file_export | 20 | 3794.72 | 189736 |
| png_clipboard_copy | 100 | 589.474 | 5894.74 |

单元测试覆盖完整内容尺寸、有效像素、无选中色、网格/选择/句柄状态恢复、真实 PNG 文件格式、剪贴板读回和恢复；另将未选中 Auxiliary 放在选中 Stock 的渲染边界内，验证逐项 QImage 绘制不会带入该元素，区别于整场景裁剪。offscreen 平台初始 `mimeData()` 可为空，测试保存剪贴板时显式处理，不改变生产复制路径。

## 自定义框选更新

运行 `profile-editor 1000`，使用同上述静态 Release 环境。`rubber_band_resize` 通过真实 View 在最大选区和缩小选区之间反复移动，覆盖自定义 QFrame 几何更新、旧/新区域 update 和 `setSelectionArea()`；每轮校验归一化后的选区矩形。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| rubber_band_resize | 1000 | 1.2199 | 1.2199 |

回归使用 MainWindow 中央 View 和显式 button/buttons 鼠标事件，检查反向缩小、完整旧选区 Paint（Windows 插件）、帧图像与全量重绘一致以及释放后隐藏。offscreen 对子 Widget 的 Paint 区域合成方式不同，因此仅对 Windows 插件断言完整区域，offscreen 仍执行同一几何和像素场景。

## 光标与视口状态显示

运行 `profile-editor 1000`，使用同上述静态 Release 环境。`cursor_status_update` 通过真实 View 鼠标移动更新状态栏，逐轮校验光标场景坐标文本，覆盖坐标换算和 QLabel 刷新；视口原点及 zoom/scrollbar 更新由单元测试覆盖。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| cursor_status_update | 1000 | 2.1131 | 2.1131 |

## Undo 真实屏幕重绘

运行 `profile-editor 1000`，使用同上述静态 Release 环境。`move_undo_repaint` 使用 `tests/data/move-undo.qds` 中同时连接 succumbing 的多个 Connector，在 66% 缩放下将 contact_infectivity 纵向移动 400 场景单位后 undo；每轮通过 `QScreen::grabWindow()` 比较恢复帧与初始帧，覆盖 FullViewportUpdate、Scene 依赖刷新和真实屏幕捕获。

| 操作 | 数量 | 总耗时 ms | 每操作 μs |
| --- | ---: | ---: | ---: |
| move_undo_repaint | 20 | 212.507 | 10625.4 |

对照测试表明 `grab()` 会重新渲染 Widget，可能掩盖 backing store 残影；Minimal/BoundingRect 模式仍可能出现旧 Connector，而 FullViewportUpdate 能恢复一致画面。该策略只重绘视口，不增加 Document 或 Route 计算。
