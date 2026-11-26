# 参考文献本地存档

本目录保存设计所引用的公开文档，供离线核对。文档权利归原站点及作者所有；本地存档不改变其许可证，不作为本项目代码或图标素材使用。

| 本地文件 | 原始来源 | 用途 |
| --- | --- | --- |
| `xmile-1.1.html` | https://xmile.systemdynamics.org/1.1/xmile-v1.1.html | XMILE 1.1 目标规范，原始 HTML |
| `xmile-1.0-os.html` | https://docs.oasis-open.org/xmile/xmile/v1.0/os/xmile-v1.0-os.html | XMILE 1.0 正式规范，仅用于旧文件兼容核对 |

XMILE 文件为原始 HTML。Vensim 官方文档不提交下载副本；用户提供的求值顺序摘录仅保存于本地 `docs/references/vensim/`，该目录由 gitignore 排除，原始 URL 未提供时明确标记为未知，不猜测来源地址。

设计引用入口为 `docs/design/vensim-xmile-mapping.md`。

## 测试模型子模块

- 来源：https://github.com/SDXorg/test-models
- 路径：`3rdparty/test-models`，通过 `.gitmodules` 和 gitlink 固定上游提交，不复制到本项目普通目录。
- 用途：`tests/` 提供按单一功能划分的单元式模型，`samples/` 提供集成、互操作、性能验证和演示模型；其中的 XMILE、Vensim、Stella 文件及规范输出作为后续测试输入，不作为本项目图标或界面素材。
- 许可证：MIT，见子模块中的 `LICENSE`；使用时保留上游版权和许可声明。
- 初始化或更新到项目固定提交：`git submodule update --init 3rdparty/test-models`。
- 子模块内容不作为普通项目文件逐个提交；需要变更上游版本时单独审核并提交 gitlink。
