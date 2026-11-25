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

每个 ModelElement 独立解析；单公式失败只产生诊断，不阻断其他元素。AST 构建完成后不可变，后续检查共享只读访问。

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
    std::string nameKey;
    std::size_t minArguments = 0;
    std::size_t maxArguments = 0;
    FunctionCategory category;
};
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

## 8. 测试与性能

### 单元测试

- 字面量、中文/空格/转义名称、注释、无参函数、参数个数和未知符号。
- 每个优先级的 AST 形状，重点覆盖 `a^b^c`、`-x^2`、MOD、比较/等式、AND/OR。
- IF THEN ELSE、嵌套调用、空方程、非法输入及 SourceSpan。
- 引用收集、结构使用、公式引用使用、依赖环、Stock/Flow 不误判。
- 现有 SIR、中文名称、重名检查及整个 Validation UI 回归。
- 修改解析器时开启 `QDS_ENABLE_XMILE_CORPUS_TESTS`，验证全部 SDXorg XMILE 文件；不支持结构明确分类，不静默跳过。

### 可重复 profile

建立 `profile-formula`：固定生成标量公式，分别测量 lex/parse、符号解析、依赖图构建和总分析时间，报告数量、总耗时和单公式耗时。至少覆盖 1000 个不同复杂度公式，并验证 AST 节点数、引用数和图边数，避免编译器优化掉工作。

首个基线不预设未经验证的性能指标；后续优化必须比较同环境结果，不提前引入缓存、字符串池或派生索引。

## 9. 分阶段实施

1. **标量 AST**：Lexer、Pratt Parser、SourceSpan、基础节点和错误恢复，附完整测试和 profile。
2. **符号与依赖分析**：函数注册表、nameKey 解析、EquationDependency/StructuralUse、替换现有 validateDocument。
3. **求值准备**：复用 AST 和函数注册表实现标量求值、TIME/常量/基本运算及确定性测试。
4. **数组与高级语义**：Subscript、通配、转置、容器和 Apply-to-All，先补模型表示再实现求值。
5. **宏与兼容方言**：命名模型、厂商命名空间和 Vensim 差异，逐项引用权威资料，不凭经验补语法。

每个阶段独立完成功能实现、单元测试、profile、Release 后 Debug 全量回归和审核，不把后续求值或数组工作提前混入首阶段。
