# STC-Link1D 独立下载脚本（实验）

Windows、Python 3.11+，仅使用标准库及系统 HID 驱动。无需 StudioX、Frida、USBPcap 或专用驱动。

当前支持范围严格限定为 2026-11-09 成功抓包的组合：

- STC-USB-Link1D：`34BF:1005`、MI_00、65 字节 HID 报告，26 字节桥身份中的固定前缀匹配。
- AI8051U-34K64：型号字段 `78 B4`，52 字节状态，BSL 原始字段 `74 55 0B`。
- 32 位应用地址页 `FF0000..FFFFFF`，最多 64 KiB，IRC 40 MHz、分频 1。
- 硬件选项采用抓包配置；当前状态不匹配时在擦除前拒绝。该官方下载把状态选项字节 16 对应值从 `FC` 改为 `FE`，本脚本复现此配置，不宣称保留所有选项。

**当前证据是官方成功下载、程序字节核对和脚本离线回放；脚本实烧尚未验收。** 不支持任意 Link1D 固件、其它 BSL、8 位映像、EEPROM 数据、离线烧录、加密或配置单独写入。

## 使用

在本目录执行：

```powershell
python stc_link1d.py plan --hex 'D:\project\.build\firmware.hex'
python stc_link1d.py list
```

`plan` 校验 HEX，不打开硬件；记录文件哈希、映像跨度与实际有数据字节数。稀疏地址填 `FF`，不把三个不同口径混用。`list` 只读取系统 HID 元数据，不发送 ISP 命令。

关闭 AiCube/AIapp/STC-ISP，停止其他软件对同一设备的下载活动，然后执行：

```powershell
python stc_link1d.py flash `
  --hex 'D:\project\.build\firmware.hex' `
  --sha256 '<plan 返回的 HEX 文件哈希>' `
  --device-path '<list 返回的完整 path>' `
  --output 'D:\evidence\new-download-directory'
```

`flash` 会进入 ISP、校准 IRC、擦除、写入程序与硬件配置，并发送退出命令。输出目录必须尚不存在；HEX 在打开设备前一次性读取并核对指定 SHA-256。不要在实际下载期间运行另一个下载器。与 StudioX 桥下载器共用设备文件租约，但 Windows 的共享 HID 打开不等于全系统独占。

本次擦除命令对 EEPROM 的影响尚未独立确认，因此不承诺保留 EEPROM。失败不自动重试、重放擦除或启动残缺程序，可能需要用官方工具恢复。退出报文没有设备应答，`runCommandSent` 不表示已观察到应用运行。

每次生成 `plan.json`、完整 `trace.ndjson`、`result.json`。结果分开记录代码块应答、配置应答、运行命令发送；Flash 回读、配置回读及功能执行均不伪报为通过。

## 离线验证

```powershell
python -m unittest -v test_protocol
python verify_capture.py --capture 'D:\capture\official.ndjson' --hex 'D:\capture\firmware.hex'
```

`verify_capture.py` 要求采集正常结束、无序号缺口、无未完成 I/O，并核对每个报告校验和。它使用同一个下载状态机重放官方应答，逐字节比较全部输出，包含两次 IRC 测量、59 个写块及结束控制；不打开硬件。需要自己的原始抓包和相应 HEX，仓库不提交用户固件和设备唯一身份。

协议解析入口是 `protocol.py`；`winhid.py` 负责 Win32 异步 I/O、超时取消、设备选择和租约；`stc_link1d.py` 提供 CLI。状态机与硬件传输分离，因此离线回放不替代传输层实测。

协议事实来自用户操作 AiCube-ISP v6.97C 的本次被动 API 记录；IRC 计数换算与帧结构对照了现有 StudioX.Ai8051Isp 实现，未绑定或打包该工程。详见 [研究记录](../../docs/STC-LINK1D.md)。
