# MX251 v0.0180 使用说明

## 交付内容

| 文件 | 用途 |
| --- | --- |
| MX251-v0.0180-windows-x64.zip | 独立 CMake + Ninja + MX251 完整编译包 |
| studiox.mx251-0.0.180-win-x64.mcutoolchain | MCU_StdioX 编译器组件 |
| studiox.cmake-4.4.0-win-x64.mcutoolchain | IDE 所需 CMake/Ninja 组件；已安装相同组件时可复用 |
| stc.ai8051u-mx251-0.2.180.mcupack | AI8051U 寄存器、库函数工程模板 |
| stc.stc32g-mx251-0.2.180.mcupack | STC32G 寄存器工程模板 |
| MX251-v0.0180-source.zip | 对应发行源码及构建脚本 |
| SHA256SUMS.txt | 交付文件摘要 |

公开版本为 **v0.0180**，IDE 编译器标识为 `mx251-v0.0180`、组件版本为 `0.0.180`。两个包使用完全相同的编译器。器件模板的 `0.2.180` 是独立模板版本。

## 独立编译包

解压到可写目录，从包根目录运行：

```bat
Build.cmd -ProjectPath ".\examples\edata"
Build.cmd -ProjectPath ".\examples\math-casts-ai"
Build.cmd -ProjectPath ".\examples\math-casts-stc"
```

包内有 CMake、Ninja 和 PowerShell，无需设置全局 PATH。工程输出在 `.build/mx251/latest/`；`receipt.json` 记录本次编译器、输入、选项和固件身份。

新建工程可复制示例。普通 C 工程使用：

```cmake
cmake_minimum_required(VERSION 3.25)
project(MyFirmware NONE)
include("${MX251_PROJECT_MODULE}")
add_library(app INTERFACE)
target_sources(app INTERFACE main.c)
# target_include_directories(app INTERFACE include)
# target_compile_definitions(app INTERFACE APP_FEATURE=1)
mx251_firmware(app)
```

```json
{
  "schema": 2,
  "target": "AI8051U-34K64-32",
  "model": "small",
  "optimization": "speed",
  "backend": "ordinary-wide",
  "explicitAbi": false,
  "standard": "sdcc11",
  "runtime": "standard",
  "defines": []
}
```

更换 STC32G 时选择 `STC32G12K128`，使用对应寄存器头文件。编译器没有绑定官方 SDK 的函数名或工程编号。

默认开启函数裁剪、共用机器指令优化、函数共享、按调用关系选择寄存器保存、叶函数 ABI 择优及近跳转。普通 small/standard 工程自动使用原生整数辅助库；显式 ABI、native-math 和 large 工程保留其运行库选择。可用 `-NativeIntegerRuntime OFF` 关闭自动覆盖。

Keil bit/sbit 的 `~` 语义与普通 ISO 布尔取反不同，移植对应代码时使用 `-C251Compatibility`，此选项不自动开启。

## MCU_StdioX

1. 在开发环境/工具组件管理中导入 `studiox.mx251-0.0.180-win-x64.mcutoolchain`；没有 CMake 4.4.0 组件时一并导入随附组件。
2. 在器件包管理中导入两个 `.mcupack`，按目标选择新模板创建工程。
3. AI8051U 提供寄存器与库函数模板，STC32G 提供寄存器模板，使用 IDE 的编译按钮。
4. 旧工程锁定原工具链版本，不会因导入新组件自动切换。通过 IDE 的器件包/工程升级副本功能选用新版本，保留用户源码与 CMake 源码列表；也可复制新模板后迁入源码。

IDE 使用 `sdk/build/Release.cmake`，采用与独立入口相同的优化默认值。工程可用 CMake 变量覆盖；Keil bit/sbit 方言为 `MX251_C251_BIT_COMPLEMENT=ON`。真实 IDE 导入及模板编译结果见验收记录。

## 能力与限制

- 随包两套目标为 MCS-251 / 32 位指令模式。新增同架构器件通过启动、内存、中断及外设描述接入；不能仅修改器件名即声称支持。
- `__edata` 支持全局/静态标量、数组、结构体和指针访问。默认自动分配窗口为 `[0x0100,0x0400)`；指针为三字节平坦 ABI，不是 Keil near 指针 ABI。
- 原生数学运行库按 small + stack-auto 及所选 ABI 配套，不能混用不同 ABI 的对象。可用组合以 `core/build/qualified/TargetContracts.json` 为准。
- 不直接链接闭源 Keil C251 OMF `.LIB`。含此依赖的官方工程需要兼容源码或后续导入能力，未计入本次 203 项构建。汇编专用例程也不计入这组 C 工程验收。
- 部分函数仍使用旧后端或整函数择优，这是支持路径的一部分；独立压力集中记录实际选择，未声称任意用户程序均已验证。
- 未完成所有实际工程相对 Keil 的运行时间 110% 验收；编译成功、固件体积过线与实板运行分别记录。
- 单工具进程限时由 120 秒调整至 600 秒，整工程限时统一为 60 分钟，容纳较大源文件及并行构建；没有改变编译器和代码生成选项。

## 验收与复现

编译器实现提交 `bca1db33504ecba8581473e98b7c4f0c7ceb840d`，SHA-256：

`59ba965abb843fcf4d062edec38e2be77a4c3dec9d36421c8de5c627634bec40`

203 项冻结工程的全量体积统计继续运行，25 项内容一致的同编译器结果直接复用；Keil/STC-IDE 参考同样复用冻结结果。全量统计不作为本次工程版本封包的前置条件，随包 `comparison.json` 是明确标记的部分结果，不能当作全量过线成绩。后续完整结果单独保存到交付目录的 `reports`。本版已经通过的独立示例、IDE 模板和实板测试见 `qualification.json`。Keil/STC 工具及厂商完整 SDK 不包含在编译器包中。

独立用户压力集覆盖结构体、状态机/解析、环形缓冲区、位操作、回调、巨型函数和等价的混乱写法；另有寄存器/调用压力实板证据。构建检查与实板测量分别列示，不把宿主参考计算计作 MCU 执行。

源码构建见 `BUILD.md`，封装配方位于 `tools/maintenance/release_v0180.py`。历史研究保留在 Git，日常编译只需本发行包。
