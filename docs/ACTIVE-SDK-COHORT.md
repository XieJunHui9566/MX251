# 当前 SDK 对照范围：74 项

2026-11-13，按用户要求暂排除 **p108**。当前优化统计与后续默认验证采用其余 74 项；p108 的绝对汇编入口问题另行处理，历史 75 项报告与研究快照保留。

复用已验收的 `legacy-jump-tables/core-02` 及其现有构建结果：

- 体积 ≤Keil 110%：**61/74**。
- 体积 ≤Keil 120%：**72/74**；尚未达到的是 **p045、p047**。
- MX251 / Keil 合计 **191347 / 190617 B（100.38%）**。
- 剩余 13 项进入 110% 的代码字节缺口合计 **739 B**。

本次仅改变统计与验证范围，未改编译器、SDK 或固件，未重新编译。报告相对当前已验收版本为 74 项相同、0 B 新收益；完整工程运行时间仍未测。

## 后续使用

当前逐项报告：`E:/STVAPP/MX251-mainline-20261113/optimization-research/active74/comparison.json`，同目录有 Markdown 和 HTML。

后续构建以同目录 `core-02.json` 为配置模板，保留 `"excludeIds": ["p108"]`，按候选版本更新 `core`、`output` 和相应工具路径。`unified_sdk_matrix.py --ids all` 将只准备和构建这 74 项。原 75 项配置未修改，仍可复现历史结果。

后续比较以当前 74 项 `comparison.json` 为 baseline。`report_compiler_delta.py` 自动沿用 baseline 与候选配置的排除项，分母按实际名单计算；对历史 75 项结果生成当前视图时使用 `--exclude-ids p108`。

[结构化名单与证据](active-sdk-cohort.json) 包含全部 74 个 ID、结果与配置哈希、剩余 13 项差额，以及 p108 的历史结果。
