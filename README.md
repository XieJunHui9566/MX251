# MX251 v0.0180

面向 MCS-251 的 C 编译工具链。本版将通用后端、寄存器分配、地址生成、调用优化和数学运行库收口为工程使用版本。

提供两个 Windows x64 交付包，共用同一编译器和运行库：

- `MX251-v0.0180-windows-x64.zip`：自带 CMake、Ninja、PowerShell、源码、示例，解压即可编译。
- `studiox.mx251-0.0.180-win-x64.mcutoolchain`：MCU_StdioX 工具链，另附 AI8051U / STC32G 配套器件模板包及 CMake 组件。

## 独立编译

在解压目录运行：

```bat
Build.cmd -ProjectPath ".\examples\edata"
Build.cmd -ProjectPath ".\examples\math-casts-ai"
Build.cmd -ProjectPath ".\examples\math-casts-stc"
```

工程由 `CMakeLists.txt` 管理源码、头文件目录和宏，`mx251.json` 选择目标、模型及运行库。生成文件在工程的 `.build/mx251/latest/`，包括 HEX、BIN、MAP 和 `receipt.json`。移植 Keil 的 bit/sbit 取反写法时追加 `-C251Compatibility`。

使用步骤、IDE 导入、版本映射和限制见 [v0.0180 使用说明](docs/RELEASE-v0.0180.md)。直接 CMake 接口见 [构建说明](docs/BUILD.md)。

## 支持范围

通用优化根据 IR、ABI、地址空间和调用关系选择；器件差异由 Target Profile 描述。随包提供 AI8051U-34K64-32、STC32G12K128 两套配置。支持显式 `__edata`、XDATA/CODE 访问、结构体、回调、状态判断、整数及原生浮点运行库；内存和 ABI 契约见 [EDATA](docs/EDATA.md) 与包内 `TargetContracts.json`。

这是一份工程使用交付，不代表全部工程的体积和运行时间已达到 Keil C251 的 110%。构建、实板测试及剩余差距记录在随包 `docs/qualification.json`；闭源 Keil OMF 库不能直接当作 MX251 对象链接。

## 源码和许可

MX251 继承 SDCC、sdcc-c251 及 ASxxxx 工具，编译器主体和扩展遵循 GPL-2.0-or-later；其他组件和运行库以各自许可及链接例外为准，见 [第三方说明](THIRD_PARTY.md)。包内提供编译器、汇编/链接器及运行库源码，源码构建入口为 `core/build/BuildSource.py`。

底层 `sdcc.exe --version` 保留上游标识；MX251 的发行身份由 `VERSION`、`core/core.json` 和 SHA-256 确定。本版不包含活跃区间拆分研究原型。
