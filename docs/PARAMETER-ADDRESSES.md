# 内联后还原参数栈地址

2026-11-10，基于 `b5df04c`，分支 `codex/parameter-addresses`。本轮将已知参数地址的间接访问还原为直接栈访问，并减少参数写回的寄存器中转。完整 TFT **20085 → 19968 B**，USB MSC **5625 → 5621 B**；SDK 源码及配置内容相同。

## 共同问题与实现

小型交换/修改函数内联后，`&参数` 经常还保留为完整的 24 位指针。旧后端为它生成 SPX 复制、偏移计算、高位规范化，随后才读写。现有局部对象地址折叠没有处理原始参数栈槽；前端的 `addrtaken` 标记也不会因为内联后的指针用途消失而清除。

新增 `wide_parameter_addresses.c.inc`：

- 从物理参数地址建立身份，沿单定义指针复制、平坦 RAM 指针转换及常量偏移传播。每次传播和使用均使用现有完整 CFG 支配判断，包括 switch 后继。
- FAR 与 generic RAM 指针在本后端都使用平坦 24 位表示，允许二者之间的表示不变转换。CODE、近指针、分页指针和整数转换不进入该地址链。
- 完整参数槽的非 volatile 访问改成原有 `WM_PARAM_LOAD/STORE`，保留访问位置、宽度和执行顺序。部分字节、动态地址及多来源指针继续使用原访问方式。
- 去除已无用途的参数地址链。上一轮值提升根据剩余 MIR 地址用途决定能否提升，不再单凭过时的 `addrtaken` 标记排除；任何仍被传出、比较、部分访问或 volatile 访问使用的地址都会保留。
- 统一改善参数写回：字节直接使用已分配低寄存器；字和双字使用源寄存器；溢出/常量才装临时值。三字节指针保持一字节加一字的原物理布局。

新变换仍比较完整候选的字节数、加权成本、局部帧，并执行原有循环成本判定。公开 ABI、参数偏移及 SDK 不变，没有函数名选择。

## 完整工程

使用上一轮冻结输入，沿用 FunctionSharing、MachineIr、CalleePreservation 和 NativeIntegerRuntime。源文件 SHA256 相同，配置按 JSON 内容比较相同；最终调用保存反馈均为 `FINAL_ACTUAL_CALL_PRESERVATION_VERIFIED`。

| 工程 | 父版 / B | 本轮 / B | 减少 / B | Keil / B | 本轮 / Keil |
| --- | ---: | ---: | ---: | ---: | ---: |
| p080 TFT | 20085 | 19968 | 117 | 18071 | 110.50% |
| p091 USB MSC | 5625 | 5621 | 4 | 4401 | 127.72% |

TFT 距体积 110% 线还差 **90 B**；没有取得完整工程与 Keil 的运行时间比较资格。

最终 HEX/RST：`Fill_Triangel` **877 → 754 B**，局部帧 **18 → 16 B**；调用反馈使 `LCD_ShowChar` 587 → 593 B，完整净收益已计入这项增长。MSC 的 `flash_writesector` 184 → 182 B、`scsi_out` 135 → 133 B。

三角形函数的 20 次间接参数访问采用了直接栈访问，但后续五个参数整体提升未胜出，因此最终仍保留参数栈槽。独立的 `pa_ordinary` 循环交换则同时采用地址折叠和值提升，145 → 89 B。两种选择均由完整候选成本决定。

## 实板与回归

AI8051U-34K64，40 MHz，Timer0 /12T，UART1 115200，自动定位 STC-Link1D。两套夹具均对父版和新版各下载一次。

| 夹具 | 固件 / B | 测试组 tick 汇总 | 控制组 tick 汇总 | 结果 |
| --- | ---: | ---: | ---: | --- |
| 三角形内核 | 2388 → 2274 | 10371 → 10235 | 10160 → 10160 | 父/新各 128 项，632/631 帧通过 |
| 参数取址与原 ABI 用例 | 5626 → 5537 | 15511.5 → 15175 | 10192 → 10192 | 父/新各 128 项，642/640 帧通过 |

每组 64 个输入取多帧中位数后求和，无计时溢出。三角形夹具体积减少 **4.77%**、测试组耗时减少 **1.31%**；参数夹具体积减少 **1.58%**、耗时减少 **2.17%**。这是相对父版 MX251 的夹具结果。

三角形源码从现有 SDK 原样提取，用追踪回调替代 LCD 输出；主机独立计算原算法结果。参数夹具在上一轮混合 ABI、递归、指针和浮点位模式用例上，增加循环交换、8/16/32 位取址修改、跨文件可见别名、部分字节修改及 volatile 指针访问。CPU 大端字节序纳入期望值计算。

`check-parameter-addresses.py` 完成 small/large × speed/size 四组编译，检查实际选择、1/2/4 字节折叠、3 字节指针存储、有符号参数，以及逃逸、部分访问、volatile 和多来源指针仍保留地址。

## 本轮未采用的方向

曾将地址已消失的三个局部标量整体提升为 MIR 值。在未加入最终调用反馈的 GUI 单元中，三角形候选由 990 增至 1128 B，加权成本 1003 → 1394，溢出值 5 → 21，虽帧由 18 降到 12 B，仍被完整候选比较拒绝。该原型已从生产代码撤下，没有进行整工程构建或下载。问题在于当前多定义值与分配器的交互，后续不能靠放松体积/成本门槛启用它。

研究目录 `E:/STVAPP/MX251-mainline-20261110/parameter-addresses` 保存 `private-local-values-rejected.c.inc` 和 `mir-audit/u0.asm`。早期 core-01 尚未处理 FAR/generic 转换，两个完整镜像与父版相同；最终为 core-02。不重复这两个已知无收益版本。

## 复现与接续

```powershell
python core/sources/compiler/src/mcs251/tests/check-parameter-addresses.py --sdcc <sdcc.exe> --support <toolchain目录> --output <新目录>
python tools/research/parameter_addresses_fixture.py create --output <新工程目录>
# 正常 Build-CMake.ps1，MachineIr、CalleePreservation、FunctionGc OFF。
python tools/research/parameter_addresses_fixture.py capture --hex <firmware.hex> --output <新记录目录>
```

研究目录中 `core-02-p080/p091` 为完整结果；`params-parent/new`、`triangle-parent/new` 为夹具，`*-capture-parent/new` 为原始串口记录，`check-01` 为回归。源文件及产物指纹见 [证据](parameter-addresses-evidence.json)。公开发布版本未更新。

仍存在的共同成本包括局部数组逐字节初始化，以及跨调用保留可重新形成的栈地址。它们应按完整镜像与动态执行成本继续比较；本轮没有实现这两项。
