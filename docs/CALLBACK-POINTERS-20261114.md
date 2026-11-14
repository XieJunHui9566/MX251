# 通用回调指针与回调数组（2026-11-14）

基于 `e0b050a`，实现和测试提交 `28d0c34`。本轮补齐通用 C 语义，不包含器件名、SDK 名、官方函数名或工程号条件。

## 编译能力

函数地址、函数指针变量和三字节指针对象接入宽 MIR；局部数组、DATA/EDATA/XDATA 数组、CODE 只读表以及结构体中的回调均使用现有存取路径。物理对象仍为三字节大端布局，MIR 使用零扩展的四字节值，读写不触及第四个字节。函数地址重定位与数据读取空间分别表达。

ABI2 和 ABI4 的间接调用复用直接调用的参数布局、栈清理、活跃值保存及结果接收。回调目标在参数搬运前取快照，再从保留的 DR20 发起 ECALL，避免参数寄存器覆盖目标。目标作为 MIR 操作数参与活跃分析和已有复制传播。未知目标按完整调用破坏集合保存活值，不将间接调用记成虚构的直接调用。新的间接调用审计前缀与直接目标反馈分开；原有函数元数据继续记录 indirect=1。

回调还可以作为寄存器参数、栈参数和返回值；ABI4 类型检查接纳三字节函数指针。非可重入、特殊寄存器组/调用约定等原来不支持的调用继续使用旧后端。本轮没有新增间接尾调用优化，也没有将整个编译器改成另一套 ABI。

## 独立用户程序收益

原压力程序的 C/H 源码完全不变。

| 函数 | 上版 | 本版 | 体积变化 | 同板 64 输入 tick 总和 |
| --- | ---: | ---: | ---: | ---: |
| callback_clean | 145 B | **113 B** | **−22.1%** | 14315 → 11866.5（−17.1%） |
| callback_messy | 424 B | **255 B** | **−39.9%** | 19915 → 14085（−29.3%） |

两个函数均进入宽后端。压力固件共减少 201 B。其余五类压力程序的 54 个 REL 与上一版逐字节一致；9 个回调 REL 改变。63 个翻译单元全部成功，三种目标描述（AI8051U、STC32G、独立架构描述）的纯 C REL 逐字节相同。主机 3072 次求值、1536 对等价写法一致。

这套压力集目前仅剩 `giant_messy` 的 `value-capacity` 回退。动态 `_Bool`、跨字节位域及其他未覆盖基础语义不在该结论内。计时是同一 AI8051U 板和测试框架的观测，不是相对 Keil/STC-IDE 的运行时间验收。

## 板上验证

- 原六类用户程序：**768/768 PASS**，无计时器溢出。
- 新回调测试：19 种语义等价写法 × 64 输入。ABI2 **1216/1216 PASS**，ABI4 **1216/1216 PASS**。
- 新测试覆盖局部指针、局部表、全局 volatile 指针、三类 RAM 数组、CODE 表、结构体前后字节、第一/第二/第三参数、返回指针，以及旧后端与宽后端的双向调用。
- ABI2/ABI4 × small/large × size/speed 的 32 个翻译单元全部成功；其中 24 个启用宽后端，8 个刻意使用旧后端提供互操作参照。

共 **3200/3200** 板上结果与主机参考一致。STC-Link1D 自动发现，串口 COM13；最后保留新的原压力测试固件。

## 官方工程回归

STC32G 既有可构建集合 **97/97** 全部成功：1 项缩小、96 项不变、0 项增大。总代码 249199 → 249132 B。

| 参照 | 有参考项 | ≤110% | ≤120% |
| --- | ---: | ---: | ---: |
| Keil C251 | 97 | 33 | 60 |
| STC-IDE | 79 | 73 | 77 |

缩小的是 g124「75-通过定时器周期性调度任务综合例程」：2970 → 2903 B。`Task_Pro_Handler_Callback` 从 170 → 101 B；`Sample_NTC` 从 184 → 186 B，项目净减少 67 B。官方调度回调也受益，未新增对应函数名规则。

Keil/STC-IDE 沿用相同源输入的既有参考产物；本轮没有重复编译参考工具链。97 项仍是此前可构建集合，不包含源错误、未接入的 OMF 和纯汇编工程。本轮官方例程只做编译与体积回归；硬件执行证据来自独立回调程序及原压力程序。

## 候选与复现

入口：`E:/STVAPP/MX251-baseline/work/callback-pointers-20261114/final/Build-Optimized.ps1`。
编译器 SHA-256：`3f3a35a10aa26354c8cb111ada89ea195843823da69884a72393a27444684917`。
压力固件：`68ecfb8cc8130d14545b5d36d80b55b5ead222bafe54e18b55364384a7d0d38f`。
ABI2 固件：`f041e6ffb07687e2a067de295e57143880092cf666a9af0b4748a91a28a02192`。
ABI4 固件：`7a9f729b32294d63807e6fbe67a7250c406a6b5e00981bd6a72defe0a196bdbc`。

开发候选已封装，默认冻结核心未替换。主目录原有用户改动保留。报告 JSON 记录源码提交、工具指纹、逐项结果和 UART 证据。

```powershell
python -B tools/research/callback_fixture.py contracts --sdcc <sdcc.exe> --support <toolchain> --output <new-dir>
python -B tools/research/callback_fixture.py reference --gcc <gcc.exe> --output <reference-dir>
python -B tools/research/callback_fixture.py board --abi 2 --output <abi2-project>
python -B tools/research/callback_fixture.py board --abi 4 --output <abi4-project>
pwsh -File <candidate>/Build-Optimized.ps1 -ProjectPath <project>
python -B tools/research/callback_fixture.py capture --hex <firmware.hex> --reference <reference.json> --output <capture-dir>
python -B tools/research/common_device_matrix.py --root <staged-candidate> --parent <bitfield-lowering-20261114/final> --jobs 4
```

矩阵会复用已有 result.json；新版本测试需使用新目录。封装脚本和所有原始产物保存在 `callback-pointers-20261114` 工作目录，`final` 是本轮选定版本。
