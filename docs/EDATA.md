# 显式 EDATA 变量（开发版）

2026-11-09：0.0170 原有低地址平坦访问和扩展栈支持，但没有 C 语言的 EDATA 存储类。此次最小补充为 `__edata`，不是 `__xdata` 的宏别名。

```c
__edata unsigned char buffer[256];
volatile __edata unsigned int count = 7;
static __edata unsigned long state;
extern __edata unsigned char shared[];  /* 定义也必须使用 __edata */

void work(void)
{
    static __edata unsigned int saved;  /* 静态局部变量 */
    unsigned char __edata *p = buffer;  /* 指向 EDATA 的指针 */
    p[255] = 1;
    saved++;
}
```

支持全局和静态变量、数组、结构体、初值、零初始化、跨文件声明、取地址和指针访问。`__edata __at(0x0300)` 可显式定位；绝对对象也会占位并接受链接边界检查。没有初值的绝对对象不自动清零，与既有 `__at` 用法一致。

## 本次范围

- 自动分配段为 `ESEG`，绝对段为 `EABS`，实际地址位于 region 00；MAP 的 `XDATA` 标志是 ASlink 的平坦 RAM 分配类别，不能据此把地址 `0x000100` 误认成 `0x010000`。
- 为复用已有内存/栈契约，窗口限定为 **`[0x0100, stackBase)`**。目前两个正常构建 Profile 的栈起点为 `0x0400`，因此共 **768 字节**，所有翻译单元共享。超出时报错，不静默挤占栈。未开放栈上方内存，也未扩大器件已验证容量。
- 独立 CLI 链接必须提供现有 `--mcs251-stack base,size,edata-size`；正常 `Build.cmd` 已带此参数。改变栈起点前仍需按实际器件容量及栈需求配置。
- 指针复用 MX251 现有 **3 字节平坦指针 ABI**，可以转为 generic/XDATA 指针访问。这里的 `__edata` 指针不会运行时检查地址，也不是 Keil 的 16 位 near 指针 ABI。此次不改变代码生成策略或声称访问更快。
- `--stack-auto` / 重入函数中的非 static 自动对象仍归栈管理；显式限定这类对象会报错。不要把局部变量声明伪装成全局非重入存储。
- 仅 MCS251 接受该关键字；MCS51 不新增该存储空间。`__near` 保持原有含义，未悄悄改写。
- 使用新 `sdcc` 和新 `sdld` 配套。对象引用版本符号 `__mcs251_edata_v1`，旧链接器会报未定义符号，避免悄悄错误放置。

Keil 的对应低 64KB 内存类型在其文档中称为 [near](https://www.keil.com/support/man/docs/c251/c251_le_memtypes.asp)；芯片资料/链接报告的 EDATA 区域名称与 C 关键字不一定相同。

## 验证

`src/mcs251/tests/check-edata.py` 接受完整 `--toolchain`、新的 `--output-dir` 和可选旧 `--baseline`。针对性检查包括：旧版本拒绝声明；新版本两个后端的编译链接；跨 256 字节边界；独立 EDATA/XDATA 地址；启动阶段完整对象清零与初值；静态局部、结构体和跨文件指针；精确容量、累计超容、绝对地址越界/栈冲突；旧链接器拒绝；原 XDATA 示例最终 HEX 不变。

`edata.c` 同时提供先污染 RAM、后检查初值/零值的运行测试入口，但本次结论为**源码构建、编译链接及启动汇编核对**，未运行该测试的实板或模拟器，也不是性能验收。没有修改 SDK 或重建运行库。

本次最终开发工具链通过 35 项编译/链接检查；`examples/edata` 通过正常 `Build.cmd → CMake → Ninja` 入口（默认 Function GC 开启），生成 427 字节代码并取得完整构建凭据。新编译器和链接器必须作为同一开发包使用，不能只覆盖旧包的 EXE 后沿用旧内容锁。
