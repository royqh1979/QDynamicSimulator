# 公式分析器设计

## 1. 目标与边界

目标是在 `qds-core` 中提供独立于 UI 的公式词法、语法、符号和依赖分析，为方程检查、执行排序及后续仿真求值提供同一套权威表示。核心可以使用 QtCore，容器优先采用合适的 Qt 容器，但不得依赖 Qt Widgets 或画布对象。

权威语法依据为 `docs/references/xmile-1.1.html` 第 3.2～3.6 节。首阶段覆盖标量表达式；数组、宏、厂商方言和求值分阶段实现，不把 Vensim 特有行为误写成 XMILE 要求。

一条 `ModelElement.equation` 对应一棵 AST，不把整个模型拼成一棵表达式树。Stock 的初值、Flow 的速率、Auxiliary 的计算式和 Constant 的常量表达式共用语法，语义由元素类型决定。空方程允许表示外生输入，不生成 AST，也不报错。

## 2. 整体分析流程

分析输入是完整的 Document，而不是逐行公式文本。Document 在编辑/读取阶段已经建立元素、维度、宏定义以及 EntityId/nameKey 索引；这些定义在文件中的先后位置不影响语义分析。`analyzeDocument()` 按以下管线执行：

```text
完整 Document
│
├─ 阶段 1：完整定义与结构准备
│  ├─ 遍历全部 Stock、Flow、Auxiliary、Constant、Data 定义
│  ├─ 读取各元素端点、数组维度、原始总体/逐元素 equation
│  ├─ 遍历全部 MacroDefinition，解析宏体和默认参数，生成 MacroAnalysis AST
│  ├─ 提供模型变量、Dimension、DimensionElement、MacroDefinition 的统一查找
│  └─ 收集 Connector、Stock/Flow 的结构关系，不在此阶段分析模型公式
│
├─ 阶段 2：公式 AST 与符号分析
│  ├─ 每个模型总体 equation / 逐元素 equation 独立 Lexer + Pratt Parser
│  ├─ 遍历阶段 1 宏体/默认参数 AST，按需解析局部参数与全局符号
│  ├─ 模型公式和宏体中的宏调用始终保留 CallNode，不做文本或 AST 展开
│  ├─ SymbolResolver：使用现有索引区分局部参数、变量、函数、维度和宏
│  ├─ 绑定只在当次遍历中使用，直接生成诊断、引用集合及公式/宏调用关系
│  └─ 生成 ElementAnalysis 并补全 MacroAnalysis，单公式错误不阻断其它公式
│
├─ 阶段 3：全模型派生分析
│  ├─ 在不展开 AST 的前提下计算宏依赖闭包，合并公式引用与结构使用
│  ├─ 检查未使用变量、数组元素完整性和标量代数环
│  ├─ 数组合法递推及元素级调度按既定边界单独处理
│  └─ 汇总 parse / symbol / arity / structural 等诊断
│
└─ ModelAnalysis
   ├─ MacroAnalysis：每个宏唯一的宏体、默认参数 AST 和直接依赖
   ├─ ElementAnalysis：总体/逐元素 AST 与引用集合
   ├─ DependencyGraph、StructuralUse、issues
   └─ 供 Validation、统一运行期宏求值和后续执行器共享只读使用
```

阶段 1 必须先读取完整 Document 中全部模型元素和宏定义，再开始阶段 2 的模型公式分析；不得把遍历到某个 Flow 时的结构收集和公式分析混在同一循环中。这样，模型公式可以引用文件后面定义的变量或宏，宏也可以引用其他宏，不需要前置声明。最终 Document 中不存在的变量或宏仍分别报告未定义变量或未知函数。

所有公式共用手写 Lexer 和 Pratt/precedence-climbing Parser，不引入第三方 parser 依赖。Pratt 实现右结合乘方、一元运算符、函数调用和 IF THEN ELSE，并保留每个节点与 QString 字符位置一致的 SourceSpan。宏采用自定义函数语义，而不是 C 预处理器宏语义：分析期不替换文本、不复制实参到宏体、不级联展开；非递归、直接递归和间接递归都通过同一运行期调用机制执行。

