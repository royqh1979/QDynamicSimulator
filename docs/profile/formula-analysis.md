# 公式分析性能基线

## 公式分析、标量求值与数组模型

工具：`profile-formula 1000`。构建：MinGW GCC 8.1.0、Qt 5.15.18 静态 Release，Windows 11，R: 内存盘。

每轮构造一个包含变量引用、乘法、ABS 调用和 IF THEN ELSE 的公式，执行完整 `analyzeFormula()`，并验证 AST 存在、无诊断和节点计数。1000 个公式共产生 11000 个 AST 节点。模型分析基准另外创建 1 个 Stock、1000 个具名公式和 5000 个输入 Auxiliary；每个公式有 6 个不同引用，最终验证 6000 个引用和 6000 条去重依赖边。求值基准复用这些已解析 AST，通过 EntityId 哈希表读取变量，验证 1000 个结果均为有限值并输出校验和。数组基准创建 1000 个 2×3 non-Apply-to-All 数组，每个数组 6 个公式引用同一个输入变量，验证 6000 棵元素 AST 和 1000 个去重变量引用。

| 操作 | 数量 | 总耗时 ms | 每公式 μs |
| --- | ---: | ---: | ---: |
| formula_analyze | 1000 | 2.486 | 2.486 |
| formula_nodes | 11000 | — | — |
| document_analysis | 1000 | 4.520 | 4.520 |
| formula_references | 6000 | — | — |
| formula_dependencies | 6000 | — | — |
| formula_evaluate | 1000 | 0.219 | 0.219 |
| evaluation_checksum | -6775 | — | — |
| array_analysis | 6000 | 5.197 | 0.866 |
| array_references | 1000 | — | — |

`formula_analyze` 只覆盖标量 Lexer、Pratt Parser、AST 所有权和节点遍历；`document_analysis` 覆盖完整模型的 AST、nameKey 符号解析、诊断、结构使用和依赖图构建；`formula_evaluate` 只覆盖就绪 AST 的递归求值和变量读取，不包含解析或符号分析；`array_analysis` 覆盖 1000 个数组的 6000 个逐元素公式分析，其单公式耗时以 6000 为分母。各阶段是同一次运行的独立计时，不能相加。当前未引入缓存或派生索引，数组基准不包含数组求值。
