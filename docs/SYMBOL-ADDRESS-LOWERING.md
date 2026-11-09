# 符号地址直接传参与平坦指针转换

2026-11-09，从 `3ddd782` 接续，分支 `codex/symbol-address-lowering`。本轮补齐编译器的地址运输能力，没有修改 SDK 输入、参数 ABI 或构建选项。详细指纹见 [symbol-address-lowering-evidence.json](symbol-address-lowering-evidence.json)。

## 改动

此前 CODE 指针转换为通用指针会令整个函数退回旧后端，字符串调用者和局部指针数组因此不能使用已有的宽寄存器、调用保存及栈访问优化。历史 `flat-pointer-cast-v1` 已说明单独放开转换会增大镜像，本轮同时实现两项能力：

- 三字节 CODE、XDATA、generic 指针按已有平坦 ABI 复制相同的 24 位地址。目的值仍保留自己的访问空间，后续 CODE 读取仍走 CODE 路径。近指针、分页指针和函数指针没有借此获得新的转换方式。
- 在分配寄存器前跟踪单一定义的静态符号地址和指针复制链。普通 ABI2 调用中的常量地址保留为“符号＋偏移”，首参直接装入 DPTR/B，栈参数直接压入高字节和低字重定位。删除仅供这些调用使用的地址构造及复制；可变指针、指针对象读取、局部栈对象地址继续运输真实值。

`wide_constant_addresses.c.inc` 实现 MIR 变换，`wide_emit.c.inc` 完成 ABI 运输。`symbol_immediate.h` 让指令成本和寄存器效果分析识别同一种 `PUSHW #(symbol+offset)`；该指令编码为 4 字节，只更新栈，不读写通用寄存器。既有调用反馈仍按最终指令核对。

新能力随原 wide 后端启用，无新增用户开关。比较中沿用上一阶段显式开启的 `CalleePreservation`，该选项仍默认关闭。

## 完整工程结果

四个工程的所有输入哈希、项目配置和既有优化选项均与父版一致。以下是最终 HEX 实际占用字节。四项调用反馈均为 `FINAL_ACTUAL_CALL_PRESERVATION_VERIFIED`。

| 工程 | 父版 / B | 本轮 / B | 减少 / B | Keil / B | 本轮 / Keil |
| --- | ---: | ---: | ---: | ---: | ---: |
| p080 TFT | 21304 | 20902 | 402 | 18071 | 115.67% |
| p087 USB CDC | 4589 | 4581 | 8 | 3531 | 129.74% |
| p091 USB MSC | 5765 | 5651 | 114 | 4401 | 128.40% |
| p095 双 CDC | 6424 | 6416 | 8 | 4830 | 132.84% |

有代表性的实际函数指令变化：

| 函数 | 父版 / B | 本轮 / B |
| --- | ---: | ---: |
| TFT `Rotate_Test` | 441 | 268 |
| TFT `DrawTestPage` | 226 | 180 |
| TFT `Pic_test` | 248 | 203 |
| TFT `Chinese_Font_test` | 218 | 173 |
| TFT `Test_Color` | 366 | 324 |
| MSC `scsi_read_capacity10` | 127 | 91 |
| MSC `scsi_format_capacity` | 127 | 95 |
| MSC `scsi_read10` / `scsi_write10` | 各 116 | 各 93 |
| 三个 USB 工程的 `usb_setup` | 各 233 | 各 225 |

并非每个函数都缩小：MSC `scsi_request_sense` 为 75→89 B，TFT `LCD_Fill` 为 131→135 B；最终整工程仍分别净减 114 / 402 B。TFT `Fill_Triangel` 为 886→877 B，是调用生成与反馈变化的连带结果，不能将全部收益归为字符串本身。

最佳候选台账仍为 **40 个体积达到 120%，其中 18 个达到 110%**。此次只重建四个代表工程，不是统一版本的全 SDK 重跑；没有新增完整工程体积、时间双达线记录。

## 实板与编译检查

独立合成用例包含字符串首参、多个栈指针、CODE/通用指针转换、局部指针数组、循环修改的指针，以及 XDATA 数组地址。主机按字节计算期望散列；另保留既有字节控制流用例作为计时对照。没有用 SDK 算法专门识别或替换编译结果。

AI8051U-34K64，40 MHz，Timer0 /12T，UART1 115200。STC-Link1D 自动找到 COM13。

| 项目 | 父版 | 本轮 |
| --- | ---: | ---: |
| 测试固件 / B | 2934 | 1946 |
| 地址运输用例，64 输入的中位 tick 汇总 | 17600 | 12864 |
| 字节控制组，同口径 tick | 10163 | 10163 |
| 正确性 | 128 项、636 帧通过 | 128 项、630 帧通过 |

地址运输内核时间减少 **26.91%**，无计时溢出。这里的分母是父版 MX251，不是 Keil，也不是完整 TFT/USB 工程运行时间。

`check-symbol-addresses.py` 已通过 small/large × speed/size 四组编译及汇编，覆盖 ABI2/ABI4、指针返回与可变指针路径；同时验证符号 PUSHW 的成本、寄存器效果和不支持的表达式。返回指针的两个小函数由已有成本选择器选中更短的旧后端，不要求它们强行使用宽后端。

复现入口：

```powershell
python tools/research/symbol_address_fixture.py create --output <新工程目录>
python tools/research/symbol_address_fixture.py capture --hex <firmware.hex> --output <新记录目录>
```

本地研究目录：`E:/STVAPP/MX251-mainline-20261109/symbol-address`。完整工程在 `core-02-p080/p087/p091/p095`；硬件使用 `hardware-parent-03`、`hardware-new-03`，串口记录在 `capture-parent`、`capture-new`。开发核心 `core-02` 的编译器 SHA-256 为 `ff72ba6baa9056a43ce5de1e20fcc3001638d4d4efd6934d6849585ad298257d`。这是开发工具快照，源码以本分支为准，不是发行包。

## 中间结果与下一处缺口

第一版 `core-01` 漏接了符号 PUSHW 的效果/成本识别，MSC 在反馈复编时报错并回到原对象，得到 5945 B。修复后 `core-02` 为 5651 B；中间结果不计入最终成绩。

前两个合成夹具被原有窄乘法/取模准入条件挡住，未下载，也没有拿它们的相同 HEX 当新能力验证。最终 `fixture-03` 去掉了无关运算，实际进入宽后端并覆盖目标运输路径。

字符串转换这道门槛移除后，TFT `Test_FillRec`、`Test_Circle`、`Test_Triangle` 的下一处共同阻碍已经显现：DATA 数组的动态近地址索引。`Test_Read` 的下一处则是变参调用（`sprintf`）。这些比继续调整寄存器权重更值得研究；本轮没有扩展它们。
