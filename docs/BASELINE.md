# MX251 当前开发基线

2026-11-13，用户指定 `bundled-final-nine/core-07` 为新的开发基线；Git 标签 **`baseline-20261113`**。本机运行环境已集中到 **`E:/STVAPP/MX251-baseline`**，编译器与已验收构建脚本内容保持不变，公开发行版本仍为 `0.0170-test`。

## 使用

```powershell
& 'E:/STVAPP/MX251-baseline/host/powershell/pwsh.exe' -NoProfile -File 'E:/STVAPP/MX251-baseline/Build.ps1' -ProjectPath 'E:/path/to/project'
```

现用开发工作树的根目录 `Build-Baseline.ps1` 读取 `baseline.json`，转交同一入口。当前优化配置用于 `small + ordinary-wide + runtime=standard` 工程，统一启用通用函数共享、机器指令层、被调函数保存、原生整数运行库、自动叶调用与近跳转；不依赖 SDK。需要 Keil 位取反兼容语义时追加 `-C251Compatibility`。显式宽 ABI / native-math-wide 工程继续调用核心 `build/Build-CMake.ps1` 的相应参数。

- `core/`：当前唯一完整开发核心，编译器 SHA-256 `4e6a7d78e3fc4d9988bf43c56777d4cf66b25d453de60eb4f4f65f987fdd55fc`。
- `host/`：CMake、Ninja，以及本轮验收所用 PowerShell 的原样副本。
- `baseline.json`：核心、入口、配置、对照及清理记录的内容绑定。
- `sdk-baseline.json`：75 项同一验收配置，路径迁入新目录；新输出默认 `work/next-sdk-matrix`。
- `comparison.json`：当前 75 项基线，300 条结果引用已更新为迁入后的路径。
- `evidence/bundled-final-nine/`：当前完整矩阵、原始合格记录、机制检查和实板结果。
- `reference/`：Keil/STC 同源参考、p108 双方配对输入、上一统一版矩阵及报告。
- `development/compiler/`：源码构建所用生成文件与 CMake 配方；新构建目录已配置，旧对象缓存已清除。
- `archive/research-records.zip`：26166 份历史研究记录；不再保留旧编译器包、重复 SDK 和旧对象文件。

原始验收记录保持字节不变；其中旧绝对路径按 `archive/locations.json` 的映射或归档成员名查找。源码仍在现用 Git 工作树，源码实现提交 `172cb90e22aa9b99c5097ef6141f42251b7b0c69`，验收源码快照 `b6ff0f8d33e8f9707449ea86a256af449ef97da9`。已有公开包的 `Build.cmd` / `Build.ps1` 及 `core/core.json` 仍描述公开发行包；当前本机开发使用上述基线入口。

## 基线成绩

75/75 编译成功，75/75 体积 ≤Keil 110%（也全部 ≤120%）；MX251 / Keil 合计 189090 / 192049 B，98.46%。六组定向实板用例 768/768 通过。完整工程同条件运行时间仍待验收。后续收益统一与本基线比较，使用新的输出目录，不覆盖冻结产物。

## 清理与验证

截图中的 `MX251 release check`、`MX251-edata-build`、`MX251-edata-dev`、`MX251-handoff-20261109` 和四个 `MX251-mainline-*` 旧目录均已移除。现用依赖已迁入基线；重复工具链、旧编译器、历史对象缓存、试编 SDK 副本、旧移交 bundle 及先前候选包 ZIP 均已清除。必要的研究记录压缩为 49.73 MiB。

本次清理累计净释放约 **34.11 GiB**（文件长度口径，目录总量统计四舍五入）。仓库移除 6 个已无引用的 `profile.lock.before-*` / `profile.lock.parent.json` 备份。主仓库三项既有未提交改动和 `MX251-captures` 保持原样；当前源码工作树保留。

迁移后核对 6 个基线内容绑定、300 条工程结果引用及所需工具/输入路径；新入口成功编译仓库 EDATA 示例，仍为 317 B，编译器指纹不变。源码 CMake 配置成功。未重跑 75 项矩阵或下载硬件。清理明细和验证结果见 [baseline-cleanup.json](baseline-cleanup.json)。
