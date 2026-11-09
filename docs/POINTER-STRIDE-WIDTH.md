# 解引用地址表达式的整数宽度修正

2026-11-09，从 `c169c5b` 接续。比较 Keil 与 STC-IDE 的寻址代码时，在 TFT 位图读取路径发现前端错误：字节读取的结果类型会沿一元 `*` 传入地址计算，把原本的 16 位索引乘法缩成 8 位。修复与 SDK 无关，改动公共前端四行逻辑。

```c
unsigned char read(const unsigned char *p, int i)
{
    unsigned char value = *(p + i * 2);
    return value;
}
```

父版的原始 IR 已经是 `int → signed char → 左移一位 → 指针偏移`，不是寄存器分配造成。`i=64` 时偏移 128 被符号扩展为 -128；`i=128` 时偏移变成 0。Keil 对照保留 16 位索引运算。

`SDCCast.c:resultTypePropagate()` 现在对一元解引用返回 `RESULT_TYPE_NONE`，二元乘法沿用原规则。指针内部表达式按自身类型计算，读取结果再转换为目标类型。源码显式的 `(unsigned char)(i*2)` 仍保留 8 位取模语义。

## 检查与实板

- `check-pointer-stride.py`：small/large × speed/size 四组通过。覆盖有符号、无符号、嵌套运算、数组下标、volatile、字指针、写入和显式窄化。父版运行同一检查，在有符号索引乘法处失败。
- 独立夹具使用 4096 字节 XDATA 数组和 1024 元素字数组，跨编译单元调用。64 个输入覆盖正负偏移、128/256 字节边界、最大 3198 字节偏移、无符号 16 位回绕及写后读取；另有 64 个既有控制用例。
- AI8051U-34K64，STC-Link1D 自动定位并下载；串口共收到 658 帧，128 项结果全部与 Python 独立计算一致。此轮用于正确性，不比较运行时间。

四个完整工程沿用父版输入哈希、工程快照和选项：

| 工程 | 父版 / B | 修复后 / B | 变化 / B |
| --- | ---: | ---: | ---: |
| p080 TFT | 20481 | 20485 | +4 |
| p087 USB CDC | 4581 | 4581 | 0 |
| p091 USB MSC | 5651 | 5651 | 0 |
| p095 双 CDC | 6416 | 6416 | 0 |

增加的四字节恢复正确的地址计算，不计作优化收益。完整工程没有新增相对 Keil 的运行时间记录。

## 复现

```powershell
python core/sources/compiler/src/mcs251/tests/check-pointer-stride.py --sdcc <sdcc.exe> --support <toolchain目录> --output <新目录>
python tools/research/pointer_stride_fixture.py create --output <新工程目录>
# 用正常 Build-CMake.ps1 构建工程，启用 MachineIr、CalleePreservation。
python tools/research/pointer_stride_fixture.py capture --hex <firmware.hex> --output <新记录目录>
```

本机研究目录为 `E:/STVAPP/MX251-mainline-20261109/stcide-deep-review`，开发核心 `core-01`，完整工程 `core-01-p080/p087/p091/p095`；硬件工程 `stride-fixed`，捕获 `stride-fixed-capture`。源码、镜像及输入指纹见 [证据](pointer-stride-width-evidence.json)。

该修正没有处理宽后端尚不接受的三字节整数指针偏移。TFT 位图函数仍可因 `_BitInt(24)` 转换退回旧后端；这是后续寻址优化的独立问题。
