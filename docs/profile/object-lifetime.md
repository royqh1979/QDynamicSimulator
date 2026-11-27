# UI 对象生命周期性能与压力验证

## 方法与场景

- 构建：MinGW GCC 8.1.0，Qt 5.15.18 静态 Release；profile 直接运行 Release 可执行文件，不在调试器中计时。
- 命令：`profile-editor 1000` 连续三轮，`profile-simulation 1000` 一轮。
- 每轮编辑器 profile 新增四类各 100 次生命周期操作：
  - 创建真实 MainWindow、选中带 Flow 的 Stock、处理事件并销毁；
  - Controller/Scene/PropertyPanel/MiniMapView 按两种顺序交替销毁；
  - 选中 Connector 时替换 Document 并重建投影；
  - 在普通 QGraphicsScene 中删除 RoutedItem 的一个端点并重新计算几何。
- 结果表绑定覆盖 QVector 值返回及列内 times/values 值语义；仿真 profile 验证标量/数组序列查询和既有数值校验。

## 三轮结果

| 操作 | 第 1 轮总 ms / 每操作 μs | 第 2 轮总 ms / 每操作 μs | 第 3 轮总 ms / 每操作 μs |
| --- | ---: | ---: | ---: |
| window_lifecycle，100 次 | 48382.3 / 483823 | 52368.5 / 523685 | 47513.7 / 475137 |
| controller_scene_lifecycle，100 次 | 351.390 / 3513.90 | 330.453 / 3304.53 | 337.749 / 3377.49 |
| document_reset_lifecycle，100 次 | 685.568 / 6855.68 | 209.838 / 2098.38 | 217.508 / 2175.08 |
| routed_endpoint_lifecycle，100 次 | 1.2818 / 12.818 | 1.2766 / 12.766 | 1.2998 / 12.998 |
| results_table_bind，1000 次 | 1.0186 / 1.0186 | 0.9941 / 0.9941 | 1.0113 / 1.0113 |

真实窗口场景包含窗口显示、事件处理、QSettings 布局恢复及完整析构，不能把总耗时解释为 QPointer 自身开销。第一轮文档重置还包含较多首次事件处理；后两轮处于同一数量级。结果表绑定约 1 ms/1000 次，未出现逐单元格名称查询或主动深拷贝造成的异常开销。

三轮均返回 0，累计 300 次各类生命周期操作未发生崩溃。五轮动态 Qt Debug 全量 CTest 也均为 18/18，通过 500 次 `propertyPanelDestruction()` 真实窗口迭代；Release 定向及全量中的相同测试另执行 200 次。上述结果只证明覆盖场景未再复现，不把概率性未复现夸大为所有潜在生命周期问题均不存在。

## 仿真值返回 profile

`simulation_result_lookup` 为 1000 次共 0.0462 ms；Euler、RK4、数组结果和全部 checksum 校验通过。接口迁移没有改变数值结果。

## 环境说明

三轮编辑器 profile 的既有 PNG/剪贴板阶段均出现 102 行 `Retrying/Unable to obtain clipboard` 或 `OleSetClipboard` 警告，profile 自身校验仍返回 0。Release 全量 CTest 的 `pngExportAndClipboard()` 因 Windows `OpenClipboard` 失败而未通过；同一用例单独重跑仍失败。读取系统状态可见剪贴板所有者为 WindowsTerminal，但查询时 `GetOpenClipboardWindow()` 为 0，不能据此断言具体占用进程。该环境失败与新增生命周期场景分开记录，不作为跳过测试或宣称 Release 全量通过的理由。

复现时使用项目 `all-profile-targets` 构建并直接运行上述命令；输出为 CSV。新增场景的计数包含在 `profile-editor` 的最终 valid 校验中，任何场景未执行或几何失效都会返回非零。
