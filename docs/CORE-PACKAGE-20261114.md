# 新核心封装与旧文件清理 · 2026-11-14

已将截至 `e5326b6` 的编译器成果封装为 `mx251-core-20261114`，当前活动核心在 `E:/STVAPP/MX251-baseline/core`。封装实现提交 `efdf351`，本轮完成后以 `baseline-20261114` 标记；公开版本号仍为 0.0170-test。

## 可用入口

- 本机：`E:/STVAPP/MX251-baseline/Build.cmd -ProjectPath <工程>`。
- 可携带包：`E:/STVAPP/MX251-baseline/packages/MX251-core-20261114-windows-x64.zip`，182.90 MiB。
- 包 SHA-256：`36a96bb395808bbb0e6338deda8409151aff8ba59510129a385cdfb9a16c73e2`。
- 编译器 SHA-256：`54a2c9c3795f8fd3ddc617b256b5e98f0696558e6f2fe718ece3027cf795b71e`。

包内有完整核心、两套 Target Profile、运行库及源码、宿主工具、示例和许可证。当前核心所有二进制复用已验证版本；更新了完整编译器源码清单。`Build-Core.ps1` 是源码中的封装入口模板，包中名为 `Build.ps1`。原生整数覆盖库根据工程配置自动选择，也可显式 ON/OFF；原生数学与 MDU 项目不会再被旧入口强加 standard 整数库开关。

## 验证结果

| 入口工程 | 目标 | 运行库 | CODE |
|---|---|---|---:|
| EDATA 跨文件示例 | AI8051U | standard + 原生整数覆盖 | 276 B |
| 混合浮点/整数格式 | AI8051U | native-math | 8196 B |
| 数学转换示例 | STC32G12K128 | native-math-wide | 1226 B |

三项均通过新包的统一入口构建。浮点 HEX 与上一轮已经上板验证的固件 SHA-256 完全相同。核心迁入活动目录后，身份/入口/配置/结果等九项内容绑定、十四份当前工程产物引用，以及 436 份保留的官方输入文件均匹配。

这轮没有重编编译器、重刷硬件或重跑全矩阵。最近的 512 项实板证据及七项工程结果沿用同一编译器。历史 75 项成绩归属旧基线，未冒充新版全量结果。

## 清理结果

已移除 12 个旧开发目录及被替换的旧核心，另清除这次封装的重复临时目录。扣除新包、必要输入和研究归档后，按文件长度统计**净释放约 6.86 GiB**。

- `development/` 只保留当前编译器构建目录和配置日志。
- 79612 份历史记录保存为 `archive/development-records-20261114.zip`，索引逐项记录路径、长度和 SHA-256，ZIP 完整性检查通过。
- 最新工程/实板结果集中到 `evidence/abi4-backend-choice/`。
- 官方扩展例程原始输入和适配器集中到 `reference/sdk-expanded/`。
- 旧基线身份与对比表位于 `reference/baseline-20261113/`；旧编译器二进制包已删除，历史研究记录不含这些可替代包和对象缓存。
- Git 历史、活动源码工作树、主仓库三项已有未提交改动、原始 STC-Link1D 抓包及 Keil/STC 对照保留。

额外删除源码 `__pycache__` 的命令被自动审批策略拦截，工具只返回“blocked by policy”，没有具体原因；未重试该删除。这些小缓存和本轮本地维护脚本保留。主要旧开发目录清理已完成。

结构化明细见 [CORE-PACKAGE-20261114.json](CORE-PACKAGE-20261114.json)，使用约定见 [BASELINE.md](BASELINE.md)。
