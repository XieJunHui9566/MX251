# MX251 当前开发基线

2026-11-14 起使用 **mx251-core-20261114**，Git 标签 `baseline-20261114`。
核心位于 `E:/STVAPP/MX251-baseline/core`；可携带包为 `packages/MX251-core-20261114-windows-x64.zip`。
本轮封装复用已验证的编译器、汇编器和运行库，包含 ABI4 整函数后端择优及此前累计成果。

## 使用

```powershell
& 'E:/STVAPP/MX251-baseline/host/powershell/pwsh.exe' -NoProfile -File 'E:/STVAPP/MX251-baseline/Build.ps1' -ProjectPath 'E:/path/to/project'
```

源码工作树的 `Build-Baseline.ps1` 读取根目录 `baseline.json`，调用同一入口。
压缩包可直接解压执行 `Build.cmd -ProjectPath <工程>`，自带 CMake、Ninja 和 PowerShell。

入口统一启用函数共享、机器指令优化、被调函数保存、自动叶调用和近跳转。
`NativeIntegerRuntime=AUTO` 仅为 small + ordinary-wide + standard 普通 ABI 工程添加原生整数库；native-math、MDU、显式 ABI 与 large 配置按工程自身运行库选择。
可用 `-NativeIntegerRuntime OFF` 保持普通 standard 库；Keil 位取反语义用 `-C251Compatibility`。

## 身份与验证

编译器 SHA-256：`54a2c9c3795f8fd3ddc617b256b5e98f0696558e6f2fe718ece3027cf795b71e`。
编译器实现提交 `e5326b6`，封装实现提交 `efdf351`；公开发行版本仍为 `0.0170-test`。
源码内容清单重新生成，包内包含完整编译器/运行库源码与许可；两套 Profile 共 176 个工具/运行库文件的指纹匹配。

上一轮七个工程中 2 项缩小、5 项 HEX 不变，512 个定向实板用例通过，见 [ABI4 结果](ABI4-BACKEND-CHOICE.md)。
本轮新入口构建了 AI8051U EDATA（276 B）、AI8051U 原生浮点（8196 B）及 STC32G 原生数学（1226 B）三个工程。
浮点 HEX 与上一轮已测固件逐字节相同，因此复用实板证据，没有重刷或重跑全矩阵。

旧 `baseline-20261113` 的 75/75 体积 ≤Keil 110% 是那一版、那一组输入的冻结成绩，保存在 `reference/baseline-20261113` 和 `evidence/bundled-final-nine`。
当前 `comparison.json` 记录本次编译器实际重编的七项，不能把历史 75 项与后续扩展例程直接拼成新版的统一验收成绩；完整工程相对 Keil 的运行时间仍待验收。

## 目录

- `core/`：唯一活动核心；`host/`：宿主工具；`Build.ps1` / `Build.cmd`：统一入口。
- `packages/`：本次可携带 ZIP 及 SHA-256；`examples/`：随包示例。
- `evidence/abi4-backend-choice/`：最新七工程、编译检查、512 项实板证据。
- `evidence/core-packaging-20261114/`：本次入口检查和封装/清理记录。
- `reference/sdk-expanded/`：扩展例程的原始输入、清单、Keil/STC 适配器；后续矩阵用此处作为 `--coverage`。
- `reference/official75/`、`reference/p108/`：既有同源参考；`development/compiler/`：当前源码编译工作目录。
- `archive/development-records-20261114.zip`：本轮清理前的 79612 份历史记录；JSON 索引列出成员路径、长度和 SHA-256。

旧绝对路径通过 `archive/locations.json` 和归档索引查询；历史记录本身保持原内容。
旧核心二进制、重复开发包和中间对象文件已清除。源码 Git 历史、当前工作树、主仓库未提交改动、官方输入和原始 STC-Link1D 抓包保留。
详见 [封装与清理记录](CORE-PACKAGE-20261114.md)。
