# 公式分析器设计

## 1. 目标与边界

目标是在 `qds-core` 中提供不依赖 Qt 的公式词法、语法、符号和依赖分析，为方程检查、执行排序及后续仿真求值提供同一套权威表示。

权威语法依据为 `docs/references/xmile-1.1.html` 第 3.2～3.6 节。首阶段覆盖标量表达式；数组、宏、厂商方言和求值分阶段实现，不把 Vensim 特有行为误写成 XMILE 要求。

一条 `ModelElement.equation` 对应一棵 AST，不把整个模型拼成一棵表达式树。Stock 的初值、Flow 的速率、Auxiliary 的计算式和 Constant 的常量表达式共用语法，语义由元素类型决定。空方程允许表示外生输入，不生成 AST，也不报错。

## 2. 分析流程

```text
UTF-8 equation
→ Lexer（Token + SourceSpan）
→ Pratt Parser（每个公式一棵 AST）
→ SymbolResolver（nameKey → EntityId、内置函数注册表）
→ DependencyGraph（依赖 → 被依赖）
→ ModelAnalysis（AST 集合、依赖图、诊断）
```

采用手写 Lexer 和 Pratt/ precedence-climbing Parser，不引入第三方 parser 依赖。Pratt 便于准确实现右结合乘方、一元运算符、函数调用和 IF THEN ELSE，同时保留每个节点的源码范围。

每个 ModelElement 独立解析；单公式失败只产生诊断，不阻断其他元素。AST 的节点类型、源码范围和子节点结构在语法分析完成后不可变；符号解析只填写 `ReferenceNode.entityId`，`ModelAnalysis` 返回后供后续检查共享只读访问。

## 3. AST 与结果所有权

```cpp
struct SourceSpan {
    std::size_t begin = 0; // UTF-8 byte offset，含 begin，不含 end
    std::size_t end = 0;
};

enum class UnaryOperator { Positive, Negative, Not, Transpose };
enum class BinaryOperator {
    Power, Multiply, Divide, Modulo, Add, Subtract,
    Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual, And, Or
};

struct FormulaNode {
    SourceSpan span;
    virtual ~FormulaNode() = default;
};

struct NumberNode : FormulaNode { double value; };
struct ReferenceNode : FormulaNode {
    std::string nameKey;
    EntityId entityId = 0; // 未解析时为 0，不保存对象指针
};
struct UnaryNode : FormulaNode {
    UnaryOperator op;
    std::unique_ptr<FormulaNode> operand;
};
struct BinaryNode : FormulaNode {
    BinaryOperator op;
    std::unique_ptr<FormulaNode> left;
    std::unique_ptr<FormulaNode> right;
};
struct CallNode : FormulaNode {
    std::string nameKey;
    std::vector<std::unique_ptr<FormulaNode>> arguments;
};
struct ConditionalNode : FormulaNode {
    std::unique_ptr<FormulaNode> condition;
    std::unique_ptr<FormulaNode> thenExpression;
    std::unique_ptr<FormulaNode> elseExpression;
};
struct SubscriptNode : FormulaNode {
    std::unique_ptr<FormulaNode> target;
    std::vector<std::unique_ptr<FormulaNode>> indices;
};
```

`FormulaAnalysis` 保存 `unique_ptr<FormulaNode>`。`ModelAnalysis` 唯一拥有各元素 AST、引用集合和诊断；DependencyGraph 只保存 EntityId，因此 AST 或 Document 对象地址变化不会使图失效。普通编辑阶段不提前引入缓存；调用方每次基于当前 Document 重新分析，待 profile 证明需要后再设计修订号缓存。

## 4. 标量语法

### 4.1 字面量与标识符

