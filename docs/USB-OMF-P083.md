# USB OMF 小对象接入与 p083 体积拆分

2026-11-14；分支 `codex/usb-omf-format-runtime`。原始产物位于
`E:/STVAPP/MX251-baseline/development/usb-omf-format`，开发包为其下 `core-next`。
本轮没有更改编译器二进制，也没有替换冻结基线。

## p083 的确定结果

| 输入与运行库 | MX251 HEX 有效字节 | Keil 同输入 | MX/Keil |
|---|---:|---:|---:|
| 上轮原始格式串、软件整数库 | 9331 | 1457 | 640.43% |
| 格式串勘误、软件整数库 | 3169 | 1459 | 217.20% |
| 格式串勘误、原生 MDU32 整数库 | **2994** | **1459** | **205.21%** |

两个 `unsigned long` 被原例程分别用 `%d`、`%u` 打印，实际推入四字节，
格式只消费两字节。研究构建器在原生与 MX251 对照副本中同样改为 `%lu`，
保存前后哈希；原始 SDK 未改。宽度检查继续保留，没有让编译器接受错配参数。

由此恢复已有的格式库裁剪；生成器不含工程名判断。完整 printf 替换本身节省
6155 B，此外格式串和启动初始化共同导致最终从 9331 B 降到 3169 B。
MDU 再减少 175 B，合计减少 6337 B（67.91%）。这不是 6337 B 的新后端优化：
其中主要收益来自修复输入后启用已有能力。

通用改动有两项：空字符串直接按零长度解码，消除 PowerShell 空数组变成 null
导致的异常；普通 ABI 的 MDU runtime 现在也可以使用闭合格式库。研究适配器按
输入库能力选择运行库：只含 MDU32 库时使用 `native-int-mdu32`，不再借用浮点配置。

纠正上一份记录中的推断：原来的 `_mullong.rel`、`_divslong.rel`、`_divulong.rel`
来自 **libnativeint**，实际入口为 `___m251r4__*`，已经是原生 ABI。
对象文件名不能据此判断为传统 long 实现。

### 剩余开销

最终 2994 B 可以按对象和段完整拆分：

| 部分 | 字节 |
|---|---:|
| 应用代码（其中 main 1998 B） | 2057 |
| 精简 printf 与 32 位数字转换 | 681 |
| MDU 整数库 | 107 |
| CODE 常量 | 114 |
| 向量、启动与初始化 | 35 |

`main` 在第一次 EDATA 全局标量赋值处被 wide MIR 拒绝：
`assignment-cast-or-memory-contract`。`wide_symbol_snapshot` 目前只把直接 RAM/SFR
真符号接成 `WM_SYMBOL_*`；EDATA 不属于该空间。volatile 标量保持真符号，因而整函数
回到旧后端，反复发出 DPTR/DPXL 设置和逐字节 MOVX。

下一轮应先补齐 **EDATA 真符号的独立内存节点和发射路径**，再处理直接作为 ABI4
算术实参时的取样路径。现有 `VolatileDataWide` 只作用于 DATA，不能直接拿来放宽
EDATA。需要保持 volatile 访问次数和顺序，同时利用 EDATA 的原生寻址减少地址搬运。
这比继续压缩仅 107 B 的 MDU 库更有空间，适用于同类工程。

另做了去掉 volatile 的诊断副本，2028 B；该副本改变了 C 语义并允许常量传播，
只保留为定位材料，**不是候选版本或可兑现的节省量**。

### 验证与出处

- 软件路径：`p083-fixed/projects/p083/mx251`；同输入 Keil：该目录的兄弟 `keil`。
- 最终研究适配器重建：`final-matrix/projects/p083/mx251`，2994 B；与独立 MDU
  试验映像 SHA256 相同：`72d532ac456b69faefd62ca2f2316bdb898bbbfd80ce899fc3caab7c84a031a4`。
- `check_variadic_format.ps1` 通过空字符串、Latin-1、MDU 需求解析和原有 ABI/宽度检查。
- `integer_format_mdu_fixture.py` 在 STC-Link1D 自动发现的 COM13 上 **128 组通过**，
  覆盖 MDU 乘除法、混合 `%lu/%ld/%08lx`、返回长度和空字符串；功能测试未测时间。
  固件 1854 B，SHA256 `2dc3095e53c673f9e1c5b26d4f6fdb52d67f874421cad55d6528767ac5143efb`。
- 未逐个上板运行完整 p083 或全部 SDK；覆盖数量仍为 106/114，未新增过体积线项目。

## USB OMF 已完成的接入层

[Arm 的 OMF251 答复](https://community.arm.com/forums/f/keil-forum/53525/omf251-specification/178710)
说明该格式没有公开规范。本轮以本机 C251 5.60、A251 4.69、L251 4.66 生成的可控
对象为依据，通过重新布局后与 L251 链接结果逐字节比较，确定实际需要的字段。

新增 `core/compat/keil/omf251.py`：解析记录校验、模块、段、public/external、数据、
代码重定位与单独的调试重定位；把已验证的子集生成为 SDAS251 汇编，再生成真正的 REL。
保留段和符号引用，不把一个固定地址 HEX 冒充可重定位库。

目前支持 CODE、DATA、EDATA、XDATA 的普通可重定位段；低/高字节、16/24 位地址、
far 指针高字、同 bank LCALL/LJMP、同段相对跳转。各段独立分配 area，public 使用
`__omf_` 加名称的 ASCII 十六进制编码。外部 C ABI 名称没有自动混为 MX251 C 符号。

`check_omf251.py` 使用 5 个模块、9 个段、23 处重定位，在 **两套不同布局中各 74 B
完全匹配 L251**，同时验证错误校验和、未知重定位不能被输出。结果在 `checks-omf-04`。
这是主机对象/链接验证，尚无 USB 枚举或传输的实机结论。

### 完整库尚缺的内容

| 库 | 已解析 public | 代码/数据重定位条目 | 未实现的位重定位 06 | 未实现的 MOV DR 立即数重定位 16 |
|---|---:|---:|---:|---:|
| HID | 106 | 696 | 6 | 2 |
| CDC | 105 | 721 | 5 | 3 |
| CDC32G | 105 | 699 | 5 | 3 |

三个库都还有固定地址 0xCB 向量、BIT 段和特殊 public；CDC 两类另有 class 12。
这些形式目前明确列在 inventory 的 `unsupported` 中，完整库不能直接导出。
0x16 会涉及指令编码字段，不能当普通 16 位地址覆盖。原始记录和字段在 `usb-inventory`。

后续顺序：

1. 用 A251 小对象核对上述位、固定向量、特殊类和指令重定位，补齐完整库输出。
2. 将段布局接入 MX251 构建，处理段级回收和启动初始化。
3. 逐个实现 C251 参数、返回值、寄存器保存、重入、ISR 与反向回调桥。
   `memcpy/strlen/strcmp/vsprintf`、静态参数区、`?C?LMUL` 等依赖必须按各自约定接入。
4. 再跑 HID/CDC 的真实枚举与收发，随后重编八个 USB 例程。

本轮是小对象导入阶段通过；八个 USB 工程仍未计入可编译覆盖数。
