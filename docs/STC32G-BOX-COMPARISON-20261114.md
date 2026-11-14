# STC32G 实验箱三方编译对比（2026-11-14）

范围：官方 STC32G12K128 V9.6 包，142 个工程，其中 103 个 C 工程、39 个纯汇编工程。
来源：[官方实验箱页面](https://www.stcai.com/syx)；页面标注版本 20260908；下载包 SHA256 `1618375dd8300cb367e66d53c66a11f254d39007ef1d88df317a725ff22bfac2`。

使用已封装的 `mx251-core-20261114`，本轮不修改编译器核心或例程算法。所有工程和失败记录均计入清单。

## 口径

- MX251：公开 Build.ps1 入口，STC32G12K128、small、默认 EDATA、ordinary-wide、C251Compatibility。普通整数使用 AUTO；浮点工程选择 native-math，显式 MDU32 库工程选择 native-int-mdu32。
- Keil5：本机 C251 V5.60.0.0，XSMALL、ROM(LARGE)、原工程 OPTIMIZE 等级/SPEED、INTR2；EDATA/HDATA 分配范围 0–0xFFF；保留原工程 REMOVEUNUSED 选项。扩展中断使用向量适配器，PARM4 等调用约定原样保留。
- STC-IDE：本轮重新下载官方 worker，目标 STC32G12K128，24 位指针、GB2312、优化开启，分别运行 soft 和 mdu32。
- 体积为最终 HEX 有效数据字节，包含常量和初始化数据；地址空洞另存。110%/120% 只在双方成功的 C 工程中统计。
- 只做编译与链接测试，未测执行时间，也未证明三方固件功能等价。不能据此宣布最终工程验收通过。
- C251 关键字、字符接口及数字向量做既有语法适配；8 个工程缺省引用的 STC32G.H 从同包 COMM 目录补入所有工具输入。纯汇编保留原 A251 语法并实际调用 MX251 汇编器。
- USB OMF 不删除、不替换；MX251 尚无完整自动 ABI 接入。STC-IDE worker 不接收原 A251 单元和 USB OMF 库，对应工程列为不支持。MDU32 算术库由各编译器对应算术运行库替代，原始运算保留。

## 编译结果

| 工具 | C 工程成功 | 纯汇编成功 | 全部状态 |
|---|---:|---:|---|
| mx251 | 89/103 | 0/39 | BUILT: 89, FAILED: 48, UNSUPPORTED_KEIL_OMF: 5 |
| keil | 101/103 | 39/39 | BUILT: 140, FAILED: 2 |
| stc-soft | 79/103 | 0/39 | BUILT: 79, UNSUPPORTED_ASSEMBLY_INPUT: 57, UNSUPPORTED_KEIL_OMF: 5, FAILED: 1 |
| stc-mdu32 | 79/103 | 0/39 | BUILT: 79, UNSUPPORTED_ASSEMBLY_INPUT: 57, UNSUPPORTED_KEIL_OMF: 5, FAILED: 1 |

## 体积对比

| 参照 | 可比 C 工程 | MX ≤110% | MX ≤120% | 中位比值 | MX 总字节 / 参照总字节 |
|---|---:|---:|---:|---:|---:|
| keil | 89 | 9 | 15 | 160.98% | 290604 / 206514 |
| stc-soft | 78 | 60 | 68 | 92.31% | 252402 / 286583 |
| stc-mdu32 | 78 | 60 | 68 | 92.31% | 252402 / 286583 |

三方均成功的同一组 78 个 C 工程，按有效字节相加：

| 工具 | 总字节 | 相对 Keil |
|---|---:|---:|
| mx251 | 252402 | 143.05% |
| keil | 176447 | 100.00% |
| stc-soft | 286583 | 162.42% |
| stc-mdu32 | 286583 | 162.42% |

## 差距与后续顺序

1. 先补齐 STC32G 目标配置。当前仍固定生成 62 个向量；无中断的 g000 有效 HOME 区为 255 B，其中 248 B 来自未用向量桩。完整体积 307 B，而 Keil 为 60 B、STC-IDE 为 35 B。按需求生成向量是已定位的共性收益，尚未在本轮修改或计入收益。
2. STC32G 配置未启用已有 nativeCodeReads 和 sph85-v1 能力；其有序外设位操作范围也较旧。官方 STC32G.H 的 SPH 为 0x85，而当前编译器元数据仍标记 legacy-v1（栈寄存器识别使用 0xBE）。应核对并补齐目标能力，不能把这些开关差距归因于优化算法本身。
3. MX251 有 8 项自身构建失败：g079 报 unit 属性不存在；g102、g105–g110 共 7 项报 Missing EOF or reset vector。优先修复这两条路径。主表统一使用封装核心；输入适配和 Keil pragma 处理修正前的日志另行保留。
4. g083 MDU32 原例程存在 printf 参数宽度不匹配，格式化器记录为 format-argument-width-mismatch，拉入完整 printf；9720 B 中 printf/vprintf/print_format 区段合计 6680 B。此工程需先处理输入格式问题，才能作有效的优化判断。
5. 5 个 USB 二进制库工程仍需完整 OMF/ABI 接入；39 个纯 A251 工程是汇编语法接入工作，不能用 C 代码生成优化解决。
6. 官方输入问题：g072 科学计算器在 Keil 上含未定义标识符/声明错误，直接编译原始 nor_cal.c 也复现 30 个错误，记录在 native-check-g072。g140 的 DOWNLOAD、SVCC_E、LED_POWER 未定义，三方均报错。
7. STC-IDE 的 soft / mdu32 两次配置在本包成功的 79 项上生成相同 HEX；请求参数和官方 worker 版本均已记录。

## C 工程失败与不支持项

| 工具 | 编号 | 状态 | 日志/记录 |
|---|---|---|---|
| mx251 | g072 | UNSUPPORTED_KEIL_OMF | [mx251/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g072/mx251/result.json) |
| keil | g072 | FAILED | [keil/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g072/keil/result.json) |
| stc-soft | g072 | UNSUPPORTED_KEIL_OMF | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g072/stc-soft/result.json) |
| stc-mdu32 | g072 | UNSUPPORTED_KEIL_OMF | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g072/stc-mdu32/result.json) |
| mx251 | g079 | FAILED | [mx251/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g079/mx251/result.json) |
| stc-soft | g081 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g081/stc-soft/result.json) |
| stc-mdu32 | g081 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g081/stc-mdu32/result.json) |
| stc-soft | g100 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g100/stc-soft/result.json) |
| stc-mdu32 | g100 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g100/stc-mdu32/result.json) |
| stc-soft | g101 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g101/stc-soft/result.json) |
| stc-mdu32 | g101 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g101/stc-mdu32/result.json) |
| mx251 | g102 | FAILED | [mx251/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g102/mx251/result.json) |
| stc-soft | g102 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g102/stc-soft/result.json) |
| stc-mdu32 | g102 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g102/stc-mdu32/result.json) |
| stc-soft | g103 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g103/stc-soft/result.json) |
| stc-mdu32 | g103 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g103/stc-mdu32/result.json) |
| mx251 | g105 | FAILED | [mx251/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g105/mx251/result.json) |
| stc-soft | g105 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g105/stc-soft/result.json) |
| stc-mdu32 | g105 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g105/stc-mdu32/result.json) |
| mx251 | g106 | FAILED | [mx251/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g106/mx251/result.json) |
| stc-soft | g106 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g106/stc-soft/result.json) |
| stc-mdu32 | g106 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g106/stc-mdu32/result.json) |
| mx251 | g107 | FAILED | [mx251/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g107/mx251/result.json) |
| stc-soft | g107 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g107/stc-soft/result.json) |
| stc-mdu32 | g107 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g107/stc-mdu32/result.json) |
| mx251 | g108 | FAILED | [mx251/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g108/mx251/result.json) |
| stc-soft | g108 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g108/stc-soft/result.json) |
| stc-mdu32 | g108 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g108/stc-mdu32/result.json) |
| mx251 | g109 | FAILED | [mx251/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g109/mx251/result.json) |
| stc-soft | g109 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g109/stc-soft/result.json) |
| stc-mdu32 | g109 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g109/stc-mdu32/result.json) |
| mx251 | g110 | FAILED | [mx251/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g110/mx251/result.json) |
| stc-soft | g110 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g110/stc-soft/result.json) |
| stc-mdu32 | g110 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g110/stc-mdu32/result.json) |
| stc-soft | g111 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g111/stc-soft/result.json) |
| stc-mdu32 | g111 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g111/stc-mdu32/result.json) |
| stc-soft | g112 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g112/stc-soft/result.json) |
| stc-mdu32 | g112 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g112/stc-mdu32/result.json) |
| stc-soft | g113 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g113/stc-soft/result.json) |
| stc-mdu32 | g113 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g113/stc-mdu32/result.json) |
| stc-soft | g114 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g114/stc-soft/result.json) |
| stc-mdu32 | g114 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g114/stc-mdu32/result.json) |
| stc-soft | g115 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g115/stc-soft/result.json) |
| stc-mdu32 | g115 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g115/stc-mdu32/result.json) |
| mx251 | g125 | UNSUPPORTED_KEIL_OMF | [mx251/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g125/mx251/result.json) |
| stc-soft | g125 | UNSUPPORTED_KEIL_OMF | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g125/stc-soft/result.json) |
| stc-mdu32 | g125 | UNSUPPORTED_KEIL_OMF | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g125/stc-mdu32/result.json) |
| stc-soft | g128 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g128/stc-soft/result.json) |
| stc-mdu32 | g128 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g128/stc-mdu32/result.json) |
| stc-soft | g130 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g130/stc-soft/result.json) |
| stc-mdu32 | g130 | UNSUPPORTED_ASSEMBLY_INPUT | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g130/stc-mdu32/result.json) |
| mx251 | g131 | UNSUPPORTED_KEIL_OMF | [mx251/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g131/mx251/result.json) |
| stc-soft | g131 | UNSUPPORTED_KEIL_OMF | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g131/stc-soft/result.json) |
| stc-mdu32 | g131 | UNSUPPORTED_KEIL_OMF | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g131/stc-mdu32/result.json) |
| mx251 | g132 | UNSUPPORTED_KEIL_OMF | [mx251/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g132/mx251/result.json) |
| stc-soft | g132 | UNSUPPORTED_KEIL_OMF | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g132/stc-soft/result.json) |
| stc-mdu32 | g132 | UNSUPPORTED_KEIL_OMF | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g132/stc-mdu32/result.json) |
| mx251 | g137 | UNSUPPORTED_KEIL_OMF | [mx251/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g137/mx251/result.json) |
| stc-soft | g137 | UNSUPPORTED_KEIL_OMF | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g137/stc-soft/result.json) |
| stc-mdu32 | g137 | UNSUPPORTED_KEIL_OMF | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g137/stc-mdu32/result.json) |
| mx251 | g140 | FAILED | [mx251/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g140/mx251/result.json) |
| keil | g140 | FAILED | [keil/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g140/keil/result.json) |
| stc-soft | g140 | FAILED | [stc-soft/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g140/stc-soft/result.json) |
| stc-mdu32 | g140 | FAILED | [stc-mdu32/result.json](E:/STVAPP/MX251-baseline/work/stc32g-box-20261114/projects/g140/stc-mdu32/result.json) |

