# Vensim MDL 导入 Profile

## 用途与输入

`profile-mdl` 直接测量 Release 导入器的完整文本解析、名称转换、Document 建立和 Sketch 映射，不在调试器中计时，不设超出当前需求的性能门槛。输入包括：

- `small_abs`：SDXorg ABS 模型，含 Stock、Flow、Connector 和 Sketch。
- `small_teacup`：SDXorg Teacup 模型，含带空格名称、单位/范围 warning 和仿真设置。
- `wfinv`：仓库内的 `tests/data/wfinv.mdl`，覆盖两个 Stock、三条 Flow、三个开放 Cloud 和 Sketch，验证不遗留孤立 Cloud。
- `medium_300_variables`：程序内确定性生成的 60 组 Stock、流入、流出、参数和辅助变量，共 300 个语义变量；开放端点计入输出元素数。
- 可选自定义路径：用于额外本机模型验证，不作为仓库测试依赖。

工具依赖 `QDS::IO`、QtCore 和 `3rdparty/test-models`。每次先做一次 warm-up 并验证导入成功，再重复完整导入；失败会写 stderr 并返回非零。

## 运行

在源码根目录使用 Release 构建：

```bash
cmake --build <RELEASE_BUILD_DIR> --target profile-mdl --parallel
<RELEASE_BUILD_DIR>/tests/profile/profile-mdl.exe 50
<RELEASE_BUILD_DIR>/tests/profile/profile-mdl.exe 1 "path/to/model.mdl"
```

第一个参数是每种输入的重复次数，默认为 50；第二个可选参数只测量指定 MDL 文件。输出为 CSV：

```text
operation,count,total_ms,per_operation_us,model_elements,warnings
```

`model_elements` 包含 Model 与 Diagram 元素及开放 Cloud；`warnings` 是单次导入的非致命降级数量。

## 2026-11-27 基线

环境：Windows 11、Qt 5.15.18 static Release、GCC 8.1.0，直接运行 `R:/build-qds-include-release/tests/profile/profile-mdl.exe 50`。

| operation | count | total_ms | per_operation_us | model_elements | warnings |
| --- | ---: | ---: | ---: | ---: | ---: |
| small_abs | 50 | 7.4437 | 148.874 | 5 | 0 |
| small_teacup | 50 | 12.5931 | 251.862 | 8 | 3 |
| wfinv | 50 | 45.6634 | 913.268 | 32 | 1 |
| medium_300_variables | 50 | 312.979 | 6259.58 | 420 | 0 |

WFINV 使用仓库夹具 `tests/data/wfinv.mdl`，不再依赖本机 `R:/WFINV.MDL`。
