# DATA 字段的原生宽访存

2026-11-10，基于 `916fe33`，分支 `codex/direct-field-access`。本轮让已确定对象、偏移和类型的普通 DATA 结构字段、固定数组元素，使用原生字/双字访存。完整 TFT 减少 199 B，两个 USB 工程也有净收益；SDK 源码和配置未修改。[证据](direct-field-access-evidence.json)保存输入、编译器、产物和实板记录指纹。

## 实现与覆盖

共享中端和宽后端已经把这些访问恢复成带有 `addressSymbol`、`addressOffset` 和准确宽度的 `WM_SYMBOL_LOAD/STORE`。但 `wmemory_symbol` 原先只给零偏移的完整标量对象发出原生 MOV；属于聚合对象或者带偏移时，即使值是普通 16/32 位整数，也重新拆成字节搬运。

本轮复用现有寻址结果，在原生 MOV 中带上字段偏移：

```asm
mov wr14,(_record+1)
mov dr24,(_record+3)
mov (_record+1),wr14
mov (_record+3),dr12
```

不增加导入类型、不改变 ABI，也不改动寄存器分配算法。原先能确认的字段及固定数组元素共同受益，包括非对齐偏移。volatile 和 SFR 保持原有字节访问路径；普通 ABI 参数要求的反向读取也保持原路径。既有显式 volatile 宽访问配置不变。移除了导入诊断中已经不准确的 `transaction=bytes` 文本。

本轮未引入对象缓存或把访问移过调用；跨函数的字节别名修改仍会被下一次字段读取观察到。

## 完整工程

使用与父版相同的冻结输入和 small/speed、FunctionSharing、MachineIr、CalleePreservation、普通调用反馈、函数 GC；TFT/MSC 继续使用 NativeIntegerRuntime。

| 工程 | 父版 / B | 本轮 / B | 减少 / B | Keil / B | 本轮 / Keil |
| --- | ---: | ---: | ---: | ---: | ---: |
| p080 TFT | 19696 | 19497 | 199 | 18071 | 107.89% |
| p087 USB CDC | 4451 | 4434 | 17 | 3531 | 125.57% |
| p091 USB MSC | 5386 | 5360 | 26 | 4401 | 121.79% |

三个工程均完成最终实际调用保存核对。TFT 继续低于体积 110% 线；MSC 距体积 120% 线还差 79 B，距 110% 线还差 519 B。完整工程相对 Keil 的运行时间尚未测得。

TFT 主要变化：`Test_Circle` 322→282 B、`Test_FillRec` 342→310 B、`Test_Triangle` 404→380 B、`Test_Color` 324→300 B、`LCD_Clear` 75→63 B。也记录到 `Show_Str` 增加 4 B、`LCD_direction` 增加 8 B，整工程净减少 199 B。

MSC 的 `scsi_read10`、`scsi_write10` 各减少 9 B，`usb_setup` 减少 8 B。收益来自字段访存的共同规则，没有按这些函数名称选择代码生成。

## 实板与编译检查

新增独立字段夹具包含偏移 1 的 16 位字段、偏移 3 的 32 位字段、固定数组元素、前后及中间相邻字节、16 位回绕和跨调用字节别名写入。别名修改函数使用普通后端生成逐字节访问；宽后端读取更新后的字段。另有 volatile 字段和既有控制流组。

AI8051U-34K64、40 MHz、Timer0 /12T、UART1 115200；自动定位 STC-Link1D / COM13。64 个种子、每个种子 8 轮，父版与新版分别对独立 Python 定宽整数参考核对。最后保留新版夹具。

| 项目 | 父版 | 本轮 |
| --- | ---: | ---: |
| 夹具固件 / B | 1541 | 1471 |
| 字段组 tick 汇总 | 11520 | 10560 |
| 控制组 tick 汇总 | 10087 | 10087 |
| 检查项 | 128 通过 | 128 通过 |
| 有效帧 | 630 | 634 |

各输入取多帧中位数再求和，无计时溢出。夹具体积减少 **4.54%**，字段组耗时减少 **8.33%**。此处不是完整 TFT 或 USB 工程相对 Keil 的耗时成绩。

`check-direct-fields.py` 通过 small/large 两组编译、汇编，确认字/双字字段与数组元素使用实际宽指令，volatile 仍保持 11 次字节读取和 11 次字节写入及其顺序。既有 `check-data-index.py` 的 small/large × speed/size 四组也通过。

## 未采用的指针字段扩展

研究先试过允许含三字节指针的 DATA 结构进入宽后端，并补了物理三字节存储、寄存器零填充和符号地址直接保存。第一版使 MSC/CDC 增加 178/177 B；符号地址保存和普通字段宽访存补齐后，仍分别增加 10/13 B。

原因是更多函数从普通后端切到宽后端后，地址运输、分支和尾部共享的成本超过局部收益。最终完整撤回指针字段类型扩展及其专用发射，仅保留普通标量字段宽访存。试验没有烧录到硬件，补丁保存在研究目录 `pointer-fields-trial.patch`。后续如果重做这项扩展，应同时处理新接入函数的实际后端选择和控制流成本。

`core-02` 的 USB 编译因不被汇编器接受的符号重定位 `>>0` 表达式中止，`core-03` 已修正该试验问题；最终 `core-04` 不包含整项指针扩展。

## 复现

研究目录：`E:/STVAPP/MX251-mainline-20261110/direct-pointer-fields`。最终核心 `core-04`，编译器 SHA-256 为 `02725244c5db56cd8e549e04ff8721606b5ee622e679660a875eaa2ba944e921`。验证后只整理了换行并移除过时诊断文字。公开发布工具链未更新。

```powershell
python core/sources/compiler/src/mcs251/tests/check-direct-fields.py --sdcc <sdcc.exe> --support <toolchain> --output <新目录>
python tools/research/direct_fields_fixture.py create --output <新工程目录>
python tools/research/direct_fields_fixture.py capture --hex <firmware.hex> --output <新记录目录>
```

`core-04-p080/p087/p091` 为完整工程，`board-parent/new` 为夹具构建，`capture-parent/new` 保存原始 UART 和逐输入核对结果。研究目录的 `run.py`、`hardware.py` 保存本次构建入口。
