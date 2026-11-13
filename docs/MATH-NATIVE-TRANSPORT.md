# 通用位型传递与基础数学库

本轮实现研究报告中的 A+B，分支 `codex/math-native-transport`，父提交 `b185c1d`。以 `baseline-20261113` 的冻结编译器和数学库为比较起点；SDK、Math123 输入源码未修改。

## 收益

完整 Math123：**11139 → 10339 字节，减少 800 字节，7.18%**。两次构建的输入文件哈希一致，使用同样的 ordinary-wide、函数共享、Machine IR 和调用保存选项。新增库调用信息启用后的结果仍为 10339 字节。

九个普通 C 数学包装的汇编体积合计 **1049 → 291 字节**。二元包装每个 141 → 39，局部帧 12 → 0；一元包装每个 97 → 27，局部帧 8 → 0。这是链接共享和近跳转处理前的函数口径，不与完整映像的 800 字节重复相加。

| 函数 | 原本体 | 新本体 | 原数学库依赖合计 | 新数学库依赖合计 |
|---|---:|---:|---:|---:|
| fabsf | 100 | 5 | 100 | 5 |
| frexpf | 480 | 254 | 480，加通用指针写辅助 | 254 |
| ldexpf | 1002 | 448 | 1002，另有 errno | 448，另有 errno |
| floorf | 186 | 288 | 665 | 288 |
| ceilf | 158 | 294 | 637 | 294 |
| modff | 128 | 463 | 971，加通用指针写辅助 | 463 |

表中依赖合计是运行库成员的静态闭包，不含启动、调用方、链接布局；多个函数共同使用时不能直接相加。floor/ceil/modf 本体变大，但消除了转换和浮点减法依赖。六个本体合计 2054 → 1752；全部 66 个成员的 CODE+CONST 合计 15795 → 15394。完整归档总量包含未被应用选中的成员。

## 实现

- 将完整、非 volatile、地址未逃逸的 4 字节局部 union 识别接入正常宽后端，不再依赖实验环境变量。
- 在现有局部对象提升和成本比较中，把这类 union 转成原始 32 位虚拟值。位复制与数值转换分开，保留 NaN 位型、负零以及每个使用点的有符号/浮点类型。
- 定义级复制传播接纳原始位复制；重新定义和控制流汇合继续使用原有数据流规则。真 volatile、部分字段和逃逸对象保留内存访问。
- fabs/frexp/ldexp/floor/ceil/modf 使用 ordinary-wide 构建，公开 ABI 不变。floor/ceil/modf 按指数和尾数拆分，不再把输入先强制转换成 long；两份运行库源快照同步更新。
- 构建器生成与归档哈希绑定的 `libnativefloat.effects.json`，记录六个函数的寄存器、特殊寄存器、内存和完整性摘要。当前只有 fabs/floor/ceil 的摘要完整，可提供寄存器保存信息；另外三项保留未知状态。正常 native-math 构建消费已被 profile.lock 固定的摘要，并复用已有调用闭包分析。
- 修正 sqrt_domain 配方对等价 `(_errno+0)` 地址拼写的处理，使 66 个成员完整构建。

编译器没有 SDK 路径、项目名或数学包装函数名特判。4 字节 union 的结构判定对普通 C 同样适用。

## 验证

- floor/ceil/modf 的生产源码在宿主执行 **3010752 次位级结果比较**：全部指数、两种符号、尾数边界及一百万个伪随机输入。参照宿主 libm；对部分 Windows libm 丢失的 modf 零符号，按 C 规范另行检查。NaN 按当前库的位型保留约定比较。[WG14 C23 草案，modf 与 F.10.3.12](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3096.pdf)。
- small/large × speed/size 四种配置：局部对象、跨调用、可变 union、循环、真正 volatile、部分字段、取址修改均编译通过；既有 value-storage 回归通过。
- 原普通栈浮点回归 12 组配置/边界通过，包含不支持场景回退、volatile 四次字节事务与取址对象保留。测试中旧的“必须仍有 spill”和“union 必须字节不变”断言随本轮能力更新。
- STC-Link1D 自动发现端口后下载至 AI8051U。最终启用调用摘要的固件 **128 组、518 帧通过**，覆盖位型传递、六项数学函数、负零、非正规数、NaN/Inf、大数和正负边界。
- Math123 正常 CMake/Ninja 构建通过，调用保存分析结果为 `FINAL_ACTUAL_CALL_PRESERVATION_VERIFIED`。

板测采用独立编译器夹具和 AI8051U 的启动/内存布局，未扩大已发布 profile 的运行库支持列表。本轮 Math123 的 10339 字节是构建结果，未在 STC32G 上重测完整 Math123；没有新增与 Keil 同条件的耗时比值。75 项冻结基线也未重写。

## 产物与继续方向

可用开发包：`E:/STVAPP/MX251-baseline/development/math-native-transport/core`。完整证据在同级目录，精简账本为 [MATH-NATIVE-TRANSPORT.json](MATH-NATIVE-TRANSPORT.json)。公开版本仍为 `0.0170-test`。

复现入口：

- `core/sources/native-float-builder/BuildNativeFloat.py`：构建归档及调用摘要。
- `core/sources/native-float-builder/check-basic-math.py`：宿主数值核对。
- `tools/research/math_transport_fixture.py create/build/capture`：独立跨文件和数学板测；`build --effects` 启用新摘要。
- 开发包 `build/Build-CMake.ps1`：正常工程构建。

下一处主要收益仍在共享多项式调用和三角约简，尤其 cos；不继续逐条压缩这些包装。基础函数中的跨分支 union 地址仍有可优化空间，但不是本轮完成大收益后继续扩展验证的理由。pow 负底数、fmod 缺项和完整 libm 异常约定仍按研究报告另列。
