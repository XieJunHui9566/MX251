# 构建与使用

## 使用预编译工具链

Release 的 Windows x64 ZIP 是完整可运行包，包含源码及宿主工具。优先使用根目录 `Build.cmd -ProjectPath <工程绝对路径>`。PowerShell 入口 `Build.ps1` 使用包内工具，不修改系统 PATH。

最小工程采用以下结构：

```cmake
cmake_minimum_required(VERSION 3.25)
project(MyFirmware NONE)
include("${MX251_PROJECT_MODULE}")
add_library(app INTERFACE)
target_sources(app INTERFACE main.c)
# target_include_directories(app INTERFACE include)
# target_compile_definitions(app INTERFACE MY_FEATURE=1)
mx251_firmware(app)
```

`mx251.json` 示例：

```json
{
  "schema": 2,
  "target": "STC32G12K128",
  "model": "small",
  "optimization": "speed",
  "backend": "explicit-wide",
  "explicitAbi": true,
  "standard": "sdcc11",
  "runtime": "native-math-wide",
  "defines": []
}
```

`target_sources`、`target_include_directories`、`target_compile_definitions` 是主要 CMake 接口。此适配层会拒绝不支持的生成器表达式、任意编译/链接选项及链接库属性，避免默默丢弃用户选项。

在完整工具链根目录的 PowerShell 中，可以直接调用 CMake/Ninja：

```powershell
$sdk = (Get-Location).Path
$project = Join-Path $sdk 'examples/math-casts-ai'
$build = Join-Path $project '.build/cmake-direct'
& "$sdk/host/cmake/bin/cmake.exe" -S $project -B $build -G Ninja `
  "-DCMAKE_MAKE_PROGRAM=$sdk/host/ninja/ninja.exe" `
  "-DMX251_PROJECT_MODULE=$sdk/core/build/qualified/Project.cmake" `
  "-DMX251_BUILD_SCRIPT=$sdk/core/build/qualified/Build.ps1" `
  "-DMX251_PWSH=$sdk/host/powershell/pwsh.exe" `
  "-DMX251_TOOLCHAIN=$sdk/core/toolchain" `
  "-DMX251_PROFILE=$sdk/core/profiles/AI8051U-34K64-32" `
  -DMX251_FUNCTION_GC=ON -DMX251_WIDE_VALUE_LIMIT=512 -DMX251_WIDE_NODE_LIMIT=2048
& "$sdk/host/cmake/bin/cmake.exe" --build $build --target firmware
```

普通应用使用 `Build.cmd` 会另外检查编译器内容和最终构建凭据。不要在同一工程上同时启动多个构建或边构建边下载其产物。

## 运行库与限制

两个配置均提供 `standard`、`native-int` 和显式宽 ABI 的 `native-math-wide` 等组合；STC32G 还提供普通 ABI 的 `native-math`。完整白名单位于 `TargetContracts.json`，未列出的组合会拒绝。原生数学库当前要求 **small + stack-auto**；不要直接用于 medium/large 或混合不兼容 ABI 的对象。

默认包入口开启函数可达性裁剪。外部汇编入口可用 `-FunctionGcKeep` 显式保留；其他可选优化开关见 `Build.ps1` 参数。未知中断、地址空间或 SDK 适配不能仅靠改变芯片名称解决。

## 从源码构建编译器

`core/build/BuildSource.py` 是当前 Windows 编译器/汇编器/链接器源码构建入口，生成 CMake 工程并以 Ninja 编译，输出 `sdcc.exe`、`sdas251.exe`、`sdld.exe`。它不要求 StudioX。所需宿主依赖由开发者显式提供，不包含在日常固件编译的依赖内：

- Python 3.11+、MinGW-w64 GCC/G++（C17/C++11）和 GNU awk。
- MSYS2 风格的 Bison、Flex、M4 及其依赖：`<host-deps>/usr/bin`、`usr/share/bison`。
- Boost 头文件放在 `<host-deps>/mingw64/include`。已用构建依赖版本：Bison 3.8.2、Flex 2.6.4、M4 1.4.19、Boost 1.87.0。
- CMake 与 Ninja；可使用 Release 内的版本。输出必须是源码目录之外一个尚不存在的目录。

在源码仓库根目录执行，路径按自己的环境替换：

```powershell
python core/build/BuildSource.py `
  --source core/sources/compiler --output ../mx251-host-build `
  --host-deps C:/deps/mx251 --gcc-bin C:/deps/mingw64/bin `
  --gawk C:/deps/msys/usr/bin/gawk.exe `
  --cmake C:/tools/MX251/host/cmake/bin/cmake.exe `
  --ninja C:/tools/MX251/host/ninja/ninja.exe
```

宿主依赖版本不同可能产生不同二进制指纹；该脚本保留输入清单和原始诊断。仅替换二进制会导致内容锁检查失败，开发构建需要重新生成对应清单，不能把旧指纹当作新构建资格。

预处理器、归档工具等继承组件的源码与构建系统在同一 Release 的 `sdcc-c251-b09075b-source.zip`，其原始文档位于压缩包中的 README / doc。该快照是上游来源，不应替换本仓库已修改的编译器源码。DLL 依赖源码另见 `third-party-sources.zip`。

## 重建原生浮点库

以下命令在完整工具链根目录执行；`--output` 必须尚不存在：

```powershell
python core/sources/native-float-builder/BuildNativeFloat.py `
  --source core/sources/native-float `
  --compiler core/toolchain/bin/sdcc.exe --toolchain core/toolchain `
  --cmake host/cmake/bin/cmake.exe --ninja host/ninja/ninja.exe `
  --output ../mx251-native-float-build
```

它重建 66 个成员并核对成员顺序/归档内容；新归档需要重新做适用的 ABI/工程验证。发布包沿用已集成的归档，未把此重建命令当成发布包的逐字节复现承诺。
