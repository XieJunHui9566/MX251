# 技术架构与来源边界

## 继承链

本次使用的原始工具链与源码来源是 [`gevico/sdcc-c251`](https://github.com/gevico/sdcc-c251) 提交 `b09075b6a93e6afe10645181e3aeff041ea37f87`。该快照的 README 将 MCS-251 分支指向 [`zevorn/sdcc-c251`](https://github.com/zevorn/sdcc-c251)。这些项目继续建立在 [SDCC](https://sdcc.sourceforge.net/) 及其组件之上。

已有 MCS-251 移植、前端语言支持、SDAS/SDLD、GNU 预处理器与大部分标准库并非 MX251 从零编写。MX251 保留相应源码版权声明。Release 的 `sdcc-c251-b09075b-source.zip` 提供原始快照，当前改动后的编译器与数学库源码位于本仓库。

## MX251 代码入口

| 路径 | 职责 |
| --- | --- |
| `core/sources/compiler/src/mcs251/wide.c` 及同目录头文件/`.inc` | 宽值 IR 导入、原生指令选择、寄存器重叠、spill、帧及 ABI 处理 |
| `core/sources/compiler/src/mcs251/gen_cfg_cleanup.c.inc` | 生成阶段的共享控制流清理与尾部合并 |
| `core/build/qualified/FunctionGc.ps1` | 按启动、调用、库和符号引用保守计算函数可达性 |
| `core/build/qualified/CallEffects.ps1` 等 | 调用副作用、寄存器保存和反馈信息的构建配套 |
| `core/sources/native-float/device/lib/mcs251/native-float/` | 本次合入的浮点内核与转换实现 |
| `core/sources/native-float-builder/BuildNativeFloat.py` | 66 成员原生浮点库的 CMake/Ninja 构建配方 |
| `core/sources/compiler/device/lib/mcs251/native-int/` | 原生整数运行库源码 |
| `core/build/qualified/TargetContracts.json` | 启动、内存、中断与优化能力契约 |
| `core/build/qualified/Project.cmake` | CMake 工程描述转为固件构建目标 |

## 通用性与回退

优化判定依赖值宽度、IR、调用约定、地址空间、可证明副作用和真实成本。目标相关能力集中在 Profile/契约中；例如 MDU 运行库必须显式选择并有匹配的硬件契约，不能把存在于一块板上的 MDU 当作通用指令。

不支持的宽值结构可以回退到已有生成路径；无法保证正确性的 ABI、运行库或目标组合会明确拒绝。回退并不承诺相同的体积或速度。中断、volatile、内联汇编、函数指针和跨模块调用均有独立约束。

原生数学库高层算法继承 SDCC；MX251 的工作集中在内核、转换、传参/返回、运行库选择与工程整合。本版数学归档由先前编译器构建后按相同内容复用，当前编译器源码并不意味着重建数学库一定产生逐字节相同的归档，见验证记录中的两个编译器指纹。

## 宿主结构

```text
CMakeLists.txt + mx251.json
        ↓ Project.cmake
      Ninja firmware
        ↓ PowerShell 构建驱动
MX251 C → SDAS251 → SDLD → HEX / receipt
        ↑ Target Profile + runtime
```

CMake 负责源码、包含路径和宏；目标/ABI 参数由同一构建服务处理。整个入口无需 StudioX 运行时、工程数据库或开发机安装路径。
