# 固件源码级模拟器

模拟器直接将 `src/main.cpp`（由 `simulator/firmware_bridge.cpp` 包含）、`src/reader.cpp`、`src/music_info.cpp`、`PocketCore.hpp` / `MediaCore.hpp` 编译为 WebAssembly。浏览器接收固件实际发出的绘图调用。六个页面、按键判断、菜单状态、翻页、书签校验、保存时机和设置均由原 C++ 代码执行，JavaScript 不维护另一套页面或阅读状态机。

## 启动与修改源码

在项目根目录双击 **开发模拟器.cmd**，自动启动本地服务并打开 <http://127.0.0.1:8765/>。服务只绑定本机，服务目录仅为 `simulator/`，不会提供原始 Flash 备份或整个工作目录。

本工作目录已有 `.tools/python312/python.exe` 和 Zig 依赖。复制到其他电脑时先安装 Python 3.10+，然后 `python -m pip install ziglang==0.14.1`。运行预构建版本无需安装编译器；双击 **打开模拟器.cmd** 即可离线查看，但这个入口不会自动构建。

终端启动开发服务：

```powershell
.tools/python312/python.exe tools/run_simulator.py
```

1. 编辑 `src/main.cpp` 中的标题、布局、颜色、菜单或按键逻辑，保存。
2. 后台检测到变化，调用 `tools/build_simulator.py`。页面右侧显示构建状态。
3. 编译成功后，浏览器载入新的 WASM 并重新执行固件 `setup()`。设置和已保存的书签保留，音乐停止，页面回到固件初始状态。
4. 编译失败时保留上次成功版本。点击红色“编译失败”查看编译器输出；修复源码并保存后自动重试。

监视范围由 `tools/build_simulator.py` 的 `source_files()` 决定，包括实际消费的主界面、阅读器、核心、头文件和 HAL 适配代码。修改 `simulator/host.js`、HTML 或 CSS 后刷新浏览器；它们是电脑外壳与硬件适配，不是固件界面源码。

单独构建并打开文件也可查看新效果：

```powershell
.tools/python312/python.exe tools/build_simulator.py
```

随后重新打开或刷新 `simulator/index.html`。生成的 `firmware.js` 内嵌同一个 `assets/firmware.wasm`，使直接打开本地 HTML 时不依赖 `fetch(file://...)`。`build-info.json` 记录源文件清单、输入摘要与二进制摘要。

## 适配边界

| 固件接口 | 浏览器中的实现 |
| --- | --- |
| `setup()` / `loop()` | 执行原 C++，浏览器每 8ms 调用循环 |
| LovyanGFX 绘图 | 原坐标和 RGB565 颜色转换为 Canvas 绘图；使用系统中文字形近似 efont |
| `digitalRead()` / `millis()` | 虚拟 GPIO 和浏览器时钟；实际执行原 30ms 去抖、650ms 长按代码 |
| `SD` / `File` | C++ 虚拟文件系统，固件实际扫描目录、分窗口读取小说 |
| `Preferences` | 原二进制 NVS blob 保存到浏览器 localStorage，书签键与内容由固件生成 |
| `player` 接口 | 保持原 UI 命令接口，HTML Audio 播放本地 MP3，结束事件交回固件处理循环切歌 |
| FreeRTOS 信号量 | 单线程兼容接口，不能验证并发、任务调度或互斥时序 |

模拟器不执行 ESP32 `.bin` 或 Xtensa 指令，不模拟 SPI/I2S 电气时序、真实 heap、Flash 磨损或音频质量。`src/audio_service.cpp` 由 `simulator/audio_adapter.cpp` 替代，因此修改真实解码、DMA 或任务代码需要实机测试。增加尚未适配的 Arduino/LovyanGFX API 时，编译会明确失败，需要在 `simulator/hal/` 补充相应 API。

右侧快捷入口只通过桥接函数跳转固件页面；七个设备按键则输入虚拟 GPIO，完整经过固件的去抖与事件逻辑。浏览器将极快的鼠标点击保持至少 55ms，避免真实去抖逻辑将其当成抖动忽略。固件循环使用独立的 8ms 定时器，避免绘图帧率低时漏过短按；浏览器后台定时器仍可能降频，协作式查页的墙钟耗时与实机不同。

## 媒体与记录

