# 通用多字节位域（2026-11-14）

基于 `69deb8b`；实现与测试提交 `23ff345`。这次补齐 9～32 位的通用 C 位域读写，没有 SDK、工程名或器件型号条件。

## 实现与布局

前端将大于一字节的位域对齐到字节边界；本轮遵守该布局，没有改成从任意位偏移开始紧密跨字节打包。与普通整数不同，既有位域 ABI 按低字节在前存放。

位域展开为普通字节读写、掩码、移位及组合操作，再进入原有寄存器分配与择优流程。最后一个不足整字节的存储执行读改写，保留相邻位；完整字节直接写入。17～24 位字段在 MIR 中占四字节，物理内存仍恰好访问三字节。新增有符号扩展和前端生成的无损 `_BitInt(24)` 中间转换；这没有扩展到任意三字节对象、算术或公开 ABI。

测试同时发现既有常量转换错误：`valCastLiteral` 将位域常量强制经过 16 位 `int`。例如有符号 17 位表达式中的 `0xffff` 掩码变成 `-1`，24 位掩码也会丢失高位。现在常量按实际位域宽度截断及符号扩展，`floatFromVal`、转换和重转换保持同一表示。原来的新旧后端互访测试在修复后全部通过。

## 独立测试

`tools/research/multibyte_bitfield_fixture.py` 生成全部 24 种宽度，覆盖有符号/无符号、volatile、CODE、DATA、EDATA、XDATA、栈数组、别名、相邻位保留、零/高位/极值和实际对象字节布局。另有 12/16/17/24/31/32 位的新旧后端互访。主机用 GCC 独立计算语义结果；目标字节布局另外按 MX251 的既有 ABI 检查，没有假定主机位域布局等同于目标。

- ABI2：**1920/1920 PASS**。
- ABI4：**1920/1920 PASS**。
- 原有全部 36 种字节位域布局：**2304/2304 PASS**。
- 本轮实板合计 **6144/6144**，STC-Link1D 自动发现，串口 COM13。
- ABI2/ABI4 × small/large × size/speed：8 组配置、每组 72 个基础函数全部可导入；280 个最终选用新后端的读写函数通过实际字节事务次数与顺序检查。成本择优仍可选择旧后端。
- 原有 8 组字节访问检查通过；原来用于回退检查的 12 位样例已改为 33 位，仍与明确使用旧后端的 REL 相同。

原用户程序压力集 63/63 单元成功，三种目标描述的纯 C REL 一致；与布尔候选版的全部 63 个 REL 逐字节一致。主机 3072 次求值、1536 对等价结果通过。本轮未重复原压力集的硬件测试；本轮硬件结果是上述 6144 项。

## 体积

同源 ABI2/small/size 的 72 个基础函数，原始 REL CSEG **9457 → 8648 B**，减少 **809 B（8.55%）**。43 个缩小、28 个不变、1 个增大；唯一增大项是 `code32`，增加 4 B。

| 函数 | 上版 B | 本版 B | 变化 B |
| --- | ---: | ---: | ---: |
| access12 | 115 | 86 | -29 |
| signed17 | 141 | 141 | +0 |
| access24 | 116 | 97 | -19 |
| code24 | 128 | 120 | -8 |
| access32 | 121 | 120 | -1 |
| code32 | 132 | 136 | +4 |

这里是链接共享前的函数范围与代码段大小。`signed17` 等保持原大小，说明支持新后端不保证每个函数立即缩小；运行时间尚未与 Keil/STC-IDE 建立对应达标结论。

## 官方工程回归

STC32G 既有可构建集合 **97/97** 成功；0 项缩小、97 项不变、0 项增大；总代码 **249132 → 249132 B**。源文件和配置与父版本逐字节一致。

| 参照 | 参考项 | ≤110% | ≤120% |
| --- | ---: | ---: | ---: |
| Keil C251 | 97 | 33 | 60 |
| STC-IDE | 79 | 73 | 77 |

全部 97 项大小不变。

Keil/STC-IDE 沿用同源参考产物。97 项是既有可构建子集，未包含既有源错误、未接入 OMF 或纯汇编项目；此次官方工程验证构建和体积，没有逐项执行板测。

## 交付与后续

候选入口：`E:/STVAPP/MX251-baseline/work/multibyte-bitfields-20261114/final/Build-Optimized.ps1`。

编译器 SHA-256：`1330e2d565ba51315d86797502027b8a3cf33166783d3d6ddec887b2ac00b6d1`。
ABI2 固件：`29ab51b00deedcce6be24f7e4948b879a3275f0f7629ba41266b91a8720d02c7`。
ABI4 固件：`9255a80e381850973dc22b6629cb061b1f12f8228e8378b6560dcf5ca2569e1b`。
字节位域固件：`036f3c4e6375c9df08fac3fc3cc7724ec71708c8fb442dd96ac146b91c790692`。

开发候选已经封装，默认冻结核心未替换；主目录用户改动保留。最后保留字节位域测试固件。初次失败的 `abi2-capture` 留作定位证据；正式验收使用 `abi2-capture-final`、`abi4-capture-final`、`byte-capture`，编译结果使用 `contracts-qualified`、`user-stress-qualified`。

大于 32 位的位域、运行期近指针 ABI、超大函数容量与局部降级仍属后续范围。当前已有回退统计中，下一轮应优先梳理有序内联汇编的位操作语义，找出能够普遍接入的形态，再决定实现范围。

```powershell
python -B tools/research/multibyte_bitfield_fixture.py reference --output <host> --gcc <gcc.exe>
python -B tools/research/multibyte_bitfield_fixture.py contracts --output <contracts> --sdcc <sdcc.exe> --support <toolchain>
python -B tools/research/multibyte_bitfield_fixture.py board --abi 2 --output <project>
pwsh -File <candidate>/Build-Optimized.ps1 -ProjectPath <project>
python -B tools/research/multibyte_bitfield_fixture.py capture --hex <firmware.hex> --reference <reference.json> --output <capture>
```
