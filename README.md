# 拾光 · ESP32 离线音乐与小说播放器

基于普通 ESP32 的本地 MP3 播放器与中文 TXT 阅读器。存储卡保存媒体，内置中文字库，设备运行不需要 Wi-Fi、账号或服务器。

用户的 USB 主板已实读为 **ESP32-D0WD-V3 / rev v3.1 / 双核 240MHz / 4MB Flash**，默认 `esp32` 环境以此为目标，不要求 PSRAM；另提供 ESP32-S3 N8R8 配置。硬件识别与测试实录见 [调试记录](docs/hardware-session.md)，接线和采购见 [硬件文档](docs/hardware.md)。

## 功能

- microSD 本地 MP3，播放/暂停、上一首/下一首、音量、列表循环/单曲循环。
- 中文 UTF-8 TXT 阅读，16/24 像素字号、前后翻页、按书保存位置，音乐可在后台继续。
- 深夜/暖纸主题；设置和书签保存在主板 NVS，文件修改后用长度和头部指纹检查旧位置。
- 音频任务与 UI 分开，SD 访问统一互斥；小说一次只读取 2KB 窗口，最近 64 个页面偏移保存在 RAM，不会把整本小说读入内存。

阅读位置在最后一次翻页后约 750ms 保存，退出阅读时立即保存。突然断电可能丢失尚未保存的最后一次翻页。打开旧进度后首次向前翻页，可能需要从书首逐页定位；过程可响应 UI，音乐任务继续运行。

首版支持 MP3 与 TXT；不包含 EPUB/PDF、视频、蓝牙耳机、联网曲库、歌词、音乐断电续播或任意时间拖动。进度条表示音频文件消费的近似字节比例，不能视为精确时间。内置字体覆盖常用中文，不保证所有罕见字或 emoji。

## 电脑模拟器：改源码即可查看

双击根目录的 **开发模拟器.cmd**，打开 <http://127.0.0.1:8765/>。修改 `src/main.cpp` 的界面/按键或 `src/reader.cpp` 后保存，后台自动编译并载入新固件；编译失败会显示错误并继续运行上次成功版本。

模拟器直接运行这些原始 C++ 源码编译的 WebAssembly，屏幕和阅读状态由固件生成。浏览器负责虚拟屏幕、GPIO、SD、NVS 和音频接口。**打开模拟器.cmd** 可直接离线打开最近一次构建，无需启动服务。

支持五个按键的长短按、键盘、六个界面、边听边看、文件导入与书签保存。它使用系统中文字形与浏览器 MP3 播放，不仿真 Xtensa、真实内存、I2S 或 FreeRTOS 时序。使用、适配范围和源码更新验证见 [模拟器文档](docs/simulator.md)。

## 构建

Python 3.10+，首次下载编译器和依赖需要联网；之后可使用本地缓存构建。固件使用 PlatformIO 固定平台与库版本，详见 [platformio.ini](platformio.ini)。

```powershell
python -m pip install -r requirements-dev.txt
python -m platformio run -e esp32 -j 3
python tools/test.py
```

本工作目录已有便携工具，可直接将上面的 `python` 替换为 `.tools\python312\python.exe`。缓存和工具保存在项目内，不需要改系统 Python。

可选 S3 构建：

```powershell
python -m platformio run -e esp32s3 -j 3
```

普通 ESP32 的应用二进制位于 `.pio/build/esp32/firmware.bin`，它是应用分区文件，写入偏移为 `0x10000`。完整刷写还包含引导程序与分区表，建议使用 PlatformIO 上传命令。分区使用单个 factory 应用，不提供 OTA。

本次已生成的交付物在 `dist/`：

- `esp32/full.bin`：普通 ESP32播放器完整镜像，写入偏移 `0x0`。
- `esp32s3/full.bin`：S3 N8R8对应镜像，写入偏移 `0x0`；两种芯片的镜像不能混用。
- `diagnostic/full.bin`：普通 ESP32无外设诊断镜像。
- `esp32mp4-source.zip`：源码、测试、硬件文档与示例媒体；`SHA256SUMS.json`记录交付文件摘要。

重新打包使用 `python tools/package_release.py --include-s3`，需先完成三个环境构建。当前实机保留诊断固件，正式播放器等外设接好后按下文上传。

## 存储卡

将 8–32GB microSD 格式化为 FAT32，根目录为：

```text
/music/test-tone.mp3
/music/your-song.mp3
/books/start.txt
/books/your-novel.txt
```

`sdcard/books/start.txt` 是原创中文示例，`sdcard/music/test-tone.mp3` 是原创的 440Hz、约半秒单声道测试音。可以直接把 `sdcard` 中的两个目录复制到卡根目录。

