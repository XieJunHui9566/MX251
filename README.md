# MX251 0.0170（测试）

**面向 MCS-251 MCU 的开源 C 编译工具链，集成通用代码生成优化、原生整数/浮点运行库与独立 CMake + Ninja 工程入口。**

MX251 builds on SDCC and the existing `sdcc-c251` MCS-251 port. This experimental Windows x64 distribution combines the optimized compiler and native math runtime, and works without MCU StudioX.

[下载 0.0170 测试版](https://github.com/XieJunHui9566/MX251/releases/tag/v0.0170-test) · [构建说明](docs/BUILD.md) · [技术架构](docs/ARCHITECTURE.md) · [验证范围](docs/VALIDATION.md) · [上游与许可](THIRD_PARTY.md)

## 技术栈继承与创新

| 层次 | 继承的基础 | MX251 的扩展与整合 |
| --- | --- | --- |
| C 编译器 | SDCC 4.6.0 系列的前端、公共 IR、优化框架与 MCS-51 基础；`sdcc-c251` 已有的 MCS-251 移植 | 251 原生宽值后端、寄存器/栈分配、调用与返回值处理、地址空间及 ABI 适配 |
| 代码体积 | 上游代码生成和窥孔优化 | 跨普通/宽值生成路径的最终控制流清理、公共尾部合并、调用闭包驱动的未用函数剔除等共通优化 |
| 汇编与链接 | ASxxxx 派生的 SDAS/SDLD、SDCC 的 GNU Binutils 工具 | 与 251 代码生成、重定位、启动及运行库契约配套的构建链 |
| 数学运行库 | SDCC 数学库算法与库接口 | 原生整数和浮点内核、整数/浮点转换、ABI 桥接；本版合入累计 66 个成员的原生浮点库 |
| 工程构建 | CMake、Ninja、PowerShell | 可独立调用的固件构建模块、Target Profile 能力声明、工具/源码内容指纹及构建凭据 |

优化依据 IR、ABI、地址空间、目标能力和成本条件选择，面向满足同一条件的工程复用。某些优化仍需显式开启；不能从一个例程的结果推断所有 SDK 均已通过。继承来源及代码入口见[架构说明](docs/ARCHITECTURE.md)。

## 适用 MCU

本版交付以下两个 **MCS-251 / 32 位指令模式**目标配置：

| MCU / 配置名 | 本版提供内容 | 状态 |
| --- | --- | --- |
| AI8051U，`AI8051U-34K64-32` | 启动、内存/ABI 契约、标准及原生运行库、跨文件数学示例 | 已有离线集成证据；本次合并镜像未进行实板计时 |
| STC32G12K128，`STC32G12K128` | 启动、中断向量契约、标准及原生运行库、数学示例 | 已有完整数学工程离线集成证据；本次合并镜像未进行实板计时 |

其他 MCS-251 器件可研究新增 Target Profile；必须核对启动、内存映射、中断、ABI 和外设能力。本版不宣称覆盖所有 STC 型号，也不把上游 MCS-51 支持直接等同于任意 8051 器件的 MX251 支持。配置中的内存窗口是当前构建契约，不能当成芯片全部物理容量。

## 快速开始（Windows x64）

1. 从 Release 下载并解压 `MX251-0.0170-test-windows-x64.zip`。
2. 在解压目录打开终端，执行：

```bat
Build.cmd -ProjectPath ".\examples\math-casts-ai"
```

STC32G 配置示例：

```bat
Build.cmd -ProjectPath ".\examples\math-casts-stc"
```

包内包含 MX251、CMake 4.4.0、Ninja 1.10.2 和 PowerShell 7.6.5，无需安装 StudioX 或修改系统 PATH。固件、链接信息与 `receipt.json` 位于工程的 `.build\mx251\latest`，CMake 构建目录为 `.build\cmake-mx251-core`。示例只计算并保存结果，不配置 LED、串口或 OLED。

工程源码、头文件目录和宏由 `CMakeLists.txt` 管理；`mx251.json` 保存目标、内存模型、ABI 和运行库选择。本版通过 `Project.cmake` 的 `mx251_firmware()` 生成 Ninja 固件目标，尚未提供通用的 CMake C 语言驱动识别或任意 `CMAKE_TOOLCHAIN_FILE` 接口。直接使用 CMake 的命令见[构建说明](docs/BUILD.md)。

## 源码与版本

仓库包含编译器/汇编器/链接器源码、原生数学库源码、构建脚本、目标契约与示例。预编译 EXE、运行库归档和宿主工具放在 Release 附件中。克隆源码仓库不会自动下载这些二进制文件。

`0.0170（测试）` 是本次合并发布的 MX251 版本；预编译编译器和数学库复用已合并的内容，没有为了改版本重复编译。底层 `sdcc.exe --version` 仍显示上游 `SDCC 4.6.0 #0`，MX251 的身份以 `VERSION`、`core/core.json` 和内容 SHA-256 为准。

本版可用于开始工程编译与试用。它仍是测试版：尚未完成全部工程的性能验收，合并镜像的实板正确性和耗时未测，不能宣称全面达到或替代 Keil C251。具体已测和未测范围见[验证说明](docs/VALIDATION.md)。

## 许可

编译器主体及 MX251 扩展遵循 GPL-2.0-or-later；套件包含 GPL-3.0-or-later、带链接例外的 SDCC 运行库和其他独立许可组件，各文件原有许可优先。详见 [LICENSE](LICENSE)、[THIRD_PARTY.md](THIRD_PARTY.md) 和随包许可证。