AST 在语法分析完成后完全不可变，只保存语法结构、规范化名称、字面量和 SourceSpan。Resolve 是独立的按需过程，不在 AST 中写入 EntityId、内置函数目标、宏定义、签名或其它语义绑定，也不生成另一份 Resolved AST。`validateDocument()` 只是返回 ModelAnalysis.issues 的薄接口，不另建检查流程。

## 3. AST 与结果所有权

```cpp
struct SourceSpan {
    int begin = 0; // QString 字符位置，含 begin，不含 end
    int end = 0;
};

class FormulaNode {
public:
    FormulaNodeKind kind() const;
    const SourceSpan &span() const;
    virtual ~FormulaNode() = default;
};

class NumberNode final : public FormulaNode {
public:
    double value() const;
};
class ReferenceNode final : public FormulaNode {
public:
    const std::string &nameKey() const;
};
class UnaryNode final : public FormulaNode {
public:
    UnaryOperator op() const;
    const FormulaNode *operand() const;
};
class BinaryNode final : public FormulaNode {
public:
    BinaryOperator op() const;
    const FormulaNode *left() const;
    const FormulaNode *right() const;
};
class CallNode final : public FormulaNode {
public:
    const std::string &nameKey() const;
    std::size_t argumentCount() const;
    const FormulaNode *argument(std::size_t position) const;
};
```

Conditional、Subscript、Range 等节点采用相同的私有状态和 const 子节点访问。集合节点只提供数量及按位置的 const 元素访问，不公开 `unique_ptr` 容器。FormulaAnalysis 私有持有 AST 和 issues，构建完成后只读；公式变化时替换整份结果。

上述代码块只表达 AST 的逻辑结构，不表示公开字段接口。正式类型必须通过私有状态和只读访问接口保证创建后不可变；可变构造仅属于 Parser 内部，外部不能取得可修改的子节点指针。FormulaAnalysis 和独立保留的 ModelAnalysis 也是完成后不可变的派生结果，构建期可变与完成后只读相互分离。FormulaIssue、ModelIssue、ArrayValue 和 EvaluationResult 同样按逻辑结果语义提供不可变边界；输入 DTO、EvaluationContext 和内部 Builder 不被迫采用同一接口。

`FormulaAnalysis` 保存 `unique_ptr<FormulaNode>`。`ModelAnalysis` 唯一拥有各元素公式 AST、每个宏的宏体/默认参数 AST、引用集合和诊断；同一宏体不得复制到调用方 AST。AST 不保存 Resolve 结果，`Formula.h` 也不为了实体绑定依赖 `Model.h`。DependencyGraph 作为独立分析结果可以保存 EntityId，因此 AST 或 Document 对象地址变化不会使图失效；EntityId 出现在依赖图中不属于 AST 语义绑定。

普通编辑阶段不提前引入绑定表、执行计划或其它解析缓存；当前调用方可以每次基于当前 Document 全量重新分析，这只是实现选择，不代表调用方在语义上依赖宏体副本。待 profile 证明重复名称解析确实是瓶颈后，才可设计 AST 之外、按定义修订号失效的执行缓存。

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

## 5. 按需符号解析与内置函数注册表

Resolve 是校验、依赖分析和求值需要名称语义时执行的遍历操作，不是必须保存的最终结果。Parser 不执行 Resolve；Resolver 使用当前作用域和现有索引完成查询，绑定只保存在当次调用的局部变量中，默认在遍历结束后丢弃，不回写 AST，也不建立平行名称索引或外部绑定表。

解析数据来源只有：

- 宏的局部参数及运行期调用帧；
- Document 已有的 `nameKey → ModelElement/EntityId` 索引；
- Document 已有的维度、维度元素索引；
- 内置函数签名注册表；
- 宏的 `nameKey + 签名 → MacroDefinition` 索引。宏是可调用定义，但不为了复用索引而伪装成 ModelElement。

内置函数注册表包含：

```cpp
struct FunctionSignature {
    std::size_t minArguments = 0;
    std::size_t maxArguments = 0;
    FunctionCategory category;
};

const FunctionSignature *findFunctionSignature(const std::string &nameKey);
```

注册表覆盖 XMILE 3.5 全部内置函数的名称和参数个数，包括数学、统计、延迟、测试输入、时间及其它函数。分析阶段可识别全部签名，求值阶段复用同一注册表分派实现，避免函数列表散落。

