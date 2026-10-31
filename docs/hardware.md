# 硬件与接线

主固件支持普通 ESP32 与 ESP32-S3 两套**独立**配置。用户现有主控已实读为 ESP32-D0WD-V3 / 4MB Flash，优先复用它，不必为本首版另购 S3。具体开发板名称及排针顺序仍以实物为准。

## 采购清单

| 必需部件 | 规格与选择依据 |
| --- | --- |
| ESP32 开发板 | 复用现有双核 ESP32；代码不要求 PSRAM。新购可选官方 ESP32-S3-DevKitC-1-N8R8，使用 S3 配置。 |
| 彩屏 | 2.8 英寸、240×320、ILI9341、四线 SPI；选择明确支持 3.3V 供电和逻辑的模块。参考 [Adafruit ILI9341 模块](https://www.adafruit.com/product/1770)。不使用触摸功能。 |
| microSD 读卡模块 | 3.3V SPI 模块，独立引出 CS/SCK/MOSI/MISO；配 8–32GB 卡，FAT32。不要把只接受 5V 电源、带慢速电阻分压的模块直接照搬接线。 |
| 音频模块 | MAX98357A I2S 单声道功放，5V 供电。[原厂方案接线说明](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/pinouts)。 |
| 喇叭 | 8Ω / 2–3W 一只，连功放 SPK+ 与 SPK−。 |
| 按键及电阻 | 7 个常开按键、7 个 10kΩ 上拉电阻；另备面包板、短跳线。 |
| 电源 | USB 稳定 5V，建议电源至少 1A，接线短；开发板与功放共地。主板 3.3V 稳压输出须能承担屏幕及 SD 瞬时电流。 |

首版 USB 供电。便携电池需另用带保护、充电及电源路径管理的模块；若要耳机，增加合适的 DAC/耳放。MAX98357A 输出是差分喇叭驱动，SPK−不能接 GND，不能直接接常见共地耳机。[MAX98357A 说明](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/pinouts)；[PCM5102A](https://www.ti.com/product/PCM5102A)可作为另配耳放的线出 DAC，但不是本固件必需品。

## GPIO 对照

按 GPIO 名称接线，不按排针物理位置猜测。两列只能选择与固件环境匹配的一列。屏幕与存储卡分别使用不同 SPI 控制器。

| 外设信号 | 普通 ESP32：`esp32` | S3 N8R8：`esp32s3` |
| --- | ---: | ---: |
| TFT SCK | 18 | 12 |
| TFT MOSI | 23 | 11 |
| TFT CS | 14 | 10 |
| TFT DC | 27 | 9 |
| TFT RST | 26 | 8 |
| TFT MISO | 不接 | 不接 |
| SD SCK | 32 | 18 |
| SD MISO | 33 | 17 |
| SD MOSI | 13 | 16 |
| SD CS | 4 | 15 |
| MAX98357A BCLK | 25 | 5 |
| MAX98357A LRC/WS | 22 | 6 |
| MAX98357A DIN | 21 | 7 |
| 上键 | 16 | 1 |
| 下键 | 17 | 2 |
| 确认键 | 34 | 4 |
| 返回键 | 35 | 40 |
| 模式键 | 39 | 41 |
| 左键（后退进度） | 19 | 42 |
| 右键（前进进度） | 36 | 47 |

TFT VCC、SD VCC 接 3.3V；所有逻辑只用 3.3V。TFT LED 按所选模块说明接常亮背光供电（模块须有背光限流/驱动），不由 GPIO 直接承担 LED 电流。MAX VIN 接 5V、GND 共地；SD/EN 按模块说明保持使能、GAIN 使用默认配置。屏幕常亮，首版没有软件亮度调节。

七个按键的连接方式完全相同：GPIO 经 10kΩ 电阻接 3.3V，按键接在 GPIO 与 GND 之间，按下为 LOW。普通 ESP32 的 GPIO34/35/36/39 无内部上拉，不能省略这些电阻。[ESP32 GPIO 文档](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/peripherals/gpio.html)

ESP32 参考配置使用 GPIO16/17，接线前确认没有外部 PSRAM占用这两脚。S3 参考配置避开了 Octal PSRAM相关的 GPIO35–37、USB19/20 和启动配置脚；[官方 S3 开发板手册](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)说明了不同模组的占用差异。

## 外设验收

1. 断电接线，先接 SD 与屏，确认启动日志出现 `READY sd=1`、中文示例可打开，连续前后翻页无乱码。
2. 接功放与喇叭，从低音量播放 `test-tone.mp3`；再测自有 44.1/48kHz、128/320kbps、CBR/VBR MP3。
3. 连续播放至少 30 分钟，同时快速翻页、换书、切歌、暂停和调音量，监听断音并观察 heap 是否持续下降。
4. 翻页停留一秒，断电重启并重开同一本书，确认恢复；替换文件后确认旧进度不会越界。
5. 检查无卡、空目录、损坏 MP3、UTF-16/GB18030 小说的提示。文件复制完成后重启，首版不支持热插拔扫描。

只有 USB 主板的测试无法完成上述外设验收。详细已执行记录见 [hardware-session.md](hardware-session.md)。