## 对 Keil 绝对增量最大的 C 工程

| 编号 | 工程 | MX | Keil | MX/Keil | 增量 |
|---|---|---:|---:|---:|---:|
| g083 | 46-MDU32–乘法和除法单元 | 9720 | 1551 | 626.69% | +8169 |
| g070 | 38-2.4寸ILI9325驱动TFT显示屏实验程序-带触摸功能\Project | 26719 | 19536 | 136.77% | +7183 |
| g133 | 83-老梁示波器-CDC虚拟显示 | 11235 | 6979 | 160.98% | +4256 |
| g091 | 52-CAN1-CAN2同时连接总线最多可用4组过滤器 | 6459 | 3471 | 186.08% | +2988 |
| g141 | 89-USB CDC转双串口-做自动停电上电烧录器 | 7366 | 4656 | 158.20% | +2710 |
| g090 | 51-CAN1-CAN2总线标准帧扩展帧同时使用收发测试 | 5873 | 3325 | 176.63% | +2548 |
| g128 | 79-UART-SPI-LCM_DMA_交替使用下载显示图片例程 | 6980 | 4616 | 151.21% | +2364 |
| g119 | 70-CDC(Communication Device Class)协议范例 | 5484 | 3531 | 155.31% | +1953 |
| g134 | 84-USB鼠标-键盘复合设备例程 | 5403 | 3523 | 153.36% | +1880 |
| g069 | 37-2.4寸ILI9341驱动TFT显示屏实验程序\Project | 20312 | 18449 | 110.10% | +1863 |
| g123 | 74-MSC(Mass Storage Class)协议范例 | 5914 | 4061 | 145.63% | +1853 |
| g120 | 71-基于HID协议的USB键盘范例 | 4674 | 3040 | 153.75% | +1634 |
| g121 | 72-基于HID协议的USB鼠标范例 | 4302 | 2687 | 160.10% | +1615 |
| g122 | 73-基于WINUSB协议的范例 | 4531 | 2951 | 153.54% | +1580 |
| g118 | 69-HID(Human Interface Device)协议范例 | 4159 | 2633 | 157.96% | +1526 |

