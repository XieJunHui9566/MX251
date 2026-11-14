# 通用单字节位域后端补齐（2026-11-14）

基于 `e3752a9`，提交 `35f2739` 修复公共优化阶段新插入转换的指令编号，`ee965f7` 加入单字节位域处理和独立测试。编译器优化不包含工程名、SDK 名或器件型号条件；官方源算法、头文件和项目参数保持上一轮输入。

## 实现与覆盖

位域提取、符号扩展和保留相邻位的更新，在导入后展开为已有的字节读取、移位、掩码、按位或和写回节点。后续地址折叠、寄存器分配、成本比较直接处理这些普通节点。覆盖字节内 1～8 位的 signed/unsigned 字段、固定 EDATA/XDATA、已证明的 DATA 地址、栈对象与运行时线性指针；CODE 字段读取复用现有只读路径。部分位域写入保持一次字节读加一次字节写，整字节字段写入不增加读取。

DATA 地址分析同时识别同型指针复制；识别首个普通标量成员时，仅检查该成员的类型，不因结构体后面的位域而排除整个结构体。外部 DATA 对象仍使用原有布局检查。

新压力用例还复现了上一版已有的公共优化错误：运算缩窄插入的转换指令 `seq=0`，下一轮转换合并在已编号基本块中删除它时触发内部错误。现在在该阶段刷新指令编号与活跃范围；没有删除检查或关闭优化。

## 97 项 STC32G 同源回归

全部 97/97 构建成功，4 项缩小、93 项不变、0 项增大。总代码 253393 → **249199 B**，减少 **4194 B**。

| 工程 | 上版 B | 本版 B | 缩小 | 本版 / Keil | 本版 / STC-IDE |
| --- | ---: | ---: | ---: | ---: | ---: |
| g086 | 2825 | 1819 | 35.6% | 117.89% | 57.75% |
| g088 | 2827 | 1822 | 35.6% | 117.93% | 57.69% |
| g090 | 5661 | 4763 | 15.9% | 143.25% | 60.40% |
| g091 | 6234 | 4949 | 20.6% | 142.58% | 60.88% |

| 参照 | 有参考项 | ≤110% | ≤120% |
| --- | ---: | ---: | ---: |
| Keil C251 | 97 | 33 → 33 | 58 → 60 |
| STC-IDE | 79 | 73 → 73 | 77 → 77 |

前两个 CAN 工程进入 Keil 120% 线；后两个仍约 143%。主要收益来自整函数解除回退：g086/g088 的 CanSendMsg 961→448 B，CanReadFifo 806→414 B。g091 的 main 2033→1140 B。具体函数与原因见 JSON；没有针对 CAN 添加特殊生成规则。

Keil/STC-IDE 沿用上一轮相同源输入的参考产物，未重复编译参考工具链。97 项是此前可构建集合，不包含此前源错误、尚未接入的 OMF 及纯汇编工程。官方工程本轮只做编译/体积比较，未运行 CAN 总线等外设场景。

## 独立用户程序与实板结果

- 原六类用户程序、12 种写法的 C/H 源码未修改。主机 3072 次求值及 1536 对等价写法一致。
- 63 个翻译单元全部编译成功；AI8051U、STC32G 和独立架构描述的纯 C REL 逐字节相同。同上一版比较，54 个非位域 REL 也逐字节不变，9 个位域 REL 改变。
- 原压力程序 **768/768** 板上结果与主机参考一致，零计时器溢出。
- 新生成测试覆盖字节内全部 **36 种起始位/宽度组合**；交替使用 char/int 声明、signed/unsigned、volatile/普通访问、EDATA/XDATA/DATA/栈和别名。36 个布局函数全部进入新后端，**2304/2304** 板上结果通过。每个输入内部测试 4 个取值，覆盖 0～255 对应的字段截断和符号范围，并检查相邻字段与前后普通字节。
- ABI2/ABI4 × small/large × size/speed 的 8 组测试，核对实际 MOV 字节事务及顺序、CODE 读取，以及 12 位字段回退时与旧后端 REL 一致。

| 压力函数 | 上版 B | 本版 B | 64 输入计时 tick 总和（上版→本版） |
| --- | ---: | ---: | ---: |
| bits_clean | 491 | 324 | 33046.5 → 21633（约 -34.5%） |
| bits_messy | 661 | 680 | 48986 → 34945（约 -28.7%） |

复杂写法增加 19 B，虽然速度改善，但体积差距尚未解决。其分配试算已有更小候选，当前循环成本比较未选用；后续可继续研究循环内寄存器分配和地址复用。上表计时来自同一 AI8051U 测试框架的每输入重复帧中位数之和，属于本机观测，不是相对 Keil/STC-IDE 的运行时间验收。

## 仍未覆盖的部分

大于一字节或跨字节位域继续走旧后端。动态 `_Bool` 转换仍可能使函数回退；本轮新增样例已记录该现象。回调指针/数组两种压力写法仍回退，96 次展开大函数仍触及值容量。下一步优先补齐回调指针对象语义，局部降级接口继续单独评估。

## 候选与复现

候选入口：`E:/STVAPP/MX251-baseline/work/bitfield-lowering-20261114/final/Build-Optimized.ps1`。
编译器 SHA-256：`1c07ba246aa30bac651c9a6c5d38db32dde7e598557c3175faa2ecdcb46c559f`。
压力固件：`f9e81e3450db0a41559ef2ad4ec00a8d770c78b6e94aebeda740d244c7089a3f`；布局固件：`d469469eee787dcf7a8b27247583c176497b4b00e63b420ebf08890919e31d72`。

这是开发候选，未替换默认冻结核心。原主目录的三个用户改动保留。板上最后留下新的压力测试固件，未恢复旧固件。STC-Link1D 自动发现，串口 COM13。

```powershell
python -B core/sources/compiler/src/mcs251/tests/check-byte-bitfields.py --sdcc <sdcc.exe> --support <toolchain> --output <new-dir>
python -B tools/research/bitfield_fixture.py reference --gcc <gcc.exe> --output <new-reference>
python -B tools/research/bitfield_fixture.py board --output <new-project>
pwsh -File <candidate>/Build-Optimized.ps1 -ProjectPath <new-project>
python -B tools/research/bitfield_fixture.py capture --hex <firmware.hex> --reference <reference.json> --output <new-capture>
python -B tools/research/common_device_matrix.py --root <new-staged-candidate> --parent <pointer-cache-cost-20261114/final> --jobs 4
```

矩阵复用已有 result.json；重跑须先用本轮 stage.py 在新目录封装对应编译器。原始日志、HEX、汇编、串口数据和逐项回执保存在候选目录；同名 JSON 固化本轮汇总和证据指纹。