- 支持 XMILE 数字形式：`.375`、`14.`、`6E5`、`+8.123e-10`；正负号由一元运算符表达，不并入负数字面量。
- 未加引号的标识符遵循 XMILE 规则；中文、空格及其它 UTF-8 名称使用双引号及转义规则。
- Lexer 保留原始文本，Resolver 在反转义后调用现有 `normalizeIdentifier()` 生成 nameKey，不另建名称规范。
- `{}` 内联注释被跳过，不生成 AST；未闭合注释给出位置明确的错误。首版不支持嵌套注释，遇到 `{` 即进入注释、遇到 `}` 结束。

### 4.2 优先级与结合性

按 XMILE 3.3.1 从高到低处理：

| 优先级 | 形式 | 结合性 |
| --- | --- | --- |
| 下标 `[]` | 左结合 | 保留 AST，数组语义后续处理 |
| 括号 `()` | 不改变 AST 优先级 | 必要时保留 span |
| 乘方 `^` | 右结合 | `a^b^c = a^(b^c)` |
| 一元 `+`、`-`、`NOT` | 右结合 | `NOT NOT x`、`-x^2` 按规范优先级 |
| `*`、`/`、`MOD` | 左结合 | MOD 语义另按 Knuth floored modulus |
| `+`、`-` | 左结合 | |
| `<`、`<=`、`>`、`>=` | 左结合 | 返回 0/1 |
| `=`、`<>` | 左结合 | 返回 0/1 |
| `AND` | 左结合 | 返回 0/1 |
| `OR` | 左结合 | 返回 0/1 |

不擅自加入 C 的 `==`、`!=`、`&&`、`||` 或三目 `?:`。

### 4.3 函数与条件表达式

- 普通调用：`ABS(x)`、`MAX(x, y)`。
- 无参数内置函数可省略括号：`TIME`、`PI`、`INF`。
- 条件语句：`IF condition THEN expression ELSE expression`，生成 ConditionalNode。
- `if_then_else(condition, then, else)` 作为可选函数形式由注册表处理。
- Lookup、命名图形函数、命名模型和数组平坦索引均先形成 CallNode，再由 Resolver 区分，不在 Parser 中硬编码为变量引用。

## 5. 符号解析与内置函数注册表

`SymbolResolver` 只通过 Document 的统一 nameKey 索引解析变量，不重复实现名称查找。函数注册表包含：

```cpp
struct FunctionSignature {
    std::size_t minArguments = 0;
    std::size_t maxArguments = 0;
    FunctionCategory category;
};

const FunctionSignature *findFunctionSignature(const std::string &nameKey);
```

注册表覆盖 XMILE 3.5 全部内置函数的名称和参数个数，包括数学、统计、延迟、测试输入、时间及其它函数。分析阶段可识别全部签名，求值阶段复用同一注册表绑定实现，避免函数列表散落。

解析规则：

1. nameKey 命中内置函数时检查参数个数并生成诊断。
2. 命中模型变量时保留 ReferenceNode/CallNode 语义；图形函数和命名模型的执行语义后续处理。
3. 未知名称分别报告未定义变量或未知函数，不把函数名误报为变量。
4. 厂商命名空间、宏、命名模型和数组平坦索引先保留语法结构，给出明确的未支持诊断，不静默猜测。

## 6. 依赖图与结构关系

DependencyGraph 的边统一为 **dependency → dependent**，表示必须先求值的公式依赖。只从 AST 的变量引用和函数参数生成，不包括文字、内置函数名或注释。

以下关系分开保存，不能混成一种依赖：

- `EquationDependency`：公式引用，用于代数环和后续执行排序。
- `CausalLink`：Connector 的因果关系，用于界面展示和未使用判断，不直接决定求值顺序。
- `StockFlowLink`：Stock/Flow 端点及流入流出关系，用于存量计算和结构检查，不直接当普通公式依赖。
- `StructuralUse`：上述关系及公式引用共同用于“未连接或未引用”检查，因此只有公式引用的变量不再被误报为未使用。

`CausalLink` 的方向是被影响者依赖于影响者时，映射为 from → to；StockFlowLink 不盲目转成普通公式依赖，避免错误解释 Stock 与 Flow 的计算顺序。

