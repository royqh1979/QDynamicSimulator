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

## 测试输出与 GUI 诊断

直接运行 Qt Test 时设置 `QT_FORCE_STDERR_LOGGING=1`，确保 qDebug/qWarning 输出到 stderr；需要定位鼠标或绘制问题时，根据实际插件可用性选择 `QT_QPA_PLATFORM`，并设置 `QT_QPA_FONTDIR=<FONT_DIR>`。动态 Qt 可使用 offscreen；静态 Qt 若只提供 windows 插件则不得强制 offscreen。Windows 本机通常使用 `C:/Windows/Fonts`。

```bash
export QT_FORCE_STDERR_LOGGING=1
export QT_QPA_PLATFORM=offscreen
export QT_QPA_FONTDIR=<FONT_DIR>
<BUILD_DIR>/tests/app/test-editor.exe <TEST_CASE> -o <RESULT_FILE>,txt
```

仅写结果文件不能替代诊断日志；调试 qDebug/qWarning 时保留 stderr 重定向。
