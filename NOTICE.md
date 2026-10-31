# Copyright and third-party notices

Original application, simulator, paging engine, tools, sample texts and demo audio:
Copyright (c) 2026 ESP32MP4 contributors. GPL-3.0-or-later; see LICENSE.

Build dependencies are fetched separately and keep their original notices:

- ESP8266Audio 2.0.0 — Earle F. Philhower, III and contributors; relevant player and MP3/libmad components GPL-3.0-or-later. [Source and component licenses](https://github.com/earlephilhower/ESP8266Audio/tree/2.0.0).
- LovyanGFX 1.2.35 — lovyan03 and contributors; FreeBSD/Simplified BSD. Included efont CN font data retains its BSD-3-Clause notice. [Licenses](https://github.com/lovyan03/LovyanGFX/tree/1.2.35).
- Arduino-ESP32 2.0.17 — Espressif and contributors; LGPL-2.1-or-later and file-level licenses. [Source](https://github.com/espressif/arduino-esp32/tree/2.0.17).
- esptool 4.8.1 — Espressif and contributors; GPL-2.0-or-later. The read-only backup tool calls its ESP32 ROM loader and device MD5 functions. [Source](https://github.com/espressif/esptool/tree/v4.8.1).
- The optional fixture regeneration tool uses lameenc 1.8.1 / LAME under their own LGPL licenses. The generated sine wave is original project audio.
- The simulator WASM includes Zig/LLVM C++ and WASI runtime components. Their notices and licenses are reproduced in `simulator/licenses/`; the simulator HAL is original application code, not Arduino or LovyanGFX implementation code.

ESPuino, ESP32-MiniWebRadio and CrossPoint Reader were consulted for architectural comparison; their application code is not vendored here. Vendor drivers and device backup images stay outside the distributed project.