解析规则：

1. 变量引用在宏体内先查当前宏的局部参数，再查全局模型变量；局部参数不分配 EntityId，也不生成模型变量依赖。
2. 调用名称通过内置函数注册表和宏索引查询，按 nameKey、实参个数及默认参数规则即时选择目标；CallNode 始终只保存名称和实参，不保存调用目标。
3. 依赖分析把当次查询结果直接转换为 EntityId 引用、宏调用边或诊断；这些独立分析结果不是 AST 的字段。
4. 求值使用相同的名称规则和索引：局部参数读取当前调用帧，全局变量按 nameKey 读取，内置函数和宏按名称及实参个数分派。
5. 未知名称分别报告未定义变量或未知函数，不把函数名误报为变量。
6. 图形函数、命名模型、厂商命名空间和数组平坦索引保留语法结构，按实际支持情况处理或给出明确诊断，不静默猜测。

## 6. 依赖图与结构关系

DependencyGraph 的边统一为 **dependency → dependent**，表示必须先求值的公式依赖。只从 AST 的变量引用和函数参数生成，不包括文字、内置函数名或注释。

宏不是 ModelElement，不作为 DependencyGraph 的伪 EntityId 节点。分析器另以具体宏签名节点记录“宏体/默认表达式的全局变量引用”和“直接宏调用”，再计算传递依赖闭包；递归宏形成的签名环按不动点或强连通分量处理，不作为错误。调用方的公式依赖包括全部实参引用、实际使用的默认表达式全局引用，以及被调宏及其可达宏的全局变量引用。这样无需展开 AST，也不会漏掉嵌套或递归宏引入的模型变量依赖。

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
- 不支持的命名模型、厂商命名空间及其它尚未实现的语义。
- 宏签名冲突、未知宏、错误参数个数和运行期递归深度超限；合法直接或间接递归不产生分析期错误。
- 代数依赖环、未使用变量。
- 单位一致性检查后续复用同一诊断通道。

`validateDocument()` 改为调用 ModelAnalysis，不再保留现有逐字符公式扫描和平行括号检查。不得在旧实现与新 AST 解析器之间维护两套公式语义。

## 8. 标量求值与共享函数注册表

函数签名集中在 `Function.h/.cpp`，Analysis 和 Evaluator 都通过 `findFunctionSignature()` 查询同一份名称、参数个数和分类。无参数内置函数也由签名派生，不再维护独立名称集合。注册表只描述语义，不保存求值状态。

`Evaluation.h/.cpp` 提供不依赖 Document 或 Qt UI 的同步标量求值，可使用 QtCore 容器：

```cpp
using VariableReader = std::function<std::optional<double>(const std::string &nameKey)>;

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

变量通过 `ReferenceNode.nameKey` 和 `readVariable` 读取，读取器内部直接使用当前 Document 的既有名称索引；AST 不保存实体 ID。读取器缺失、名称未定义或变量无值均返回带源码位置的错误，不以零代替。Result 不持有 AST 或 Context 指针，调用方继续拥有输入对象。

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
using VariableReader = std::function<std::optional<Value>(const std::string &nameKey)>;
using DimensionReader = std::function<const Dimension *(const std::string &)>;

struct EvaluationScope {
    std::vector<std::string> dimensions; // 当前方程输出维度
    std::vector<std::size_t> indices;    // 为空表示整体求值，否则为当前元素零基下标
    std::string self; // 当前方程元素的 nameKey，供 SELF 与普通变量共用读取入口
};

struct EvaluationContext {
    double time, dt, startTime, stopTime;
    VariableReader readVariable;
    DimensionReader readDimension;       // 返回对象仅需在同步调用期间存活
    EvaluationScope scope;
};
using EvaluationResult = std::optional<Value> + error/errorSpan;
```

ArrayValue 的维度长度等于 shape 长度，元素数等于各维长度乘积，Document/Evaluator 在边界检查这些不变量。调用方拥有变量和 Dimension 对象，Result 只持有值，不保存指针。`readVariable` 与标量阶段一样接收 nameKey，并通过现有索引定位当前值；下标和数组运算不依赖 AST 中的语义绑定。