- 自带两段原创 24 秒 MP3 和两篇原创 TXT，全部随项目提供，运行时无 CDN 或联网依赖。
- 支持导入 MP3、LRC、JPG/PNG 和 UTF-8 / GB18030 / UTF-16 TXT；可选择整个文件夹并保留最多 3 级目录；导入时将文本转换为 UTF-8。模拟器单本上限 8MB，单曲上限 128MB。
- 导入文件存放在浏览器 IndexedDB。导入后重新启动固件，走同一套 SD 扫描逻辑；这与实机启动时扫描的行为一致。
- 音频字节内容由浏览器管理，虚拟 SD 内挂载 MP3 前 128KB，用原 C++ 检测内嵌歌词封面，外置 LRC / 图片则完整挂载；播放和跳转由 HTML Audio 计时，无法证明固件字节消费或解码行为。
- 浏览器 origin 不同，记录也独立：`file://` 与 `http://127.0.0.1:8765` 不共用书签；清除浏览器数据会删除模拟记录。
- 模拟重启不重刷、不连接 ESP32，不影响板上的诊断固件和本地原始备份。

## 验证与记录（2026-10-31）

```powershell
node tests/simulator_test.cjs
node tools/test_simulator_browser.cjs --hot-reload
```

第一组执行真实固件 WASM，**88 项断言通过**：SD 目录扫描、七键导航、音量与暂停时跳转、阅读 40% / 末尾跳转、字号、UTF-8 边界、书签恢复、时钟保存、自选顺序与文件夹循环、ID3v2.3/v2.4 中文歌词封面、大小写一致的外置歌词、空文件拒绝。

第二组在隔离的无头 Edge 中执行，**33 项集成断言通过**：浏览器播放与真实时间跳转、实际按键、边听边看、40% 阅读跳转与恢复、UTF-16 / 文件夹导入、歌词与封面像素绘制、自选列表保存与跨刷新恢复、移动端无横向溢出，以及关键的源码更新验证：临时修改 `src/main.cpp` 的主页标题，观察自动编译后屏幕真实变更；再临时加入 `#error`，确认错误展示且成功版本仍运行；最后原样恢复源文件并自动编译恢复。测试通过 `try/finally` 恢复源码。

另执行 `node tools/test_simulator_browser.cjs --file-only`，本地 `file://` 入口的固件启动、真实阅读器和无浏览器异常 **3 项断言通过**，验证无需开发服务也能查看预构建固件。

截图保存在 `.local/simulator-home.png`、`simulator-reader.png`、`simulator-reader-seek.png`、`simulator-player.png`、`simulator-settings.png`、`simulator-mobile.png`，不包含在源码包中。浏览器工具内核在本会话无法初始化；受限环境内的 Edge 调试连接会断开，改用获准的隔离无头进程后通过，未操作用户已有浏览器窗口。

Windows 一键开发启动器将后台服务日志与记录写入 `.local/simulator-server*`。再次启动会复用本项目已运行的服务。双击 **停止模拟器.cmd** 可停止启动器管理的后台服务：本地控制接口校验启动时生成的随机令牌，然后由服务自行退出。终端直接运行的服务在终端按 Ctrl+C 停止。

## 本次交互扩展

- 固件新增左右两键：短按调整歌曲进度，长按切歌；播放页上下直接调音量。
- 曲库和书架由固件按目录浏览。曲库长按确认增删自选列表，NVS 按完整路径摘要保存加入顺序；文件夹与自选列表均由固件处理 EOF 循环。
- 阅读页确认打开进度 / 字号菜单，上下调整百分比或字号后再确认；跳转与书签仍使用原始 UTF-8 字节偏移。
- 各页面保留时钟，底部移除常驻按键提示。模拟器首次采用浏览器当地时间，之后使用固件校时和设置保存逻辑。
- 图片适配只接收固件发出的 JPEG / PNG 字节与绘图坐标，浏览器不检测封面或维护歌词状态。音频尚未加载数据时的跳转延后到 loadeddata 应用。
- 多个开发服务同时监视同一个目录时，WASM / JS 临时产物使用进程 ID 区分。Windows 编译器可能短暂占用源码，热更新测试会重试写入及恢复。

无头浏览器回归关闭后台计时降频并将测试页置前，避免把短按延长成 650ms 长按；生产模拟器对已满足 55ms 的释放直接执行，不再排入零延时定时器。
