# 公式分析性能基线

## 标量 AST 解析

工具：`profile-formula 1000`。构建：MinGW GCC 8.1.0、Qt 5.15.18 静态 Release，Windows 11，R: 内存盘。

每轮构造一个包含变量引用、乘法、ABS 调用和 IF THEN ELSE 的公式，执行完整 `analyzeFormula()`，并验证 AST 存在、无诊断和节点计数。1000 个公式共产生 11000 个 AST 节点。

| 操作 | 数量 | 总耗时 ms | 每公式 μs |
| --- | ---: | ---: | ---: |
| formula_analyze | 1000 | 2.349 | 2.349 |
| formula_nodes | 11000 | — | — |

该结果只覆盖标量 Lexer、Pratt Parser、AST 所有权和节点遍历，不包含符号解析、依赖图或求值。后续阶段应使用同一公式族比较，不把不同阶段的时间直接相加。
