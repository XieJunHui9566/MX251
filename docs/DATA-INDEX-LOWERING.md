# DATA 数组动态索引

2026-11-09，从 `566f1cb` 接续，分支 `codex/data-index-lowering`。本轮补齐宽后端对 DATA 数组动态索引的支持，SDK 源码、项目配置和公开参数 ABI 保持原样。指纹与完整工程差异见 [data-index-lowering-evidence.json](data-index-lowering-evidence.json)。

## 实现

此前 `array[i]` 形成的一字节 DATA 地址无法导入宽后端，即使函数其余部分已经支持，整个函数仍退回旧后端。本轮在 `wide_near_index.c.inc` 中导入这些地址，使函数能继续使用宽寄存器运算、调用保存和已有的内存访问优化。

- 从已知 DATA 对象形成内部一字节地址值，支持动态加减、指针复制、循环更新和转为 generic 指针。索引在地址形成时取样；后续修改索引或调用其他函数不改变已保存的地址。
- 固定地址定义先保留符号与偏移。分支汇合或循环需要真实地址值时，再物化同一临时变量的各个定义；只供固定偏移访问使用的地址不占用虚拟寄存器。
- 支持 8/16/32 位整数数组的动态读写，以及有完整声明的外部 DATA 数组。volatile 元素继续按既有逐字节访问路径处理。
- `WM_NEAR_ADDRESS` 生成一字节地址加减，读写节点按需零扩展地址。近指针参数 ABI、IDATA/分页指针、外部 volatile 对象及结构体成员转换沿用原路径。

新能力随原宽后端启用，没有新增构建开关。比较继续显式开启上一阶段的 `CalleePreservation`，该选项默认值没有改变。

## 完整工程

四个工程的输入 SHA-256、项目配置和构建选项均与父版一致，调用反馈均为 `FINAL_ACTUAL_CALL_PRESERVATION_VERIFIED`。以下统计最终 HEX 的实际占用字节。

| 工程 | 父版 / B | 本轮 / B | 减少 / B | Keil / B | 本轮 / Keil |
| --- | ---: | ---: | ---: | ---: | ---: |
| p080 TFT | 20902 | 20563 | 339 | 18071 | 113.79% |
| p087 USB CDC | 4581 | 4581 | 0 | 3531 | 129.74% |
| p091 USB MSC | 5651 | 5651 | 0 | 4401 | 128.40% |
| p095 双 CDC | 6416 | 6416 | 0 | 4830 | 132.84% |

TFT 的三个数组索引函数全部进入宽后端：`Test_FillRec` 为 446→346 B，`Test_Circle` 为 425→326 B，`Test_Triangle` 为 569→412 B。其他函数与调用反馈的连带变化计入完整工程净值，不只累加缩小项。

候选台账仍为 **40 个体积达到 120%，其中 18 个达到 110%**。本轮重建四个代表工程，没有全 SDK 重跑，也没有新增完整工程的体积、时间双达线记录。

## 实板结果

独立用例覆盖外部 DATA 数组、8/16/32 位读写、volatile 数组、索引更新后使用旧地址、跨调用保存、两种顺序的固定/动态地址分支、负偏移、指针减法、循环自增和 generic 转换。Python 按数组状态计算期望散列；另有原字节控制流用例作为计时对照。

AI8051U-34K64，40 MHz，Timer0 /12T，UART1 115200。STC-Link1D 自动定位到 COM13。

| 项目 | 父版 | 本轮 |
| --- | ---: | ---: |
| 测试固件 / B | 3203 | 1873 |
| 索引用例，64 输入的中位 tick 汇总 | 20768 | 9824 |
| 控制组，同口径 tick | 10160 | 10160 |
| 正确性 | 128 项、624 帧通过 | 128 项、644 帧通过 |

测试固件缩小 **41.52%**，索引内核耗时减少 **52.70%**，无计时溢出。分母是父版 MX251；这些数据不代替 Keil 对比或完整工程计时。

最终 `hardware-new-05` 与实际下载的 `hardware-new-03` 的 HEX SHA-256 完全相同：`38c552316c1e76bdfd585c491a9cc1e2736794461ee396c4c8b9ab5356476c50`，因此没有重复烧录。父版测试后没有恢复原固件。

`check-data-index.py` 通过 small/large × speed/size 四组编译与汇编，检查目标函数实际选择宽后端、动态读写宽度、volatile 访问、减法路径，并确认近指针参数和原结构体转换仍走既有路径。

复现入口：

```powershell
python core/sources/compiler/src/mcs251/tests/check-data-index.py --sdcc <sdcc.exe> --support <toolchain目录> --output <新检查目录>
python tools/research/data_index_fixture.py create --output <新工程目录>
python tools/research/data_index_fixture.py capture --hex <firmware.hex> --output <新记录目录>
```

研究目录：`E:/STVAPP/MX251-mainline-20261109/data-index`。完整工程位于 `core-04-p080/p087/p091/p095`，编译检查在 `check-04`，串口原始记录在 `capture-parent` 和 `capture-new`。开发核心为 `core-04`，编译器 SHA-256 为 `1488e3c953b20e795c7edc3ae474023cd861b53a5d4dfa50c5dd0602b02c6c55`。这是开发工具快照，源码以本分支为准。

## 中间结果与后续方向

`core-02/03` 顺带接收了原先未支持的结构体首成员转换，使 MSC 多出 59 B。最终版将这类转换留在既有路径，同时保留标量数组的动态地址能力。固定地址采用按需物化，避免无用地址值干扰寄存器分配。

前两个测试夹具遇到原有乘法准入限制，没有下载；最终 `fixture-03` 用递增值初始化数组，全部目标操作实际进入新路径。

本轮清除了 TFT 三个图形测试函数的共同导入缺口。下一处已知缺口仍是 `Test_Read` 的 `sprintf` 变参调用；应继续从通用调用能力和后端选择成本入手，而不是修改 SDK。
