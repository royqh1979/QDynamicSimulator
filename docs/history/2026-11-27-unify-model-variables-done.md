# 统一模型 Variable 类型重构计划

创建时间：2026-11-27 13:17:28 +0800

## 目标

对齐 Vensim Variable 工具和 XMILE 广义 Auxiliary：Stock 与 Flow 保留独立语义，其余模型变量统一为 `ElementKind::Variable`。删除 Auxiliary、Constant、Data 三种平行元素类型及其画布局限，不通过枚举别名或兼容层保留旧分类。运算/显示类型由实际公式决定。

## 范围

- Core：ElementKind、默认名称、Document 校验、公式分析、模拟编译及 Run 元数据。
- App：统一 Variable 面板入口、工具和椭圆外观；删除 Constant/Data 专用工具和路径。
- I/O：XMILE `<aux>` 与原生 `<variable>` 均读写 Variable；不保留旧的 Auxiliary/Constant/Data 分类映射。
- 结果：保留“常量不进入结果变量下拉框”的既定交互，但改为由公式 AST 识别数值常量，不依赖 ElementKind。
- 测试与 profile：迁移现有场景，保留公式、运行、持久化和撤销行为；不通过跳过用例掩盖回归。

## 方法

1. 用单一 Variable 替换三种旧枚举值，优先使用编译错误发现所有分类分支。
2. 画布只保留一种 Variable 外形；面板和 Simulation 工具只保留一个 Variable 入口。
3. 使用共享公式分析识别数值字面量及一元正负号常量；RunVariableInfo 固定保存该显示属性，历史结果不依赖当前 Document。
4. 保留 Stock initialEquation、Flow 端点、名称、Route 和全部非分类语义。
5. 执行常规及全量 XMILE corpus、Release 后 Debug、编辑器与 XMILE profile。

## 验收

- 创建、改名、公式编辑、复制、删除、撤销、QDS/XMILE 往返和模拟均只使用统一 Variable。
- 数值常量仍不进入结果变量下拉框；其他公式变量正常显示、制表和绘图。
- 面板没有 Constant/Data/Auxiliary 三个并列类型，只有一个 Variable。
- 不保留旧枚举别名，不新增平行模型；文档与 NEWS 同步。

## 完成结果

- Auxiliary、Constant、Data 已删除并统一为 ElementKind::Variable；Stock/Flow 语义不变。
- 原生 QDS 使用 `<variable>`，XMILE 使用 `<aux>`；测试资源中的旧标签已迁移，不存在生产兼容层。
- 画布和工具面板只有单一 Variable 入口；RunVariableInfo 使用公式 AST 固定保存数值常量显示属性。
- 常量公式识别 profile 为 1000 次 0.8222 ms；核心、模拟和编辑器 profile 均通过。编辑器出现一次系统剪贴板占用警告，但退出码为 0。
- Release 17/17、Debug 17/17 和显式开启的完整 XMILE corpus 均通过，未跳过或排除语料。
