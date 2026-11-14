# 通用布尔转换与平坦指针判零（2026-11-14）

基于 `e7769f4`，实现与测试提交 `55207bd`。没有 SDK、工程名或器件型号条件。

## 实现

8/16/32 位整数、现有平坦指针值到 `_Bool` 的转换，复用 MIR 的完整值判零节点，结果为字节 0/1。常量直接折叠，已经规范化的布尔值直接复制；仅用于分支的规范化与逻辑非可以合并为对原值的判断。普通整数窄化仍保留截断语义，例如 `(unsigned char)0x100` 为零。

布尔参数和返回值复用 ABI2/ABI4 的字节位置；布尔对象接入 DATA、EDATA、XDATA、栈、结构体和数组的现有访问路径。前端保留为符号的 XDATA/CODE 布尔对象展开成地址节点及单字节读写。volatile 取样和写回仍各自保留在原操作位置。

实板发现并修复了旧后端的 generic 指针判零错误：原来忽略第三字节，误将 `0x010000`、`0x800000` 等地址判断为空。MCS251 的三字节均为地址，现在完整参与判零；整函数择优选择旧后端时也得到正确结果。

## 独立用户程序

新增 `tools/research/boolean_fixture.py`，30 类 × 64 输入，包含零值、仅高字节非零、负数、generic/CODE/函数指针、布尔对象、数组、结构体首成员、参数三个位置、返回值、新旧后端互调，以及分支、状态机、解析器的等价写法。

ABI2 与 ABI4 各 **1920/1920 PASS**，合计 **3840/3840**。各自均执行刻意编译为旧后端的调用辅助函数。ABI2/ABI4 × small/large × size/speed 的 32 个翻译单元全部通过；24 个启用宽后端的单元没有语义拒绝，8 个辅助单元刻意使用旧后端。允许成本选择旧后端，与语义不支持导致的回退分开记录。

同源 small/size 的原始 REL 函数范围比较如下；字节数包括函数范围内的指令和跳转表，未经最终链接共享。

| 函数 | 上版 B | 本版 B | 变化 |
| --- | ---: | ---: | ---: |
| 复杂分支 | 113 | **61** | −46.0%，与直接分支相同 |
| 复杂状态机 | 153 | **93** | −39.2% |
| 复杂解析器 | 202 | **106** | −47.5% |
| 布尔自增与复合赋值 | 253 | **124** | −51.0% |
| 混合结构体 | 152 | **197** | +45 B，仍需改善地址/寄存器选择 |
| 函数指针判零 | 93 | **96** | +3 B |
| generic 指针判零 | 63 | **70** | +7 B，修正高地址被误判为空 |

四个测试单元 CSEG 总和 **3689 → 3292 B**，减少 397 B（约 10.8%）。状态机和解析器的复杂写法仍分别大于清晰写法的 66 B、80 B；语义回退消除后，仍有常规代码生成差距。

原六类用户压力程序：63/63 单元成功，三种目标描述的纯 C REL 一致；与上一版的全部 63 个 REL 逐字节一致。上一版 768 个实板结果作为历史证据保留，本轮未重跑原压力集板测；本轮硬件证据来自上述 3840 个布尔测试。主机 3072 次求值、1536 对等价结果通过。原压力集仍只有 `giant_messy` 的值容量回退；这不是对全部 C 程序的覆盖结论。另有 8 组既有位域检查通过，包含字节事务、CODE 读取及宽位域回退对象一致性。

## 官方工程回归

STC32G 既有可构建集合 **97/97** 成功，0 项缩小、97 项不变、0 项增大；总代码 **249132 → 249132 B**。源文件与配置逐字节保持上一版输入。

| 参照 | 参考项 | ≤110% | ≤120% |
| --- | ---: | ---: | ---: |
| Keil C251 | 97 | 33 | 60 |
| STC-IDE | 79 | 73 | 77 |

全部 97 项大小不变。

Keil/STC-IDE 沿用同源参考产物。97 项是既有可构建子集，未包含既有源错误、未接入 OMF 或纯汇编工程。官方工程本轮验证构建与体积；执行证据来自独立布尔测试。这些结果没有建立相对 Keil/STC-IDE 的运行时间达标结论。

## 范围与交付

浮点和 64 位数值到布尔、运行期近指针 ABI、特殊调用约定等继续遵循现有后端范围；跨字节位域、超大函数容量和局部降级接口留待后续。本轮不更改公开对象布局或 ABI。

入口：`E:/STVAPP/MX251-baseline/work/boolean-conversions-20261114/final/Build-Optimized.ps1`。
编译器 SHA-256：`eefeead02d8106f2d234ee069ed91b33ceaa848574219209822eda1b62a4dc00`。
ABI2 固件：`cc2ec4aa976ee93f63ccad86ef0c84b5cb96df75f0db7e3e248df40ce5c1dac7`。
ABI4 固件：`5037cf67e823ef7c332ff1374075a0098804a04ad921da7709afa5e09f9c0bd4`。

STC-Link1D 自动发现，串口 COM13，最后保留 ABI4 测试固件。开发候选已封装，默认冻结核心未替换；主目录用户改动保留。`abi2-capture` 是修复前的定位证据，验收只使用 `abi2-capture-final`、`abi4-capture-final`；原压力集只使用 `user-stress-final`。

```powershell
python -B tools/research/boolean_fixture.py reference --output <new-host> --gcc <gcc.exe>
python -B tools/research/boolean_fixture.py contracts --output <new-contracts> --sdcc <sdcc.exe> --support <toolchain>
python -B tools/research/boolean_fixture.py board --abi 2 --output <new-project>
pwsh -File <candidate>/Build-Optimized.ps1 -ProjectPath <new-project>
python -B tools/research/boolean_fixture.py capture --hex <firmware.hex> --reference <reference.json> --output <new-capture>
```