### 10.2 下标、作用域与切片

普通下标是 1 起始的整数实数，非整数、非有限值或越界按 XMILE 返回 0。命名元素和 `Dimension.Element` 转换为对应位置。不足的下标按尾部通配处理；全部下标为单值时返回 double，含通配/区间时返回保留所选维度的 ArrayValue，数据仍按行主序。

维度名和 `@n` 依赖 EvaluationScope：指定当前元素下标时转为单值；整体求值时相当于沿该维通配，使 Apply-to-All 公式能保留输出维度。`@n` 取当前方程第 n 个输出维度。转置反转维度和 shape，并重排行主序数据，而不是只改显示。

### 10.3 广播与逐元素语义

标量对数组广播。数组间运算要求维度集合一致，顺序可以不同；按名称重排到输出维度，不仅按位置比较 shape。当前方程提供 scope.dimensions 时以它为输出顺序，否则采用数组操作数的维度顺序。维度集合不一致返回错误，不隐式引入 sub-range。

算术、比较、逻辑、一元数学函数和双参数 MAX/MIN 逐元素执行；标量条件保持短路。数组条件的 IF/AND/OR 逐元素求值，不能沿用标量短路。数组条件分支和逻辑结果仍使用 0/1 double。

单参数 MAX/MIN、SUM、MEAN、STDDEV 对切片归约为 double，STDDEV 使用总体标准差（除以 N）；SIZE 返回元素数。RANK 返回按行主序平坦位置，可选次级排序数组先按维度名对齐。整体求值可直接计算普通 Apply-to-All 数组运算；需要按当前元素归约的公式由调用方设置 scope.indices 逐点调用，后续执行器负责遍历，不在 Evaluator 内隐藏循环状态。

### 10.4 Vensim 数组初始化列表

兼容 Vensim 在 eqn 中使用的扁平逗号初始化列表，新增 `ArrayLiteralNode` 保存有序标量表达式。列表只在公式根节点解释，逗号在函数参数和下标中仍保持原语法。求值必须有 scope.dimensions，按输出 shape 校验元素总数，数据按行主序生成 ArrayValue；指定 scope.indices 时返回当前元素标量。列表项允许表达式但不得返回数组，元素数不足或过多均报错，不做隐式截断/补零。

随机、延迟、历史状态、图形函数、队列/Conveyor 和命名模型仍未实现。宏按第 11 节通过统一运行期调用支持标量和数组 Value，不使用另一套数组宏展开规则。元素级依赖调度沿用上一阶段的明确边界，不因数组值可求值就声称完整仿真排序已完成。

## 11. 表达式宏

### 11.1 语义与唯一表示

宏采用**自定义函数语义**，不是 C 预处理器式的文本替换。它支持参数、默认参数、局部作用域、嵌套调用、直接递归和间接递归，返回表达式求值得到的 Value。非递归与递归宏不得具有两套调用语义。

Document 以值对象保存 MacroDefinition；宏和参数都有 displayName/nameKey，参数可保存默认表达式。宏不是 ModelElement，不分配 EntityId 或画布 Item。按 nameKey + 实参个数及默认参数规则选择具体定义，支持 XMILE 的参数个数重载；同一签名不可重复，默认参数之后不得再出现必填参数。宏名与模型变量、维度相互排斥，参数在宏体内为局部名称并遮蔽同名全局变量。

每个 MacroDefinition 在 ModelAnalysis 中只有一个 MacroAnalysis，唯一保存该定义的宏体和默认参数 AST。模型公式及宏体中的每次宏调用只保存 CallNode 和实参 AST；**任何阶段都不得把宏体或默认参数复制到调用方，也不做文本替换、AST 替换或级联展开。** 这既是非递归调用的表示，也是递归调用的表示。

### 11.2 分析过程

宏接入第 2 节的统一管线，不另建宏展开流程：

