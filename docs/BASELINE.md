# MX251 当前基线：v0.0180

当前工程版本位于 `E:/STVAPP/MX251/dist/v0.0180/MX251-v0.0180-windows-x64`，Git 标签 `v0.0180`。源码根目录的 `Build-Baseline.ps1` 读取 `baseline.json` 并调用这一版本。

独立 ZIP 自带 CMake、Ninja、PowerShell 和 MX251；在解压目录执行 `Build.cmd -ProjectPath <工程目录>`。MCU_StdioX 使用同目录的 `.mcutoolchain` 和配套 `.mcupack`。

实测范围见 [验收说明](QUALIFICATION-v0.0180.md)，使用说明见 [v0.0180](RELEASE-v0.0180.md)。203 项体积统计单独继续，完整工程相对 Keil 的运行时间目标仍待验收。
