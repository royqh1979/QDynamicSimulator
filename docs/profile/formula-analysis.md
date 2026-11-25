# 公式分析性能基线

## 标量 AST、符号分析与求值

工具：`profile-formula 1000`。构建：MinGW GCC 8.1.0、Qt 5.15.18 静态 Release，Windows 11，R: 内存盘。

每轮构造一个包含变量引用、乘法、ABS 调用和 IF THEN ELSE 的公式，执行完整 `analyzeFormula()`，并验证 AST 存在、无诊断和节点计数。1000 个公式共产生 11000 个 AST 节点。模型分析基准另外创建 1 个 Stock、1000 个具名公式和 5000 个输入 Auxiliary；每个公式有 6 个不同引用，最终验证 6000 个引用和 6000 条去重依赖边。求值基准复用这些已解析 AST，通过 EntityId 哈希表读取变量，验证 1000 个结果均为有限值并输出校验和。

| 操作 | 数量 | 总耗时 ms | 每公式 μs |
| --- | ---: | ---: | ---: |
| formula_analyze | 1000 | 2.561 | 2.561 |
| formula_nodes | 11000 | — | — |
| document_analysis | 1000 | 4.235 | 4.235 |
| formula_references | 6000 | — | — |
| formula_dependencies | 6000 | — | — |
| formula_evaluate | 1000 | 0.235 | 0.235 |
| evaluation_checksum | -6775 | — | — |

`formula_analyze` 只覆盖标量 Lexer、Pratt Parser、AST 所有权和节点遍历；`document_analysis` 覆盖完整模型的 AST、nameKey 符号解析、诊断、结构使用和依赖图构建；`formula_evaluate` 只覆盖就绪 AST 的递归求值和变量读取，不包含解析或符号分析。三者的单公式耗时均以 1000 个非空公式为分母，是同一次运行的独立计时，不能相加。当前未引入缓存或派生索引。