## 全部工程

| 编号 | 工程 | MX251 | Keil5 | STC soft | STC mdu32 | MX/Keil |
|---|---|---:|---:|---:|---:|---:|
| g000 | 00-端口模式设置\C语言 | 307 | 60 | 35 | 35 | 511.67% |
| g001 | 00-端口模式设置\汇编 | FAILED | 41 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g002 | 00-端口模式设置\汇编 - 超64K | FAILED | 171 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g003 | 01.1-用P6口做跑马灯-入门版\C语言 | 427 | 180 | 164 | 164 | 237.22% |
| g004 | 01.1-用P6口做跑马灯-入门版\汇编 | FAILED | 153 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g005 | 01.2-用P6口做跑马灯\C语言 | 456 | 169 | 167 | 167 | 269.82% |
| g006 | 01.2-用P6口做跑马灯\汇编 | FAILED | 115 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g007 | 02-Timer0-Timer1-Timer2-Timer3-Timer4测试程序\C语言 | 511 | 286 | 828 | 828 | 178.67% |
| g008 | 02-Timer0-Timer1-Timer2-Timer3-Timer4测试程序\汇编 | FAILED | 289 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g009 | 03-用P6，P7口驱动8个数码管\C语言 | 1021 | 536 | 1044 | 1044 | 190.49% |
| g010 | 03-用P6，P7口驱动8个数码管\汇编 | FAILED | 350 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g011 | 04-利用T0,T1做外部计数器\C语言 | 933 | 524 | 1018 | 1018 | 178.05% |
| g012 | 04-利用T0,T1做外部计数器\汇编 | FAILED | 320 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g013 | 05-利用定时器测量脉冲宽度\C语言 | 1139 | 514 | 1096 | 1096 | 221.60% |
| g014 | 05-利用定时器测量脉冲宽度\汇编 | FAILED | 483 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g015 | 06-外中断INT0-INT1-INT2-INT3- INT4测试\C语言 | 1088 | 542 | 1068 | 1068 | 200.74% |
| g016 | 06-外中断INT0-INT1-INT2-INT3- INT4测试\汇编 | FAILED | 320 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g017 | 07-睡眠-唤醒定时器唤醒\C语言 | 875 | 562 | 1106 | 1106 | 155.69% |
| g018 | 07-睡眠-唤醒定时器唤醒\汇编 | FAILED | 300 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g019 | 08-睡眠-外部中断唤醒\C语言 | 937 | 518 | 1061 | 1061 | 180.89% |
| g020 | 08-睡眠-外部中断唤醒\汇编 | FAILED | 346 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g021 | 09-看门狗复位测试程序\C语言 | 764 | 331 | 355 | 355 | 230.82% |
| g022 | 09-看门狗复位测试程序\汇编 | FAILED | 274 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g023 | 10-串口1中断模式与电脑收发测试\C语言 | 769 | 346 | 951 | 951 | 222.25% |
| g024 | 10-串口1中断模式与电脑收发测试\汇编 | FAILED | 294 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g025 | 11-串口2中断模式与电脑收发测试\C语言 | 774 | 335 | 938 | 938 | 231.04% |
| g026 | 11-串口2中断模式与电脑收发测试\汇编 | FAILED | 280 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g027 | 12-串口3中断模式与电脑收发测试\C语言 | 740 | 333 | 945 | 945 | 222.22% |
| g028 | 12-串口3中断模式与电脑收发测试\汇编 | FAILED | 275 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g029 | 13-串口4中断模式与电脑收发测试\C语言 | 740 | 333 | 945 | 945 | 222.22% |
| g030 | 13-串口4中断模式与电脑收发测试\汇编 | FAILED | 275 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g031 | 14.1-串口1串口2中断模式与电脑收发测试\C语言 | 1170 | 549 | 1242 | 1242 | 213.11% |
| g032 | 14.1-串口1串口2中断模式与电脑收发测试\汇编 | FAILED | 481 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g033 | 14.2-串口2与串口3通信测试程序 | 1262 | 665 | 1345 | 1345 | 189.77% |
| g034 | 15-IO行列扫描键盘数码管显示键值和调整时间\C语言 | 1669 | 961 | 1483 | 1483 | 173.67% |
| g035 | 15-IO行列扫描键盘数码管显示键值和调整时间\汇编 | FAILED | 629 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g036 | 16-ADC键盘扫描数码管显示键值和调整时间\C语言 | 1732 | 1005 | 1620 | 1620 | 172.34% |
| g037 | 16-ADC键盘扫描数码管显示键值和调整时间\汇编 | FAILED | 696 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g038 | 17-NTC测温度数码管显示\C语言 | 1776 | 1075 | 2482 | 2482 | 165.21% |
| g039 | 17-NTC测温度数码管显示\汇编 | FAILED | 1055 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g040 | 18-通过串口2发送命令读写EEPROM测试程序\C语言 | 2248 | 1596 | 3144 | 3144 | 140.85% |
| g041 | 18-通过串口2发送命令读写EEPROM测试程序\汇编 | FAILED | 1216 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g042 | 19-使用LVD低压检测中断保存EEPROM\C语言 | 1520 | 793 | 1616 | 1616 | 191.68% |
| g043 | 19-使用LVD低压检测中断保存EEPROM\汇编 | FAILED | 679 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g044 | 20-使用比较器检测低电压时保存数据到EEPROM\C语言 | 1909 | 956 | 1979 | 1979 | 199.69% |
| g045 | 20-使用比较器检测低电压时保存数据到EEPROM\汇编 | FAILED | 791 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g046 | 23-利用P3.7做比较器正极输入源，内部1.19V或P3.6口做负极输入源\C语言 | 406 | 160 | 698 | 698 | 253.75% |
| g047 | 23-利用P3.7做比较器正极输入源，内部1.19V或P3.6口做负极输入源\汇编 | FAILED | 135 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g048 | 25-高级PWM1-PWM2-PWM3-PWM4，驱动P6口呼吸灯实验程序\C语言 | 1448 | 691 | 1046 | 1046 | 209.55% |
| g049 | 25-高级PWM1-PWM2-PWM3-PWM4，驱动P6口呼吸灯实验程序\汇编 | FAILED | 611 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g050 | 26-高级PWM5-PWM6-PWM7-PWM8输出测试程序\C语言 | 1406 | 649 | 1043 | 1043 | 216.64% |
| g051 | 26-高级PWM5-PWM6-PWM7-PWM8输出测试程序\汇编 | FAILED | 609 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g052 | 27-I2C从机中断模式与IO口模拟I2C主机进行自发自收\C语言 | 1791 | 896 | 1686 | 1686 | 199.89% |
| g053 | 27-I2C从机中断模式与IO口模拟I2C主机进行自发自收\汇编 | FAILED | 809 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g054 | 28-I2C主机模式访问AT24C02程序\C语言 | 1419 | 742 | 1430 | 1430 | 191.24% |
| g055 | 28-I2C主机模式访问AT24C02程序\汇编 | FAILED | 707 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g056 | 29-红外遥控接收程序(NEC码)-数码管显示用户地址和键值\C语言 | 1137 | 584 | 1240 | 1240 | 194.69% |
| g057 | 29-红外遥控接收程序(NEC码)-数码管显示用户地址和键值\汇编 | FAILED | 424 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g058 | 30-红外发射程序(NEC码)-使用PWM4产生38KHz载波\C语言 | 1281 | 872 | 1461 | 1461 | 146.90% |
| g059 | 30-红外发射程序(NEC码)-使用PWM4产生38KHz载波\汇编 | FAILED | 842 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g060 | 31-IO扫描键红外发射-同时接收数码管显示用户码键值程序\C语言 | 1942 | 1248 | 1941 | 1941 | 155.61% |
| g061 | 31-IO扫描键红外发射-同时接收数码管显示用户码键值程序\汇编 | FAILED | 1112 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g062 | 32-硬件SPI访问FLASH-PM25LV040-串口2监控 | 3362 | 2710 | 5351 | 5351 | 124.06% |
| g063 | 33-IO模拟SPI访问FLASH-PM25LV040-串口2监控 | 3337 | 2705 | 5318 | 5318 | 123.36% |
| g064 | 34-P1.3做ADC-使用内部基准计算外部电压\C语言 | 1190 | 509 | 1139 | 1139 | 233.79% |
| g065 | 34-P1.3做ADC-使用内部基准计算外部电压\汇编 | FAILED | 392 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g066 | 35-板上的32K xdata测试程序 | 34518 | 33903 | 35950 | 35950 | 101.81% |
| g067 | 36.1-LCD1602显示程序 | 1054 | 725 | 841 | 841 | 145.38% |
| g068 | 36.2-LCD128x64显示图形文字-ST7920 | 2110 | 1791 | 2609 | 2609 | 117.81% |
| g069 | 37-2.4寸ILI9341驱动TFT显示屏实验程序\Project | 20312 | 18449 | 31210 | 31210 | 110.10% |
| g070 | 38-2.4寸ILI9325驱动TFT显示屏实验程序-带触摸功能\Project | 26719 | 19536 | 30227 | 30227 | 136.77% |
| g071 | 39-通过USB发送命令读取ADC测试程序 | 3172 | 1655 | 2988 | 2988 | 191.66% |
| g072 | 40-科学计算器使用CDC虚拟液晶屏显示 | UNSUPPORTED_KEIL_OMF | FAILED | UNSUPPORTED_KEIL_OMF | UNSUPPORTED_KEIL_OMF | — |
| g073 | 41-软件修改内部RC主频\C语言 | 757 | 477 | 461 | 461 | 158.70% |
| g074 | 41-软件修改内部RC主频\汇编 | FAILED | 336 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g075 | 42-一线制温度传感器 DS18B20 测温\C语言 | 1291 | 1046 | 1579 | 1579 | 123.42% |
| g076 | 42-一线制温度传感器 DS18B20 测温\汇编 | FAILED | 551 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g077 | 43-IO口推挽输出驱动有源蜂鸣器实验程序\C语言 | 724 | 410 | 999 | 999 | 176.59% |
| g078 | 43-IO口推挽输出驱动有源蜂鸣器实验程序\汇编 | FAILED | 334 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g079 | 44-高级PWM输出两路互补SPWM\C语言 | FAILED | 762 | 1280 | 1280 | — |
| g080 | 44-高级PWM输出两路互补SPWM\汇编 | FAILED | 764 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g081 | 45-内部RTC时钟程序\C语言 | 2625 | 2243 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | 117.03% |
| g082 | 45-内部RTC时钟程序\汇编 | FAILED | 1021 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g083 | 46-MDU32–乘法和除法单元 | 9720 | 1551 | 4750 | 4750 | 626.69% |
| g084 | 47-SPI互为主从-SS设置主从-串口透传 | 1205 | 594 | 1292 | 1292 | 202.86% |
| g085 | 48-SPI互为主从-主模式忽略SS-串口透传 | 1208 | 600 | 1300 | 1300 | 201.33% |
| g086 | 49-CAN总线标准帧收发测试\C语言 | 3047 | 1543 | 3150 | 3150 | 197.47% |
| g087 | 49-CAN总线标准帧收发测试\汇编 | FAILED | 758 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g088 | 50-CAN总线扩展帧收发测试\C语言 | 3049 | 1545 | 3158 | 3158 | 197.35% |
| g089 | 50-CAN总线扩展帧收发测试\汇编 | FAILED | 844 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g090 | 51-CAN1-CAN2总线标准帧扩展帧同时使用收发测试 | 5873 | 3325 | 7886 | 7886 | 176.63% |
| g091 | 52-CAN1-CAN2同时连接总线最多可用4组过滤器 | 6459 | 3471 | 8129 | 8129 | 186.08% |
| g092 | 53-UART转LIN总线报文测试程序 | 1576 | 920 | 2087 | 2087 | 171.30% |
| g093 | 54-LIN总线从机收发测试 | 1647 | 1694 | 2608 | 2608 | 97.23% |
| g094 | 55-LIN总线主机收发测试 | 1688 | 1706 | 2726 | 2726 | 98.94% |
| g095 | 56-LIN总线从机收发测试-USART | 1988 | 1756 | 3046 | 3046 | 113.21% |
| g096 | 57-LIN总线主机收发测试-USART | 1821 | 1718 | 2282 | 2282 | 106.00% |
| g097 | 58-LIN总线从机收发测试-USART2 | 1991 | 1764 | 3053 | 3053 | 112.87% |
| g098 | 59-LIN总线主机收发测试-USART2 | 1814 | 1720 | 2275 | 2275 | 105.47% |
| g099 | 60-DMA-ADC采样数据自动存储\16路ADC转换-串口返回结果 | 1178 | 1198 | 1820 | 1820 | 98.33% |
| g100 | 60-DMA-ADC采样数据自动存储\16路ADC转换使用DMA-串口返回结果 | 1355 | 1288 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | 105.20% |
| g101 | 60-DMA-ADC采样数据自动存储\16路ADC转换使用DMA-串口返回结果-结构体存储结果 | 1599 | 1285 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | 124.44% |
| g102 | 61-DMA-M2M存储器与存储器之间读写 | FAILED | 1279 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g103 | 62-DMA-SPI与存储器数据自动收发\硬件SPI使用DMA访问FLASH-串口2监控 | 4379 | 3313 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | 132.18% |
| g104 | 62-DMA-SPI与存储器数据自动收发\硬件SPI访问FLASH-串口2监控-2T | 4118 | 3194 | 9424 | 9424 | 128.93% |
| g105 | 63-DMA-UART串口与存储器数据自动收发\串口1使用DMA中断模式与电脑收发测试 | FAILED | 1371 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g106 | 63-DMA-UART串口与存储器数据自动收发\串口2使用DMA中断模式与电脑收发测试 | FAILED | 1357 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g107 | 63-DMA-UART串口与存储器数据自动收发\串口3使用DMA中断模式与电脑收发测试 | FAILED | 1352 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g108 | 63-DMA-UART串口与存储器数据自动收发\串口4使用DMA中断模式与电脑收发不定长数据-软件超时 | FAILED | 1699 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g109 | 63-DMA-UART串口与存储器数据自动收发\串口4使用DMA中断模式与电脑收发测试 | FAILED | 1351 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g110 | 64-DMA综合应用-SPI互为主从-M2M-串口透传 | FAILED | 1848 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | — |
| g111 | 65-DMA-LCM液晶屏接口测试\LCM_Test_6800_DMA_8bit_ST7920 | 2421 | 1990 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | 121.66% |
| g112 | 65-DMA-LCM液晶屏接口测试\LCM_Test_8080_DMA_8bit_ILI9341 | 6081 | 4976 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | 122.21% |
| g113 | 65-DMA-LCM液晶屏接口测试\LCM_Test_8080_LCM_8bit_ILI9341 | 5783 | 4838 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | 119.53% |
| g114 | 66-DMA-I2C与存储器数据自动收发\DMA-I2C主机模式读写AT24C02-串口监控 | 3077 | 2533 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | 121.48% |
| g115 | 66-DMA-I2C与存储器数据自动收发\DMA-I2C从机中断模式与IO口模拟I2C主机进行自发自收 | 2360 | 1484 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | 159.03% |
| g116 | 67-硬件高速HSSPI访问FLASH-PM25LV040-串口2监控 | 4191 | 3340 | 9713 | 9713 | 125.48% |
| g117 | 68-高速HSPWM1-HSPWM2-HSPWM3-HSPWM4，驱动P6口呼吸灯实验程序 | 1538 | 826 | 1614 | 1614 | 186.20% |
| g118 | 69-HID(Human Interface Device)协议范例 | 4159 | 2633 | 3747 | 3747 | 157.96% |
| g119 | 70-CDC(Communication Device Class)协议范例 | 5484 | 3531 | 4677 | 4677 | 155.31% |
| g120 | 71-基于HID协议的USB键盘范例 | 4674 | 3040 | 4313 | 4313 | 153.75% |
| g121 | 72-基于HID协议的USB鼠标范例 | 4302 | 2687 | 3809 | 3809 | 160.10% |
| g122 | 73-基于WINUSB协议的范例 | 4531 | 2951 | 4014 | 4014 | 153.54% |
| g123 | 74-MSC(Mass Storage Class)协议范例 | 5914 | 4061 | 6459 | 6459 | 145.63% |
| g124 | 75-通过定时器周期性调度任务综合例程 | 3428 | 2174 | 4342 | 4342 | 157.68% |
| g125 | 76-通过USB HID协议打印数据信息-可用于调试 | UNSUPPORTED_KEIL_OMF | 6966 | UNSUPPORTED_KEIL_OMF | UNSUPPORTED_KEIL_OMF | — |
| g126 | 77-USART1复用SPI_使用DMA和寄存器方式访问Flash例程 | 1917 | 1809 | 1613 | 1613 | 105.97% |
| g127 | 78-USART1复用SPI与USART2复用SPI相互通信例程 | 862 | 499 | 1186 | 1186 | 172.75% |
| g128 | 79-UART-SPI-LCM_DMA_交替使用下载显示图片例程 | 6980 | 4616 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | 151.21% |
| g129 | 80-高级PWM输出-周期可调-脉冲计数 | 811 | 395 | 924 | 924 | 205.32% |
| g130 | 81-内部RTC时钟低功耗休眠唤醒-比较器检测电压程序 | 1542 | 1501 | UNSUPPORTED_ASSEMBLY_INPUT | UNSUPPORTED_ASSEMBLY_INPUT | 102.73% |
| g131 | 82-老刘爱捣鼓示波器，使用OLED屏显示或者虚拟OLED接口显示\1-老刘爱捣鼓示波器，使用SPI OLED屏显示或者虚拟OLED接口显示-CDC协议通信 | UNSUPPORTED_KEIL_OMF | 13799 | UNSUPPORTED_KEIL_OMF | UNSUPPORTED_KEIL_OMF | — |
| g132 | 82-老刘爱捣鼓示波器，使用OLED屏显示或者虚拟OLED接口显示\2-老刘爱捣鼓示波器，使用I2C OLED屏显示或者虚拟OLED接口显示-CDC协议通信 | UNSUPPORTED_KEIL_OMF | 13982 | UNSUPPORTED_KEIL_OMF | UNSUPPORTED_KEIL_OMF | — |
| g133 | 83-老梁示波器-CDC虚拟显示 | 11235 | 6979 | 9463 | 9463 | 160.98% |
| g134 | 84-USB鼠标-键盘复合设备例程 | 5403 | 3523 | 5016 | 5016 | 153.36% |
| g135 | 85-老梁示波器-信号发生器 | 726 | 416 | 872 | 872 | 174.52% |
| g136 | 86.1-串口绘图-使用UART1接口 | 767 | 435 | 949 | 949 | 176.32% |
| g137 | 86.2-串口绘图-使用USB-CDC串口 | UNSUPPORTED_KEIL_OMF | 3438 | UNSUPPORTED_KEIL_OMF | UNSUPPORTED_KEIL_OMF | — |
| g138 | 87.1-利用高级PWMA+PWMB溢出中断做2个定时器 | 571 | 379 | 1061 | 1061 | 150.66% |
| g139 | 87.2-利用高级PWMA+PWMB实现8个定时器 | 1998 | 887 | 1477 | 1477 | 225.25% |
| g140 | 88-USB-CDC转双串口-根据串口波特率自动调整主频 | FAILED | FAILED | FAILED | FAILED | — |
| g141 | 89-USB CDC转双串口-做自动停电上电烧录器 | 7366 | 4656 | 6499 | 6499 | 158.20% |

完整诊断、HEX、MAP、构建参数和输入 SHA256 保存在：
`E:\STVAPP\MX251-baseline\work\stc32g-box-20261114`。

运行器为 `tools/research/stc32g_box_matrix.py`，报告器为 `tools/research/report_stc32g_box.py`。
运行器参数：`--root <上述证据目录> --baseline E:/STVAPP/MX251-baseline --corpus E:/MCU/MX251-STC-SDK/research/official-box-comparison-20261101 --jobs 4`。
默认复用已落盘结果；新一轮应使用新的证据目录。源码包、官方 worker、原始工程、适配文件及工具指纹均已保留。