## 7. 诊断与现有 Validation 集成

扩展现有 `ModelIssue`，增加可选的 `code`、`SourceSpan`，保留 entityId、severity、message 和现有 UI 显示方式。诊断至少覆盖：

- 非法字符、非法数字、未闭合引号/括号/注释、缺少操作数或参数。
- 未定义变量、未知函数、错误参数个数。
- 不支持的数组、宏、命名模型或厂商命名空间。
- 代数依赖环、未使用变量。
- 单位一致性检查后续复用同一诊断通道。

`validateDocument()` 改为调用 ModelAnalysis，不再保留现有逐字符公式扫描和平行括号检查。不得在旧实现与新 AST 解析器之间维护两套公式语义。

## 8. 标量求值与共享函数注册表

函数签名集中在 `Function.h/.cpp`，Analysis 和 Evaluator 都通过 `findFunctionSignature()` 查询同一份名称、参数个数和分类。无参数内置函数也由签名派生，不再维护独立名称集合。注册表只描述语义，不保存求值状态。

`Evaluation.h/.cpp` 提供不依赖 Document/Qt 的同步标量求值：

```cpp
using VariableReader = std::function<std::optional<double>(EntityId)>;

struct EvaluationContext {
    double time = 0.0;
    double dt = 1.0;
    double startTime = 0.0;
    double stopTime = 0.0;
    VariableReader readVariable;
};

struct EvaluationResult {
    std::optional<double> value;
    std::string error;
    SourceSpan errorSpan;
};

EvaluationResult evaluateFormula(const FormulaNode *node, const EvaluationContext &context);
```

变量通过已解析的 `ReferenceNode.entityId` 和 `readVariable` 读取；ID 未解析、读取器缺失或变量无值均返回带源码位置的错误，不以零代替。Result 不持有 AST 或 Context 指针，调用方继续拥有输入对象。

本阶段支持：

- 数字、变量引用、正负号、NOT、四则运算、乘方和 Knuth floored modulus。按 XMILE 3.2.3，公式只有实数一种数据类型，当前统一使用 double；要求整数的位置后续按数值约束检查，不另设整数表达式类型。
- 比较、等式、AND/OR，按 XMILE 3.3 让逻辑结果严格为 0 或 1，非零值视为 true。
- IF THEN ELSE 与 `if_then_else`；AND、OR 和条件表达式只读取需要求值的分支。
- TIME、DT、STARTTIME、STOPTIME、PI、INF，以及 ABS、ARCCOS、ARCSIN、ARCTAN、COS、EXP、INT、LN、LOG10、LOG、MAX、MIN、SIN、SQRT、TAN。

数学函数使用 C++ 标准库的 double 语义；除零和定义域错误不替换为零，允许按 IEEE 754 产生 Inf/NaN。MAX/MIN 的单参数形式对标量返回参数本身，双参数形式返回较大/较小值。

随机数、延迟、平滑、预测、测试输入、INIT/PREVIOUS/SELF、Lookup、命名模型、数组函数、下标和转置仍返回明确的未支持错误。它们需要随机状态、历史状态、图形函数或容器语义，不在本阶段伪造实现。后续仿真运行器复用 EvaluationContext 提供时间和变量存储，不在 Evaluator 内隐藏全局状态。

## 9. 数组模型、语法与分析

数组阶段先建立权威模型和持久化，再实现数组值求值；本阶段不把下标、通配或转置伪装成标量运算。

### 9.1 维度与数组变量

Document 拥有文档级 `Dimension` 值对象。维度具有 displayName、全局唯一 nameKey 和有序 `DimensionElement` 列表；numbered dimension 的元素保存 1 起始的显示序号和空 nameKey，named dimension 保存唯一的元素 displayName/nameKey。维度不是 ModelElement，不分配 EntityId，也不生成画布 Item。