1. 阶段 1 先收集全部宏定义，分别解析宏体和默认表达式，建立 nameKey/签名索引。定义顺序无关，互相引用不需要前置声明。
2. 阶段 2 遍历宏体，按需查询局部参数、全局变量、内置函数和其它宏调用；查询结果直接形成 MacroAnalysis 的引用集合、调用关系和诊断，不回写宏体 AST。
3. 模型公式中的宏调用即时校验目标签名，CallNode 本身不变，也不保存目标。参数绑定发生在运行期，不把实参 AST 填入宏体。
4. 阶段 3 按第 6 节计算宏的传递变量依赖。直接递归、间接递归和重载调用都按具体签名建边；递归环是合法调用关系，不再产生 `recursive-macro` 诊断。
5. 未找到定义仍报告未知函数；签名重复、参数个数不匹配和非法默认参数仍报告普通分析诊断。单个宏或公式出错不阻断其它定义的分析。

### 11.3 统一运行期调用

求值器通过 MacroReader 按 nameKey + 实参个数取得当前 MacroAnalysis 中的宏体和默认参数 AST。`makeMacroReader(analysis)` 只捕获 Analysis 指针；求值期间 Document 和 ModelAnalysis 都必须存活且不得修改，Result 不持有二者的指针。

每次调用建立独立宏调用帧，形参绑定采用按值语义：

1. 全部实参先在调用方作用域求值，每个实参只求值一次，再以 Value 绑定到被调宏的局部形参。即使形参在宏体中出现多次，也不重复读取变量或执行实参表达式。
2. 缺失实参使用对应默认表达式。默认表达式在进入当前宏调用帧后求值，可以引用该宏已经绑定的前参；每次调用只对实际使用的默认表达式求值一次。
3. 被调宏的默认表达式和宏体不能动态读取调用方的局部参数，只能访问自己的局部参数、全局变量和共享求值上下文。
4. 宏体中的局部参数按 nameKey 从当前调用帧读取；未命中局部参数时，全局变量通过 VariableReader 按 nameKey 查询现有实体索引。嵌套和递归调用共享时间、数组、维度及其它 EvaluationContext 状态，但各自拥有独立局部参数帧。
5. 宏体求值结果就是 CallNode 的值。标量和数组使用相同协议，不引入宏专用数组表示。

所有宏调用共用深度计数器，直接递归、间接递归和普通嵌套调用均计入。最大宏调用深度为 64，超限返回带调用位置的求值错误，不能以进程栈溢出代替诊断。条件的短路与递归终止遵循普通表达式语义，不为递归宏增加特殊求值路径。

### 11.4 宏修改与失效边界

宏定义是宏体的唯一事实来源，调用方没有宏体副本。因此修改宏体或默认表达式后，只需重新生成该定义的 MacroAnalysis，并更新受影响的依赖闭包、诊断和将来可能存在的求值缓存；调用方 AST 保持原样，不做级联失效检查或级联重新展开。所有后续调用通过 MacroReader 使用重分析后的同一份宏体。

如果修改的是宏名、参数个数或默认参数布局，则需要重新解析相关 CallNode 的目标签名并更新参数个数诊断，但仍不修改调用方 AST 的表达式结构。变量重命名等普通编辑按统一符号解析规则处理，不为宏另建名称迁移通道。

当前每次编辑后全量重建 ModelAnalysis 仍是允许的简单实现，但它不是级联展开，也不要求调用方保存宏体。未来只有 profile 证明全量分析或运行期调用是瓶颈时，才引入增量分析、编译或缓存；这些优化必须保留宏定义的唯一权威表示，并按定义修订关系失效，禁止以保存内联宏体副本的方式优化。

### 11.5 持久化与边界

QDS/XMILE 使用 macro、parm、eqn 保存同一定义，不持久化展开结果或调用帧。真实 SDXorg 文件若只有调用而外部宏定义缺失，仍明确报告未知函数而不伪造定义。含 Stock/局部变量的宏、外部 include、命名模型和厂商命名空间后续单独实现。

## 12. 测试与性能

### 单元测试

