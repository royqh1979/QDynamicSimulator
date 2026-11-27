# 公式分析性能基线

## 公式分析、标量求值与数组模型

工具：`profile-formula 1000`。构建：MinGW GCC 8.1.0、Qt 5.15.18 静态 Release，Windows 11，R: 内存盘。

每轮构造一个包含变量引用、乘法、ABS 调用和 IF THEN ELSE 的公式，执行完整 `analyzeFormula()`，并验证 AST 存在、无诊断和节点计数。1000 个公式共产生 11000 个 AST 节点。模型分析基准另外创建 1 个 Stock、1000 个具名公式和 5000 个输入 Variable；每个公式有 6 个不同引用，最终验证 6000 个引用和 6000 条去重依赖边。求值基准复用这些已解析 AST，通过 EntityId 哈希表读取变量，验证 1000 个结果均为有限值并输出校验和。数组基准创建 1000 个 2×3 non-Apply-to-All 数组，每个数组 6 个公式引用同一个输入变量，验证 6000 棵元素 AST 和 1000 个去重变量引用。数组求值基准另建 1000 个 2×3 Apply-to-All 数组，执行标量广播与维度名下标，验证 6000 个输出值及校验和。初始化列表基准复用相同输出维度，对 1000 个六元素 ArrayLiteralNode 求值并验证 6000 个输出值及校验和。宏基准对 1000 个带默认实参的 Scale 调用分别测量 AST 展开与展开后求值，验证 1000 个结果及校验和。

| 操作 | 数量 | 总耗时 ms | 每公式 μs |
| --- | ---: | ---: | ---: |
| formula_analyze | 1000 | 2.501 | 2.501 |
| formula_nodes | 11000 | — | — |
| constant_formula_classification | 1000 | 0.8222 | 0.8222 |
| document_analysis | 1000 | 5.228 | 5.228 |
| formula_references | 6000 | — | — |
| formula_dependencies | 6000 | — | — |
| formula_evaluate | 1000 | 0.323 | 0.323 |
| evaluation_checksum | -6775 | — | — |
| array_analysis | 6000 | 6.388 | 1.065 |
| array_references | 1000 | — | — |
| array_evaluate | 1000 | 1.576 | 1.576 |
| array_values | 6000 | — | — |
| array_evaluation_checksum | 27000 | — | — |
| array_literal_evaluate | 1000 | 0.187 | 0.187 |
| array_literal_values | 6000 | — | — |
| array_literal_checksum | 21000 | — | — |
| macro_analysis | 1000 | 11.369 | 11.369 |
| macro_evaluate | 1000 | 0.503 | 0.503 |
| macro_checksum | 12000 | — | — |

`constant_formula_classification` 每轮分别确认一元正号数值为常量、含引用表达式不是常量，覆盖完整解析和 AST 判断。`formula_analyze` 只覆盖标量 Lexer、Pratt Parser、AST 所有权和节点遍历；`document_analysis` 覆盖完整模型的 AST、nameKey 符号解析、诊断、结构使用和依赖图构建；`formula_evaluate` 只覆盖就绪 AST 的递归求值和变量读取，不包含解析或符号分析；`array_analysis` 覆盖 1000 个数组的 6000 个逐元素公式分析，其单公式耗时以 6000 为分母；`array_evaluate` 覆盖 1000 个整体 Apply-to-All 数组求值，以 1000 为分母；`array_literal_evaluate` 覆盖 1000 个六元素初始化列表求值，同样以 1000 为分母；`macro_analysis` 和 `macro_evaluate` 分别以 1000 个宏调用为分母，前者包含 AST 展开与依赖分析，后者只包含展开后求值。各阶段是同一次运行的独立计时，不能相加。当前未引入缓存或派生索引。
