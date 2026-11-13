# 当前 SDK 对照范围：75 项

2026-11-13，统一编译器 `pointer-canonical-transport/core-01`，实现 `2cef560`。

- 体积 ≤Keil 120%：**75/75**。
- 体积 ≤Keil 110%：**66/75**。
- MX251 / Keil 合计 **191247 / 192049 B（99.58%）**。
- 相对上一统一版，32 项缩小、43 项相同、0 项变大，净减少 **369 B**。
- 剩余 9 项到 110% 的逐项整数缺口合计 **365 B**。

本轮补齐三字节指针装载和已知对象游标的信息传递，p074 新过 110% 线，详见 [本轮结果](POINTER-CANONICAL-TRANSPORT.md)。父版和新版各 128/128 项实板测试通过；完整工程对 Keil 的运行时间仍未验收。

## 输入与参考

74 项保持原冻结输入。p108 使用双方一致的既有单行向量修正，见 [p108 记录](P108-VECTOR-REPAIR.md)。Keil/STC-IDE 复用同源参考。STC 为 59 项 HEX、13 项不支持汇编输入、3 项失败，只作原始体积参考。此前 [CY 兼容性修复](CARRY-OBSERVATION.md)、[下标地址优化](INDEXED-ADDRESS-GENERATION.md) 与 [调用保存选择](CALLEE-PRESERVATION-CHOICE.md) 均包含于本版。

## 剩余 110% 项目

| 工程 | MX251/B | Keil/B | 尚差/B |
| --- | ---: | ---: | ---: |
| p136 | 4668 | 4128 | 128 |
| p045 | 974 | 832 | 59 |
| p049 | 1160 | 1002 | 58 |
| p039 | 1257 | 1105 | 42 |
| p133 | 468 | 404 | 24 |
| p149 | 485 | 423 | 20 |
| p047 | 192 | 160 | 16 |
| p032 | 785 | 704 | 11 |
| p015 | 306 | 272 | 7 |

## 后续使用

入口：`E:/STVAPP/MX251-mainline-20261113/pointer-canonical-transport/Build-Optimized-01.ps1`。

配置：同目录 `candidate-01.json`，保留全部 75 项与 p108 显式输入替换。完整产物 `matrix-01`；后续使用新输出目录，以 `comparison.json` 为对比基线。

[结构化名单](active-sdk-cohort.json) 与 [本轮证据](pointer-canonical-transport-evidence.json) 绑定编译器、配置、报告及测试。
