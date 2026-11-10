# 高频叶函数自动寄存器传参

2026-11-10，父版 `ad1dacd`。将此前独立研究入口的叶函数 ABI4 试选接入常规构建。编译器二进制仍为 `4cdcb0d7d7aaf96272b7c173a5c72a16b7573ed7bd14e0d7fed9097466fcff16`；本轮改动是工具链构建优化，复用编译器已有的内部 ABI4 契约。工程源码和 SDK 均未修改。

## 完整工程收益

| 工程 | 同次基线 / B | 自动选择 / B | 减少 / B | Keil / B | 当前体积比 |
| --- | ---: | ---: | ---: | ---: | ---: |
| p087 USB CDC | 4366 | 4249 | 117 | 3531 | 120.33% |
| p091 USB MSC | 5297 | 5205 | 92 | 4401 | 118.27% |
| p095 双串口 USB | 6216 | 6032 | 184 | 4830 | 124.89% |

MSC 跨过 120% 体积线；CDC 距该线还差 12 B。三项尚未达到 110% 体积线。TFT 对现有 19408 B 完整镜像运行选择器，没有候选，本轮没有重编 TFT。完整 USB 对 Keil 的运行时间仍未取得。

三个工程使用同一选择规则，都选中各自的 `usb_write_reg`，实际调用点为 61、50、90。源码、编译器、profile、原有构建选项在各自基线与候选间一致。最终 HEX、BIN、ASM、REL、RST、MAP 一同发布；调用破坏反馈的最终检查全部通过。[完整证据](automatic-leaf-abi-evidence.json)记录输入指纹和镜像结果。

## 正常使用

`Build-CMake.ps1`、`Project.cmake`、`BuildLatest.ps1`、`Build.ps1` 默认使用 `SelectiveLeafAbi=AUTO`。可通过 `-SelectiveLeafAbi OFF` 或 CMake `-DMX251_SELECTIVE_LEAF_ABI=OFF` 关闭。

选择器要求 standard runtime、全部为 ordinary-wide stack-auto C 单元、无项目归档。只试两个整数参数的叶函数：一个公开定义、普通 ABI2/bank0、无取址和特殊入口、引用均为可匹配类型签名的直接调用。启动、库及 function-GC 根引用排除；运行库扫描同时涵盖工具链和目标 profile。函数最终落到普通后端时，其带制表符的 ABI 元数据同样识别。

至少 16 个实际调用点才启动试编，按调用数乘第二参数位宽排序，每次只试一个。该排名用于限制试编成本，不是节省字节预测。源级调用元数据可能多于分支共享后的机器调用数，因此按调用者与目标签名匹配每个实际调用，不要求两种计数相等。

子构建继承本次所有显式构建参数，独立运行完整编译、反馈、GC、链接；仅更小的最终镜像被采用。ABI4 的唯一最终定义和每个 ECALL 都与 RST/HEX 重定位核对。失败或不收益候选保留基线并记录原因。公开 ABI2 约定没有全量改动，选中的内部函数由编译器正常修饰符号；选择器不含 SDK 或函数名称规则。

每次构建的 `selective-leaf-abi.json` 保存选择和收益，receipt 绑定选择记录、脚本和内部契约。采用候选时，`leaf-abi/baseline` 与 `leaf-abi/candidate` 保留两份完整记录。构建成本为候选工程额外完整编译一次；无候选工程只进行选择扫描。不增加 Python 运行依赖。

## 执行与选择检查

[选择器检查](../tools/research/check-selective-leaf-build.ps1)通过 9 项：完整闭合叶函数、外部根、地址引用、无类型调用、非叶函数、函数外调用、opaque 单元、混合后端单元、重复定义。

此前 `leaf_mix` 的增长样例在当前流水线已获益：启用 MachineIr/CalleePreservation 后 1026→988 B。因此用[发布分支回放](../tools/research/check-selective-leaf-rejection.ps1)输入已保存的真实 1016 B/1090 B 镜像对，确认生产选择流程保留 1016 B 原镜像，且不是因构建或绑定错误而跳过。此项是历史产物回放，不计为本轮编译器收益。

[独立实板夹具](../tools/research/automatic_leaf_abi_fixture.py)跨文件调用叶函数 32 次，两个字节参数写入 volatile XDATA 数组，再计算摘要；主机用独立整数运算核对。另保留原控制组。

| AI8051U-34K64 实板 | 基线 | 自动选择 |
| --- | ---: | ---: |
| 镜像 / B | 1772 | 1581 |
| 测试组 tick 汇总 | 13951.5 | 12928 |
| 控制组 tick 汇总 | 10084 | 10084 |
| 检查项 | 128 通过 | 128 通过 |
| 完整帧 | 653 | 659 |

40 MHz，Timer0 /12T；每个输入取多帧中位数后汇总。体积减少 10.78%，测试组耗时减少 7.34%，控制组不变，无计时溢出。自动定位 STC-Link1D 与 COM13，最后保留新固件。这是本夹具的前后比较，不能替代完整工程与 Keil 的双 110% 验收。

## 保存位置

`E:/STVAPP/MX251-mainline-20261110/leaf-production/`：最终冻结核心 `core-03`，完整工程 `core-03-p087/p091/p095`，实板工程 `board-auto-final`，采集 `capture-before/after`。`selector-tests` 与 `rejection-replay` 保存检查结果。最终验证后只整理源码换行和文档；未更新公开发行包或 IDE 默认工具链。

原 [SELECTIVE-LEAF-ABI.md](SELECTIVE-LEAF-ABI.md) 保留独立研究工具的历史规则与测量，本轮通用构建入口以本文为准。
