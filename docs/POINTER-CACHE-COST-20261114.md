# 通用 DPTR 地址事实修复与 EDATA 后端择优（2026-11-14）

基于 `9094a21` 的架构/器件分离候选，完成两项通用修复。没有修改 SDK、官方例程算法或器件能力条件。代码分别提交为 `e666953`（地址缓存）与 `caa9e48`（EDATA 成本）。

## 正确性修复

旧后端在运行时指针装入 DPTR 后仍保留固定地址事实，导致后续位域成员读取省掉必要的地址重载。运行时装载、远地址对象物化和指针递减现在同步清除该事实；有效的字面量地址复用继续保留。

原压力测试源码保持不变。原先 bits_messy 的 64/64 个输入错误，修复后完整 768/768 组通过。加入成本修复后的最终固件也通过 768/768 组，零不匹配、零计时器溢出。AI8051U 实板通过 STC-Link1D 自动发现后下载，未恢复旧固件。

修复后的 bits_messy 为 661 B（原错误版本 649 B），新增 12 B 用于必要地址生成；bits_clean 保持 491 B。最终 ring_messy 为 277 B（上一版 279 B）。

## EDATA 成本与择优

宽后端已经生成 EDATA 读写指令，但整个函数的成本比较只解析 DATA/SFR/BIT 符号；EDATA 指令成为 unknown，阻止选用较小的旧后端。现在同时识别 EDATA/EABS 类型、显式 e: 操作数及对象内偏移，复用既有四字节指令计价。

新增独立编译测试覆盖 ABI2/ABI4 × small/large × size/speed，共 8 组；覆盖数组成员、绝对地址、extern 变量、实际汇编字节长度和后端选择。既有整函数择优测试也通过：27 个偏移表达式、4 组编译配置及原有循环策略。

## 同源 STC32G 工程对比

重编译 97/97 个可构建工程；38 项变小、58 项相同、1 项变大。总代码 254055 → 253393 B，减少 662 B。所有构建回执核对同一最终编译器 SHA-256。

| 参照 | 有参考的工程 | ≤110% | ≤120% |
| --- | ---: | ---: | ---: |
| Keil C251 | 97 | 33 → 33 | 57 → 58 |
| STC-IDE | 79 | 72 → 73 | 75 → 77 |

| 上轮增长项 | 上轮 B | 本轮 B |
| --- | ---: | ---: |
| g048 | 1324 | 1177 |
| g050 | 1274 | 1135 |
| g062 | 3076 | 3064 |
| g063 | 3049 | 3038 |
| g079 | 1004 | 944 |
| g129 | 561 | 540 |

新增/丢失门槛与完整逐工程数据见同名 JSON。Keil/STC-IDE 使用上一轮相同源输入的参考产物；没有重复编译未改变的参考编译器。此前原始源错误、OMF 未支持及纯汇编工程不在这 97 项内。

g066 的 Xdata_Test 从 93 B 增至 95 B：编译器阶段比较为 wide 107 B、legacy 103 B，后续尾调用/转移缩短使最终 wide 版本反而小 2 B。说明当前择优成本尚未覆盖后续布局收益；本轮如实保留此增长，没有增加项目例外。

## 独立用户程序压力集

六类规整/复杂写法保留：结构体、状态机、环形缓冲区、位域、回调、大函数展开。主机 3072 次求值、1536 对等价结果一致；63 个翻译单元编译成功。同配置下，AI8051U、STC32G 与独立架构描述生成的纯 C REL 文件逐字节相同。

位域两函数和回调两函数仍因语义覆盖不足回退旧后端；大展开函数仍达到值容量上限。本轮修复了实际错误与成本漏选，没有把这些回退记录删除或当作已补齐。下一步优先补齐通用位域提取/更新，再处理回调指针对象语义；局部降级接口另行评估。

本轮上板验证针对 AI8051U 压力集。没有测量完整工程相对 Keil/STC-IDE 的运行时间，不能据此宣布最终 110% 目标达成。

## 产物与复现

- 候选入口：`E:/STVAPP/MX251-baseline/work/pointer-cache-cost-20261114/final/Build-Optimized.ps1`
- 编译器 SHA-256：`a9c2379d2f1030cf417fa402e53400981bc1d67e0e6d47cee43f4126fa6d70fb`
- 最终压力固件 SHA-256：`f5b13b03f03b132c31c842468e34752478d0bca923dc146abedc73c016758f4b`
- 工作树：`C:/Users/jav/.codex/worktrees/stclink1d-feasibility/MX251`。候选未安装为默认核；主目录既有未提交改动保持原样。
- `final/stage.py` 记录候选打包过程和源码变更清单；完整编译器源码对应上述 Git 提交。原始构建日志、汇编、HEX、串口帧和 JSON 回执位于候选目录。

矩阵工具会复用已有 `result.json`。重新构建需先在新目录按 `stage.py` 制作同版本候选，再运行矩阵；仅更换 `--root` 会从父核心复制旧编译器。

```powershell
python -B tools/research/common_device_matrix.py --root <final> --parent <common-device-semantics-20261114>
python -B core/sources/compiler/src/mcs251/tests/check-edata-backend-choice.py --sdcc <sdcc.exe> --baseline <parent-sdcc.exe> --support <toolchain> --output <new-test-dir>
python -B tools/research/user_stress.py measure --core <candidate/core> --gcc <gcc.exe> --output <new-measure-dir>
python -B tools/research/user_stress.py board --output <new-board-project>
pwsh -File <final>/Build-Optimized.ps1 -ProjectPath <new-board-project>
python -B tools/research/user_stress.py capture --hex <firmware.hex> --reference <reference.json> --output <new-capture-dir>
```
