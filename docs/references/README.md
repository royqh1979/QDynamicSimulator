# 参考文献本地存档

本目录保存设计所引用的公开文档，供离线核对。文档权利归原站点及作者所有；本地存档不改变其许可证，不作为本项目代码或图标素材使用。

| 本地文件 | 原始来源 | 用途 |
| --- | --- | --- |
| `xmile-1.1.html` | https://xmile.systemdynamics.org/1.1/xmile-v1.1.html | XMILE 1.1 目标规范，原始 HTML |
| `xmile-1.0-os.html` | https://docs.oasis-open.org/xmile/xmile/v1.0/os/xmile-v1.0-os.html | XMILE 1.0 正式规范，仅用于旧文件兼容核对 |
| `vensim-sketch-tools.md` | https://www.vensim.com/documentation/ref_sketch_tools.html | Level、Arrow、Rate、Sketch Comment 工具 |
| `vensim-rate-class.md` | https://www.vensim.com/documentation/22930.html | Rate、Valve、管线与源/汇云团 |
| `vensim-auxiliaries-arrows.md` | https://www.vensim.com/documentation/20335.html | Arrow 与 Rate 名称/阀门的结构关系 |
| `vensim-variable-types.md` | https://www.vensim.com/documentation/ref_variable_types.html | Level、Auxiliary、Constant、Data 等变量类型 |
| `vensim-sketch-comment.md` | https://www.vensim.com/documentation/22940.html | Comment、Arrow Junction 与模型结构的区别 |
| `vensim-glossary.md` | https://www.vensim.com/documentation/glossary.html | Arrow、Exogenous 等术语正文摘录 |

XMILE 文件为原始 HTML。Vensim 站点拒绝本机直接下载，因此 Vensim 文件保存为成功读取的页面正文 Markdown 存档，而非原始 HTML；`vensim-glossary.md` 明确为相关术语摘录。远程图片和样式未存档，概念结论以正文为准。

设计引用入口为 `docs/design/vensim-xmile-mapping.md`。

## 测试模型子模块

- 来源：https://github.com/SDXorg/test-models
- 路径：`3rdparty/test-models`，通过 `.gitmodules` 和 gitlink 固定上游提交，不复制到本项目普通目录。
- 用途：`tests/` 提供按单一功能划分的单元式模型，`samples/` 提供集成、互操作、性能验证和演示模型；其中的 XMILE、Vensim、Stella 文件及规范输出作为后续测试输入，不作为本项目图标或界面素材。
- 许可证：MIT，见子模块中的 `LICENSE`；使用时保留上游版权和许可声明。
- 初始化或更新到项目固定提交：`git submodule update --init 3rdparty/test-models`。
- 子模块内容不作为普通项目文件逐个提交；需要变更上游版本时单独审核并提交 gitlink。