维度通过统一 `m_dimensionIndex` 查找，与变量名称相互排斥；不同维度可以复用元素名，同一维度内不可重名。数组 ModelElement 保存有序维度 nameKey，允许同一维度重复出现以支持后续转置。维度一旦定义暂不提供删除或重命名，避免提前引入引用迁移复杂度。

```cpp
struct ArrayElementEquation {
    std::vector<std::size_t> indices; // 每个维度的零基下标
    std::string equation;
};

// ModelElement 新增：
std::vector<std::string> dimensions;
std::vector<ArrayElementEquation> elementEquations;
```

Apply-to-All 数组只使用 `equation`；non-Apply-to-All 数组只使用 `elementEquations`。下标按维度顺序保存零基整数，不保存显示名称，因此维度元素重命名不会使持久下标失效。编辑过程允许逐元素方程暂不完整，Validation 报告缺失/重复；XMILE 导入要求完整覆盖。`setEquation()` 与逐元素方程互斥，修改维度会清空旧逐元素方程。Capture/Apply/Restore 和重命名公式引用更新同时覆盖两类方程。

### 9.2 下标 AST

新增 `WildcardNode`、`RangeNode` 和 `DimensionPositionNode`，不把 `*`、`:`、`@n` 编码成伪函数或特殊 Reference。SubscriptNode 的每个 index 可以是普通表达式或以上节点；Range 两端仍是表达式。Lexer 增加 `:` 和 `@`，Parser 只在下标位置接受通配、区间和维度位置，其它位置继续报语法错误。已有 `Location.Boston` 保留为带点的 ReferenceNode nameKey。

### 9.3 符号与依赖分析

Resolver 依次区分模型变量、维度、下标上下文中对应维度的元素，以及 `Dimension.Element` 限定元素。维度/元素引用不生成 EntityId 依赖，模型变量引用继续生成依赖。Wildcard 和 DimensionPosition 不生成引用，Range 递归解析两端。

`ElementAnalysis` 增加带零基下标的逐元素 FormulaAnalysis；元素级 `references` 是所有方程引用的并集，结构使用检查覆盖全部公式。DependencyGraph 仍是变量级图，不能精确表达不同数组元素之间的执行顺序。本阶段遇到含数组变量的候选环时不报告 `algebraic-cycle`，避免把 `A[Dim-1]` 等合法递推误报为自环；元素级依赖与执行排序在数组求值阶段单独设计，不声称变量级图可完成数组调度。

数组值与求值见第 10 节。Vensim 逗号数组初始化列表作为明确的厂商扩展处理，不误标为 XMILE 标准表达式。

### 9.4 持久化

QDS 在 model 前保存完整维度定义，每个变量保存维度引用及零基加一的逐元素下标。XMILE 使用标准 `<dimensions>/<dim>/<elem>`，numbered dimension 使用 size；变量逐元素方程使用 `<element subscript="..."><eqn>...`。导出 Apply-to-All 与 non-Apply-to-All 时分别写总体 eqn 或 element，不复制平行表示。数组下标中的命名元素按 XMILE 标识符规则转义。

## 10. 数组值与求值

### 10.1 统一值与调用上下文

标量和数组共用一种求值结果，不维护数组专用 AST 或平行运算规则：

```cpp
struct ArrayValue {
    std::vector<std::string> dimensions; // 有序维度 nameKey
    std::vector<std::size_t> shape;
    std::vector<double> values;          // 行主序
};
using Value = std::variant<double, ArrayValue>;
using VariableReader = std::function<std::optional<Value>(EntityId)>;
using DimensionReader = std::function<const Dimension *(const std::string &)>;

struct EvaluationScope {
    std::vector<std::string> dimensions; // 当前方程输出维度
    std::vector<std::size_t> indices;    // 为空表示整体求值，否则为当前元素零基下标
    EntityId self = 0;
};

struct EvaluationContext {
    double time, dt, startTime, stopTime;
    VariableReader readVariable;
    DimensionReader readDimension;       // 返回对象仅需在同步调用期间存活
    EvaluationScope scope;
};
using EvaluationResult = std::optional<Value> + error/errorSpan;
```

