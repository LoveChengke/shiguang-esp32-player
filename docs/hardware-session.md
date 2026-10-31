# 调试记录：2026-10-31（Asia/Shanghai）

## 已读取的设备信息

- USB：VID `1A86` / PID `7523`，CH340 串口桥，安装驱动后端口为 COM3。
- esptool 4.8.1 自动识别：**ESP32-D0WD-V3 (revision v3.1)**。
- 双核、240MHz、40MHz 晶振，Flash manufacturer `0x20`、device `0x4016`、容量 **4MB**、Flash电压 3.3V。
- 该结果识别的是芯片及存储器，不能据此确定整块开发板商品名或排针顺序。`XXSR69` 单独不能作为 S3 或 PSRAM 容量证明。

## 串口驱动经验

最初 `serial.tools.list_ports` 只有电脑本身的 COM1。`pnputil /enum-devices /connected /problem` 查到 USB Serial、代码 28，说明是驱动缺失而非没有插入设备。

WCH 官网下载页依赖 JavaScript；真实 API 一度返回“请刷新页面后再重试”。改用 [WCH 官方 GitHub 镜像](https://github.com/WCH-IC/download/releases/download/CH341/CH341SER.ZIP)，没有使用第三方驱动网站。

- ZIP SHA256：`a7b08baaaf9b0e8548ffbab2a91fab8c75595f8d63a42a490fbf3fd1e747e21c`。
- INF `CH341SER/CH341SER.INF`，版本 `3.9.2024.09`，硬件 ID 匹配 VID/PID。
- `Get-AuthenticodeSignature CH341SER.CAT` 返回 `Valid`，签名者 Microsoft Windows Hardware Compatibility Publisher。
- 使用管理员 `pnputil /add-driver .../CH341SER.INF /install` 后，确认设备状态 Started、驱动 `oem1.inf`、COM3。此命令曾返回退出码 1，但设备随后确实正常启动；判断结果以重新枚举为准。

## 备份与测试进展

- 首次用 esptool默认串口缓冲读取 4MB，460800 波特率在约 6% 处出现 `Corrupt data`；降为 115200 仍发生短帧。失败的传输未形成有效备份，也没有写 Flash。
- 增加 1MiB Windows接收缓冲、把 stub窗口从 64 改为 1、把包长从 4096 改为 256，仍可复现短帧；因此没有把“缓冲太小”或“窗口太大”当作已证明的根因。
- 同一芯片通过 `--no-stub` ROM读取前 256KiB 成功（47.7秒）。`tools/backup_flash.py` 改为 ROM小块读取：每个 64KiB 块核对设备 MD5，空白块只有摘要匹配全 FF时才填充，种子缓存也先与设备匹配，最后核验整片摘要再写文件。完整 **4,194,304字节**备份已通过设备整片 MD5核验；本地 `.local/original-esp32-4mb.bin` 的 SHA256 为 `9e9b36a9c2d35c0708bbaf3b85dac93018897d71accb5c54154ed4879ba56cce`。
- 主固件首次 `esp32` 编译成功：Flash 1,661,393 字节、静态 RAM 24,340 字节。静态数值不含运行期音频/文件/任务分配。
- C++测试直接调用固件中的 `PocketCore.hpp`：12,542 项检查通过，包括长中文文本的逐页字节完整性、UTF-8 边界、CRLF/BOM、无效编码、书签匹配、按键去抖和计时回绕。
- SD准备工具的两项集成测试通过：GB18030/UTF-16 转 UTF-8、音频原样复制、冲突检测及写入前验证。
- `diagnostic` 固件已通过115200波特率刷入，esptool对写入数据报告 `Hash of data verified`。固件以 Flash 内置原创测试音和空输出统计器验证解码。

## 中断后的恢复与实机结果

用户重新连接后，端口从 COM3变为 **COM4**（USB location从1-1.5变为1-1.6）；原始备份与固件文件均完整。诊断程序仍在 Flash中，重新启动后可直接继续测试。

- 主动 `psramInit()` 探测返回false，报告 `PSRAM_PROBE detected=0`、`psram=0`；普通 ESP32目标继续按无 PSRAM配置运行。
- 中文 UTF-8分页：`SELFTEST UTF8_PAGE PASS consumed=24`。
- 原创4597字节 MP3：解码成功，输出 **12,097个采样**，耗时约55–56ms（空输出，并非实际播放时长）。
- 连续额外重复三次，空闲堆均为 **349,888字节**，最低堆315,260字节，没有观察到重复解码导致的内存下降。
- NVS重启计数从 **5到6**，说明本次串口复位后数据仍在。
- 首次验证脚本因控制台 GBK编码无法输出启动杂字而在打印阶段失败，实际检查与UTF-8日志已完成；给stdout显式设置UTF-8后重新运行，退出码0、`DEVICE PASS`。日志保留在 `.local/device-verification.log`。
- 启动曾提示 `No core dump partition found`；SDK启用了该功能，已为分区表补入末尾64KiB崩溃日志分区，并成功写入、核验。随后重新启动两次，提示已消失；最终验证日志 `.local/device-verification-final.log` 中，三次重复的空闲堆均为 **349,824字节**，最低堆315,196字节，NVS计数从 **9到10**，脚本退出码0、`DEVICE PASS`。
- 处理播放器正常 EOF时增加 DMA尾部排空，避免在最后一小段采样仍未输出时卸载 I2S；实际听感随外设验收一起检查。

## 交付状态

- `esp32`、`esp32s3`、`diagnostic` 三个构建配置均编译通过。主目标普通ESP32与S3应用约1.66MB，4MB目标预留末尾64KiB给崩溃日志。
- 最终桌面验证：C++核心12,542项检查、SD准备工具两项集成测试、Python工具语法检查通过。
- 板上目前保留**诊断固件**，未连接外设时可串口查询并重复解码测试。正式播放器固件、S3固件、诊断固件和源码归档由 `tools/package_release.py --include-s3` 生成到 `dist/`，含SHA256清单。
- 原始4MB备份只在本地 `.local/` 保留，未收入交付归档；串口进程均在测试后关闭，方便后续连接。

## 验证边界

当前未连接屏幕、SD卡、按键、DAC或喇叭。实际显示、SPI SD、按键电气输入、I2S音频、并行读取时断音情况均尚未实测。没有把主板诊断或编译成功记成这些项目通过。
