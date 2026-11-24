# SVG 图标性能与验证

## 范围与方法

- 资源：`src/app/resources/icons.qrc`，48 个极简单色 SVG。
- 测试：`test-icons` 逐个创建 QIcon，并实际渲染 24×24 与 48×48 像素，覆盖静态 Qt SVG 插件和资源路径；不做 SVG 源字符串自比对。
- Profile：`profile-icons` 对全部图标执行 100 轮 24×24 渲染，共 4800 次。
- 环境：MinGW GCC 8.1.0，Qt 5.15.18 静态 Release，Windows 11，R: 内存盘。

运行：

```bash
cmake --build <RELEASE_BUILD_DIR> --target test-icons profile-icons --parallel
<RELEASE_BUILD_DIR>/tests/app/test-icons.exe
<RELEASE_BUILD_DIR>/tests/profile/profile-icons.exe
```

## 结果

| 操作 | 数量 | 总耗时 ms | 每次 μs |
| --- | ---: | ---: | ---: |
| SVG 图标加载与渲染 | 4800 | 685.153 | 142.740 |

全部 48 个图标在 Release 资源测试中通过，无空图标或渲染失败。
