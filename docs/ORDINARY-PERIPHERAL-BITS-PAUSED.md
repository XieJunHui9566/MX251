# 普通函数外设位优化：迁移暂停记录

已于 2026-11-10 恢复并完成本轮工作，最新版本与结果见 [普通函数外设位优化](ORDINARY-PERIPHERAL-BITS.md)。以下保留当时暂停记录。

2026-11-10 用户要求暂停迁移。本轮处于候选阶段，尚未运行完整 75 工程及实板验证。当前推荐版本仍是 `E:/STVAPP/MX251-mainline-20261110/native-code/core-02`：75/75 成功，体积 ≤Keil 120% 为 64 项，≤110% 为 49 项；完整工程运行时间未测。

## Git 与目录

- 工作树：`C:/Users/jav/.codex/worktrees/stclink1d-feasibility/MX251`。
- 分支：`codex/ordinary-peripheral-bits`，父提交 `eee789a`。
- 首版实现：`6b4eda01bf95fb1bc1599bea0842222a0304bff9`；后续修正与夹具见该分支提交记录。
- 所有本轮核心、构建配置、日志及测试结果：`E:/STVAPP/MX251-mainline-20261110/ordinary-bits`，下文简称 N。
- 主目录 `E:/STVAPP/MX251` 的用户改动保持原样：`core/build/qualified/Build.ps1`、`BuildLatest.ps1` 和未跟踪 `VerifiedTools.ps1`。迁移时一并携带，不可用本工作树覆盖这些文件。

## 已完成

1. 共用有序内联汇编解析器，识别 NOP、数字 byte.bit 形式 CLR/SETB/CPL；外设访问顺序保持不变。
2. 机器指令效果描述识别已知外设位操作：保留内存读写效果，允许 GPR 副本跨操作复用。普通旧后端也可接入机器优化。
3. PSW/PSW1 的 PUSH/POP 作为局部效果边界；实际寄存器 bank 修改仍不进入机器优化。
4. AI8051U 目标声明扩展 S2CON、IE2、SPSTAT、SPCTL 的位能力。**`0xaf.7` 是 IE2.EUSB，不是经典 sbit 地址 0xaf 所表示的 EA**。
5. 首次试编译暴露 ABI4 调用使整函数成本比较跳过的问题。修正为含已验证有序位操作的普通函数，也比较含 ABI4 直接调用的两种完整实现。

修改只涉及 MX251 后端、目标能力、测试与研究工具，没有改 SDK 源码，也没有按工程/函数名选优化路径。

## 已得到的结果

首版 `N/core-01` 编译器 SHA-256：`f0ef9b430217e6130f51d659b6a75d5576b5a04b1707c749139e0795652837c7`。

| 工程 | 父版/B | 首版/B | 代表函数变化 |
| --- | ---: | ---: | --- |
| p095 | 6109 | 6323 | `_uart_polling` 534 → 736 B，未比较旧后端 |
| p096 | 3994 | 3905 | `_usb_class_in` 566 → 465 B |
| p136 | 4975 | 4975 | `_SPI_Config` 295 → 295 B，选旧后端 |

首版不能作为推荐版本。以上为真实构建结果，详见 `N/pilot-results.json` 和 `N/matrix-core-01`。

修正版 `N/core-02` 编译器 SHA-256：`021add97475b3e80db21e15a29d2f183b6a838e454400856e5a3c52efd8163f6`。已经编译和冻结，**尚未重编三个代表工程**，所以不能声称已消除 p095 增长。

完成的检查：

- 机器层 189 组程序、12096 次执行通过，包含位操作顺序、重复外设写保留、寄存器副本复用；`N/machine-checks`。
- small/large × speed/size 四种配置通过：数字位地址、volatile 访问顺序、能力开关、核心状态/未知指令回退，以及 ABI4 调用进入完整函数比较；`N/compiler-checks-final`。
- 中间 `N/compiler-checks-02` 的测试把 volatile 对象直接传入 ABI4，命中了既有 `argument-snapshot-abi4-contract` 限制。最终测试改用已求值的局部参数，专门检查本轮成本选择；没有更改该 ABI 限制。
- 实板夹具脚本 `tools/research/ordinary_bits_fixture.py` 已保存，源工程已生成在 `N/fixture`。它覆盖 IE2、UART2、SPI 配置位读回、活跃标量、volatile 数据及 ABI4 调用，Python 独立计算结果。**夹具尚未构建或下载，本轮未操作硬件。**

## 迁移后接着做

1. 使用冻结 `N/core-02.json` 只重编 p095、p096、p136，检查整函数选择与项目总字节。`N/pilot.py` 当前指向首版 core-01；分析修正版时改为 core-02 并另存结果，保留首版证据。
2. 确认有净收益后，同一配置运行其余 75 项（工具会跳过已完成项目）；不要混用各工程历史最小版本。Keil/STC 参考沿用父版相同输入哈希。
3. 用父版与修正版分别构建并运行 `ordinary_bits_fixture.py` 实板夹具；STC-Link1D 自动寻找 USB/串口，用户已授权下载且无需恢复旧固件。
4. 汇总最终体积和代表函数差异、实板结果，再更新达线统计与推荐版本。

```powershell
E:/MCU/python/python.exe -X utf8 tools/research/unified_sdk_matrix.py --config E:/STVAPP/MX251-mainline-20261110/ordinary-bits/core-02.json --ids p095,p096,p136 --suites mx251 --jobs 3
```

可复用 `N/prepare.py`、`N/freeze.py` 生成后续编号；不覆盖已经冻结的 core-01/core-02。宿主构建目录为 `E:/STVAPP/MX251-mainline-20261109/copy-affinity/host/build`，CMake 为 `E:/STVAPP/MX251-edata-dev/host/cmake/bin/cmake.exe`。后续换盘符需同步配置中的绝对路径。
