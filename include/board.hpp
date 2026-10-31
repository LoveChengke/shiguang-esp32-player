#pragma once

namespace board {
#if defined(POCKET_BOARD_S3)
constexpr const char* name = "ESP32-S3 N8R8";
constexpr int tftSck = 12, tftMosi = 11, tftCs = 10, tftDc = 9, tftReset = 8;
constexpr int sdSck = 18, sdMiso = 17, sdMosi = 16, sdCs = 15;
constexpr int i2sBclk = 5, i2sLrc = 6, i2sDout = 7;
constexpr int buttons[] = {1, 2, 4, 40, 41};
#elif defined(POCKET_BOARD_ESP32)
constexpr const char* name = "ESP32 WROOM reference";
constexpr int tftSck = 18, tftMosi = 23, tftCs = 14, tftDc = 27, tftReset = 26;
constexpr int sdSck = 32, sdMiso = 33, sdMosi = 13, sdCs = 4;
constexpr int i2sBclk = 25, i2sLrc = 22, i2sDout = 21;
constexpr int buttons[] = {16, 17, 34, 35, 39};
#else
#error Select a supported PlatformIO environment.
#endif
constexpr int width = 240, height = 320;
constexpr unsigned maxFiles = 128;
constexpr unsigned maxPathBytes = 192;
// All five switches use external 10k pull-ups to 3.3V, active LOW.
// ESP32 GPIO34/35/39 have no internal pull-up. GPIO16/17 need a WROOM board.
}