ArrayValue 的维度长度等于 shape 长度，元素数等于各维长度乘积，Document/Evaluator 在边界检查这些不变量。调用方拥有 Variable/Dimension 对象，Result 只持有值，不保存指针。标量读取器和现有调用迁移到 Value 后仍走原运算实现。

### 10.2 下标、作用域与切片

普通下标是 1 起始的整数实数，非整数、非有限值或越界按 XMILE 返回 0。命名元素和 `Dimension.Element` 转换为对应位置。不足的下标按尾部通配处理；全部下标为单值时返回 double，含通配/区间时返回保留所选维度的 ArrayValue，数据仍按行主序。

维度名和 `@n` 依赖 EvaluationScope：指定当前元素下标时转为单值；整体求值时相当于沿该维通配，使 Apply-to-All 公式能保留输出维度。`@n` 取当前方程第 n 个输出维度。转置反转维度和 shape，并重排行主序数据，而不是只改显示。

### 10.3 广播与逐元素语义

标量对数组广播。数组间运算要求维度集合一致，顺序可以不同；按名称重排到输出维度，不仅按位置比较 shape。当前方程提供 scope.dimensions 时以它为输出顺序，否则采用数组操作数的维度顺序。维度集合不一致返回错误，不隐式引入 sub-range。

算术、比较、逻辑、一元数学函数和双参数 MAX/MIN 逐元素执行；标量条件保持短路。数组条件的 IF/AND/OR 逐元素求值，不能沿用标量短路。数组条件分支和逻辑结果仍使用 0/1 double。

单参数 MAX/MIN、SUM、MEAN、STDDEV 对切片归约为 double，STDDEV 使用总体标准差（除以 N）；SIZE 返回元素数。RANK 返回按行主序平坦位置，可选次级排序数组先按维度名对齐。整体求值可直接计算普通 Apply-to-All 数组运算；需要按当前元素归约的公式由调用方设置 scope.indices 逐点调用，后续执行器负责遍历，不在 Evaluator 内隐藏循环状态。

### 10.4 Vensim 数组初始化列表

兼容 Vensim 在 eqn 中使用的扁平逗号初始化列表，新增 `ArrayLiteralNode` 保存有序标量表达式。列表只在公式根节点解释，逗号在函数参数和下标中仍保持原语法。求值必须有 scope.dimensions，按输出 shape 校验元素总数，数据按行主序生成 ArrayValue；指定 scope.indices 时返回当前元素标量。列表项允许表达式但不得返回数组，元素数不足或过多均报错，不做隐式截断/补零。

随机、延迟、历史状态、图形函数、队列/Conveyor、宏和命名模型仍未实现。元素级依赖调度沿用上一阶段的明确边界，不因数组值可求值就声称完整仿真排序已完成。

## 11. 非递归表达式宏

Document 以值对象保存 MacroDefinition；宏和参数都有 displayName/nameKey，参数可保存默认表达式。宏不是 ModelElement，不分配 EntityId 或画布 Item。按 nameKey + 实参个数查找，支持 XMILE 的参数个数重载和尾部默认参数；同一签名不可重复，默认参数之后不得再出现必填参数。宏名与模型变量、维度相互排斥，参数在宏体内为局部名称。

分析时先解析宏及默认表达式，再把调用展开为新的 AST，不做字符串替换：

1. 按调用实参个数选择宏，将实参 AST 绑定到对应参数 nameKey。
2. 缺失实参使用该参数的默认 AST；默认表达式可引用此前已绑定参数。
3. 深拷贝宏体，仅替换局部 Reference，调用方变量名和其它宏名不被覆盖。
4. 嵌套调用重复展开，之后统一执行符号解析、依赖图和求值。

