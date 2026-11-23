# 本机开发环境模板

复制本文件到 `docs/env/local/AGENTS.local.md` 后填写实际路径。该目录默认被 gitignore，不得提交本机配置。

## 工具链

- C/C++ 编译器目录：`<COMPILER_BIN>`
- Qt 5 前缀：`<QT_PREFIX>`
- Release 构建目录：`R:/build-qdynamic-simulator-qt5-release`

## 配置与构建

```bash
export PATH="<COMPILER_BIN>:<QT_PREFIX>/bin:$PATH"
cmake -S . -B R:/build-qdynamic-simulator-qt5-release \
  -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=<QT_PREFIX> \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build R:/build-qdynamic-simulator-qt5-release --parallel
```

`R:` 为内存盘时，重启后必须重新配置。非 Windows 环境选择对应生成器，并将构建目录替换为本机 `R:` 等价位置。
