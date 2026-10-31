# ESP32 离线播放器工作约定

## 范围与已确认硬件

- 用户确认的首版范围：microSD 离线 MP3、中文 UTF-8 TXT、阅读进度保存、边听边看。设备运行无需网络。
- 2026-10-31 实读主控为 **ESP32-D0WD-V3，rev v3.1，双核 240MHz，4MB Flash**；诊断固件主动探测未发现可用 PSRAM。`XXSR69` 不是足够的型号依据，主板排针布局仍需核实。USB重接后端口由 COM3变为 COM4，每次连接都重新枚举。
- 当前实机只有 USB 连接。外设接线以 `include/board.hpp` 的 GPIO 编号为准，购买、接线、刷机或复测前阅读 `docs/hardware-session.md` 与 `docs/hardware.md`。

## 实现约束

- 普通 ESP32 是主要适配目标；保持无 PSRAM 可运行。S3 是单独构建配置，不能用它的固件或接线替代普通 ESP32。
- 音频对象全部由一个 FreeRTOS 任务管理，UI 通过队列发命令。所有运行期 SD 文件操作复用同一个互斥锁；分页按小窗口处理，历史偏移只缓存 64 项。
- 使用原始 UTF-8 **字节偏移**保存小说位置。改变布局或恢复旧进度后的向前定位需要协作式执行，以保持音频任务运行。
- 依赖版本由 `platformio.ini` 固定。ESP8266Audio 2.0.0 配 Arduino 2.0.17 的旧 I2S API；升级核心与音频库需要一起复核。显式关闭 MCLK，避免意外占用 GPIO0。
- 标准 ESP32 GPIO34/35/39 没有内部上拉，五个按键均需外接 10k 上拉。参考配置的 GPIO16/17 适用于无外部 PSRAM占用这些引脚的板子。

## 验证与记录

- 修改界面时使用源码级模拟器：`python tools/run_simulator.py` 自动编译实际 `src/main.cpp`、`src/reader.cpp` 与核心为 WASM；浏览器只提供硬件适配，保持界面与状态逻辑在固件源码中。架构、适配边界及源码热更新验证见 `docs/simulator.md`；模拟器改动运行 `node tests/simulator_test.cjs`，浏览器集成检查需先启动服务再运行 `node tools/test_simulator_browser.cjs --hot-reload`（会临时修改并原样恢复主页标题）。
- 固件改动运行 `python -m platformio run -e esp32 -j 3`；涉及 S3 板级代码时加编 `-e esp32s3`。分页或导入改动运行 `python tools/test.py`（依赖见 `requirements-dev.txt`）。
- 本机有项目内便携 Python：`.tools/python312/python.exe`；其 `_pth` 隔离不会自动加入脚本目录，工具互相导入时应显式加入该目录。
- Windows默认控制台编码可能为 GBK；输出原始串口日志的 Python工具应显式设置 UTF-8，避免测试已经完成但打印阶段抛 UnicodeEncodeError。
- 无外设实机验证使用 `diagnostic` 环境，只触碰串口、Flash/NVS及 PSRAM探测。MP3输出到计数器，能验证解码，不能证明 I2S出声、SD读取或显示效果。
- 串口或烧录操作一次只运行一个。先枚举端口并识别芯片，Flash改写前完成原始固件备份及摘要核验；备份、原始串口日志和缓存保存在被忽略的 `.local/`、`.tools/` 中。
- 本次 CH340 链路的 stub流式读取在多种波特率/缓冲/包长下短帧；ROM小块读取已通过。备份工具使用 ROM并按设备 MD5核验缓存和空白区，避免忽略传输错误。
- 烧录默认采用已实测成功的115200波特率，修改前先验证链路。读取失败不等于写入失败；写入仍须以设备摘要核验为准。
- SDK启用了 Flash core dump，因此分区表预留末尾 64KiB给 `coredump`；更改应用大小时保持这个分区，避免启动时报告找不到崩溃日志分区。
- 调试产生的新事实、失败原因、解决方法和验证边界写入 `docs/hardware-session.md`；这里保留会改变后续工作决策的经验。