也可先生成一个准备好的目录：

```powershell
python tools/prepare_sd.py --output dist/sdcard --music sdcard/music --books sdcard/books
python tools/prepare_sd.py --output dist/sdcard --books "D:\MyBooks" --encoding gb18030
```

工具将小说转换成 UTF-8、统一换行；自动识别带 BOM 的 UTF-16。默认拒绝覆盖不同内容的同名文件；明确要替换时加 `--overwrite`。输入文件保持原样。向卡复制前先检查输出，再将 `music` 和 `books` 两个目录复制过去。

每类最多扫描 128 个文件、最多下探 3 级子目录、完整路径少于 192 个 UTF-8 字节。准备工具将输入平铺到两个目录，会提前拒绝同名冲突。文件名排序采用字节顺序；建议用 `001-...` 前缀固定顺序。首版启动时扫描媒体，换卡或复制新内容后重新启动。

## 按键

所有按键接线都需要外部上拉，具体 GPIO 见 [接线表](docs/hardware.md)。

| 操作 | 短按 | 长按 |
| --- | --- | --- |
| 上 / 下 | 菜单选择；音乐上一首/下一首；阅读上一页/下一页 | 音量加 / 减 |
| 确认 | 打开；音乐暂停/继续；阅读切换字号；设置中更改选项 | 随时播放/暂停 |
| 返回 | 返回书架/曲库，再返回主页；离开阅读时保存 | 保存并回主页 |
| 模式 | 音乐页面与当前阅读/书架切换 | 随时播放/暂停 |

串口 115200 支持 `u d o b m` 模拟五个短按、`p` 播放/暂停、`+ -` 音量、`?` 输出当前状态。无卡时主页会提示插卡并重试，设置页仍可进入。

## USB 主板诊断与上传

先通过串口枚举和 esptool 确认芯片，COM3 是本次会话端口，换电脑后需重新查找：

```powershell
python -m serial.tools.list_ports -v
python -m esptool --port COM3 chip_id
python -m esptool --chip esp32 --port COM3 flash_id
```

外设未接时使用诊断环境，MP3 解码输出到计数器，不驱动屏幕、按键或 I2S 引脚。它报告 Flash/PSRAM/heap、UTF-8 分页、MP3 解码和 NVS 重启计数。

```powershell
python -m platformio run -e diagnostic -j 3
# 完成原有 Flash 备份并核验之后，才执行上传：
python -m platformio run -e diagnostic -t upload --upload-port COM3
python -m platformio device monitor -p COM3 -b 115200
```

接好外设后刷主固件：

```powershell
python -m platformio run -e esp32 -t upload --upload-port COM3
```

诊断固件可用 `python tools/verify_device.py --port COM4`自动测试中文分页、MP3解码、重复解码内存与NVS跨复位保存。实机结果：ESP32-D0WD-V3、4MB Flash，主动探测未发现可用PSRAM；解码与上述检查已通过，详见调试记录。

原始 Flash 备份在 `.local/` 内本地保存，不放入版本库或发布包；它可能包含设备原先的私有数据。每个串口只允许一个监视/烧录/备份进程同时占用。本次断开重连后端口已由 COM3变为 COM4，命令中的端口需替换为重新枚举的实际值。

## 开源参考与许可

实现使用了固定版本的 [ESP8266Audio 2.0.0](https://github.com/earlephilhower/ESP8266Audio/tree/2.0.0) 与 [LovyanGFX 1.2.35](https://github.com/lovyan03/LovyanGFX/tree/1.2.35)。前者的 libmad 解码不强制要求 PSRAM；后者提供 ILI9341 和内置简体中文字库。Arduino 2.0.17 来自 [Espressif32 6.12.0](https://github.com/platformio/platform-espressif32/releases/tag/v6.12.0)。

选型时参考了 [ESPuino](https://github.com/biologist79/ESPuino) 的离线播放器结构、[ESP32-MiniWebRadio](https://github.com/schreibfaul1/ESP32-MiniWebRadio) 的显示/本地音频方案与 [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) 的阅读方向，没有整套移植这些项目。[ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) 当前版本要求 PSRAM和较新 Arduino 核心，因此没有用作这块普通 ESP32 的首版依赖。

本项目代码按 GPL-3.0-or-later 发布；库与字体保留各自许可，见 [NOTICE.md](NOTICE.md) 与 [LICENSE](LICENSE)。

[Wokwi 官方外设表](https://docs.wokwi.com/guides/esp32)显示 S3 的 I2S 未实现；本项目没有把仿真页面或静默解码当作真实出声测试。已执行验证与尚未完成的外设验收分别记录在 [调试记录](docs/hardware-session.md) 和 [硬件文档](docs/hardware.md)。
