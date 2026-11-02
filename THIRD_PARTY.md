# 上游、版权与许可证

这是包含多个来源的软件套件，各组件及各文件保留自己的版权和许可。根目录 LICENSE 是 GPL v2 的正文，编译器主体与 MX251 编译器扩展按源文件声明使用 GPL-2.0-or-later；它不会把其他组件的许可证改成 GPL v2。

| 组件 | 来源/版本 | 许可与源码 |
| --- | --- | --- |
| SDCC / MCS-251 分支 | SDCC 4.6.0 系列；gevico/sdcc-c251 `b09075b6a93e6afe10645181e3aeff041ea37f87`；该快照注明 zevorn/sdcc-c251 分支 | 编译器主体 GPL-2.0-or-later；当前修改源码在 `core/sources/compiler`；完整原始快照作为 Release 源码附件 |
| SDAS/SDLD / ASxxxx | 随上述快照继承 | 部分源码 GPL-3.0-or-later，见文件头及 `core/licenses/COPYING-GPL-3.0` |
| sdcpp / cc1 | GCC 12.1.0 派生预处理器 | GPL-3.0-or-later 及文件级声明；原始快照 `support/cpp` |
| sdar 等二进制工具 | GNU Binutils 2.46.0 派生 | GPL-3.0-or-later 及组件许可；原始快照 `support/sdbinutils` |
| SDCC / MX251 运行库 | 标准库、原生整数与原生浮点扩展 | 多数为 GPL 加 SDCC 链接例外；以源码文件为准。例外不改变库源码的许可，也不自动适用于所有第三方代码 |
| CMake | 4.4.0 / Kitware | BSD 类许可与内含第三方声明，随 `host/cmake` 保留 |
| Ninja | 1.10.2 / ninja-build | Apache-2.0，`core/licenses/Ninja-LICENSE` |
| PowerShell | 7.6.5 / Microsoft | MIT 与第三方声明，见 `host/powershell/LICENSE.txt` 及 ThirdPartyNotices |
| gettext-runtime / libiconv | 0.23.1 / 1.18，MSYS2 UCRT64 包 | LGPL/GPL 的组件级许可，见 `core/toolchain/licenses/archiver-deps` |
| zlib / zstd | 1.3.1 / 1.5.7，MSYS2 UCRT64 包 | zlib / BSD 等文件级许可，随工具链保留 |

## 上游源码附件

与 Windows ZIP 一起发布：

- `sdcc-c251-b09075b-source.zip`：继承组件的完整原始源码，SHA-256 为 `467fe3c8d1ecabc78422f71cde82a996228e129dd12aab4d39c467b8bf19d9ae`。
- `third-party-sources.zip`：归档工具依赖所对应的上游源码包，以及来源、版本、哈希和 MSYS2 包来源记录。
- GitHub 标签 `v0.0170-test` 的源码归档：MX251 当前编译器、运行库和构建脚本。

继承基线下载来源：

- [原始源码提交](https://github.com/gevico/sdcc-c251/tree/b09075b6a93e6afe10645181e3aeff041ea37f87)
- [原始 Windows 工具链发布](https://github.com/gevico/sdcc-c251/releases/tag/v4.6.0-mcs251-20260804)
- [SDCC 项目](https://sdcc.sourceforge.net/)
- [Ninja v1.10.2](https://github.com/ninja-build/ninja/tree/v1.10.2)
- [PowerShell](https://github.com/PowerShell/PowerShell)
- [CMake](https://cmake.org/)

本仓库不包含 Keil 编译器或其运行库，不将对比测试工具的实现纳入 MX251。
