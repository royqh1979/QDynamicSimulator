# 性能 Profile 索引

所有 profile 使用 Release 可执行文件直接运行，不在调试器中计时。构建 `all-profile-targets`，在源码根目录执行：

```bash
cmake --build <RELEASE_BUILD_DIR> --target all-profile-targets --parallel
<RELEASE_BUILD_DIR>/tests/profile/profile-core.exe 10000
<RELEASE_BUILD_DIR>/tests/profile/profile-editor.exe 1000
<RELEASE_BUILD_DIR>/tests/profile/profile-icons.exe
```

输出为 CSV：`operation,count,total_ms,per_operation_us`。核心 profile 覆盖创建、连接、ID/nameKey 查找、移动和删除；编辑器 profile 覆盖命令/Scene 创建、连接、移动、撤销/重做、空白 Flow 原子创建循环、Comment 正文编辑及 XMILE 保存/加载。

结果记录：

- [核心与编辑器架构基线](sfd-item-architecture.md)。
- [SVG 图标验证与性能](icons.md)。