- 字面量、中文/空格/转义名称、注释、无参函数、参数个数和未知符号。
- AST 只保存语法结构、nameKey、字面量和 SourceSpan；校验、依赖分析和求值前后节点结构及源码位置不变，同一 AST 可在不同索引上下文中重新 Resolve，不携带旧实体或调用目标。
- 每个优先级的 AST 形状，重点覆盖 `a^b^c`、`-x^2`、MOD、比较/等式、AND/OR。
- IF THEN ELSE、嵌套调用、空方程、非法输入及 SourceSpan。
- 引用收集、结构使用、公式引用使用、依赖环、Stock/Flow 不误判。
- 按 nameKey 读取变量、实数与 0/1 逻辑值、负数 MOD、时间常量、数学函数、条件短路、Inf/NaN、变量缺失和未支持语义。
- numbered/named 维度、名称冲突、下标范围与重复检查、Wildcard/Range/@ AST、维度元素解析、逐元素依赖、Capture/Restore 及 QDS/XMILE 往返。
- 下标越界、命名元素、切片、转置、维度重排广播、逐元素作用域、数组条件、归约/RANK、切片内数学运算及不兼容维度错误。
- 数组初始化列表 AST、表达式列表项、行主序整体/逐元素求值、真实 Vensim 初始化模型及元素数/维度错误。
- 宏签名冲突、参数个数重载、默认参数引用前参、局部遮蔽、全局依赖及 QDS/XMILE 往返。
- 模型公式和宏体中的调用始终保留 CallNode，宏体/默认参数 AST 不复制；直接、间接嵌套调用及宏依赖闭包不需要展开即可得到正确结果。
- 实参在形参被多次引用时只求值一次，默认参数按调用求值且不能读取调用方局部参数；标量和数组实参均遵守相同规则。
- 有终止条件的直接递归、间接递归、同名不同签名调用、深度 64 边界和超限错误；合法递归不产生 `recursive-macro`。
- 修改宏体或默认表达式后所有调用使用新定义，调用方 AST 结构无需变化且仍只含原 CallNode；修改签名后只重做相关调用解析，不发生级联展开。全量重建时允许 AST 对象地址变化。
- 现有 SIR、中文名称、重名检查及整个 Validation UI 回归。
- 修改解析器时开启 `QDS_ENABLE_XMILE_CORPUS_TESTS`，验证全部 SDXorg XMILE 文件；不支持结构明确分类，不静默跳过。

### 可重复 profile

建立 `profile-formula`：固定生成标量公式，分别测量 lex/parse、完整模型符号与依赖分析、纯语法 AST 求值，报告数量、总耗时和单公式耗时。求值数据包含使用现有索引进行名称解析的开销，不以预先写入 AST 的绑定规避测量。至少覆盖 1000 个不同复杂度公式，并验证 AST 节点数、引用数、图边数、求值结果数和校验和，避免编译器优化掉工作。

宏 profile 与普通公式共用可重复工具，分别测量不同调用深度、参数个数、默认参数、标量/数组实参、直接递归和间接递归的分析及运行期调用耗时，同时校验返回值、实参读取次数和 AST 节点数。必须确认节点数不随调用链级联增长。若以后实现增量分析，再单独测量宏修改后的失效与重分析成本；不得用展开后的求值速度掩盖维护成本。

首个基线不预设未经验证的性能指标；后续优化必须比较同环境结果，不提前引入缓存、字符串池或派生索引。

## 13. 分阶段实施

1. **标量 AST**：Lexer、Pratt Parser、SourceSpan、基础节点和错误恢复，附完整测试和 profile。
2. **符号与依赖分析**：函数注册表、基于现有索引的按需 nameKey 解析、EquationDependency/StructuralUse、替换现有 validateDocument；解析绑定不写入 AST。
3. **求值准备**：复用 AST 和函数注册表实现标量求值、TIME/常量/基本运算及确定性测试。
4. **数组模型与分析**：Dimension、Apply-to-All/逐元素方程、Subscript、通配、区间、@ AST、符号依赖和 QDS/XMILE 持久化。
5. **数组与高级求值**：带维度信息的 ArrayValue、下标/切片、转置、Apply-to-All 广播、数组函数及元素级执行排序。
6. **统一宏调用**：移除非递归 AST 展开，保留所有 CallNode，完成局部调用帧、默认参数、依赖闭包、递归深度限制及宏修改失效边界。
7. **兼容方言与外部定义**：命名模型、外部 include、厂商命名空间和 Vensim 差异，逐项引用权威资料，不凭经验补语法。

每个阶段独立完成功能实现、单元测试、profile、Release 后 Debug 全量回归和审核，不把后续求值或数组工作提前混入首阶段。