展开后的 ModelAnalysis 唯一拥有调用方 AST，宏定义与默认表达式 AST 由对应 MacroAnalysis 持有。参数实参在当前纯公式语义下按 AST 替换；随机、延迟等状态函数仍未实现，因此不引入“多次展开导致多次状态读取”的兼容行为。宏体内全局变量引用自然进入调用方依赖图，不建立伪 EntityId。

当前只支持非递归表达式宏。直接或间接递归返回 `recursive-macro` 诊断并保留未展开调用，不在编译期无限展开；递归宏、含 Stock/局部变量的宏、命名模型和厂商命名空间后续单独实现。QDS/XMILE 使用 macro、parm、eqn 保存同一定义，real SDXorg 文件若只有调用而外部宏定义缺失，仍明确报告未知函数而不伪造定义。

## 12. 测试与性能

### 单元测试

- 字面量、中文/空格/转义名称、注释、无参函数、参数个数和未知符号。
- 每个优先级的 AST 形状，重点覆盖 `a^b^c`、`-x^2`、MOD、比较/等式、AND/OR。
- IF THEN ELSE、嵌套调用、空方程、非法输入及 SourceSpan。
- 引用收集、结构使用、公式引用使用、依赖环、Stock/Flow 不误判。
- 已解析变量读取、实数与 0/1 逻辑值、负数 MOD、时间常量、数学函数、条件短路、Inf/NaN、变量缺失和未支持语义。
- numbered/named 维度、名称冲突、下标范围与重复检查、Wildcard/Range/@ AST、维度元素解析、逐元素依赖、Capture/Restore 及 QDS/XMILE 往返。
- 下标越界、命名元素、切片、转置、维度重排广播、逐元素作用域、数组条件、归约/RANK、切片内数学运算及不兼容维度错误。
- 数组初始化列表 AST、表达式列表项、行主序整体/逐元素求值、真实 Vensim 初始化模型及元素数/维度错误。
- 宏签名冲突、参数个数重载、默认参数引用前参、局部遮蔽、嵌套展开、全局依赖、递归诊断及 QDS/XMILE 往返求值。
- 现有 SIR、中文名称、重名检查及整个 Validation UI 回归。
- 修改解析器时开启 `QDS_ENABLE_XMILE_CORPUS_TESTS`，验证全部 SDXorg XMILE 文件；不支持结构明确分类，不静默跳过。

### 可重复 profile

建立 `profile-formula`：固定生成标量公式，分别测量 lex/parse、完整模型符号与依赖分析、就绪 AST 求值，报告数量、总耗时和单公式耗时。至少覆盖 1000 个不同复杂度公式，并验证 AST 节点数、引用数、图边数、求值结果数和校验和，避免编译器优化掉工作。

首个基线不预设未经验证的性能指标；后续优化必须比较同环境结果，不提前引入缓存、字符串池或派生索引。

## 13. 分阶段实施

1. **标量 AST**：Lexer、Pratt Parser、SourceSpan、基础节点和错误恢复，附完整测试和 profile。
2. **符号与依赖分析**：函数注册表、nameKey 解析、EquationDependency/StructuralUse、替换现有 validateDocument。
3. **求值准备**：复用 AST 和函数注册表实现标量求值、TIME/常量/基本运算及确定性测试。
4. **数组模型与分析**：Dimension、Apply-to-All/逐元素方程、Subscript、通配、区间、@ AST、符号依赖和 QDS/XMILE 持久化。
5. **数组与高级求值**：带维度信息的 ArrayValue、下标/切片、转置、Apply-to-All 广播、数组函数及元素级执行排序。
6. **宏与兼容方言**：命名模型、厂商命名空间和 Vensim 差异，逐项引用权威资料，不凭经验补语法。

每个阶段独立完成功能实现、单元测试、profile、Release 后 Debug 全量回归和审核，不把后续求值或数组工作提前混入首阶段。
