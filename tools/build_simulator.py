"""Compile the real firmware UI and reader with browser hardware adapters."""
import base64
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]

def source_files():
    return sorted([ROOT / p for p in ('src/main.cpp', 'src/reader.cpp', 'src/music_info.cpp', 'include/music_info.hpp', 'lib/PocketCore/src/MediaCore.hpp', 'include/board.hpp', 'include/reader.hpp',
        'include/audio_service.hpp', 'lib/PocketCore/src/PocketCore.hpp', 'simulator/firmware_bridge.cpp',
        'simulator/audio_adapter.cpp', 'tools/build_simulator.py')] + list((ROOT / 'simulator/hal').rglob('*.*')))

def source_digest():
    digest = hashlib.sha256()
    for path in source_files():
        digest.update(path.relative_to(ROOT).as_posix().encode()); digest.update(path.read_bytes())
    return digest.hexdigest()

def main():
    inputs_before = source_digest()
    target = ROOT / "simulator/assets/firmware.wasm"
    # A terminal server and the desktop launcher can watch the same checkout.
    # Keep their intermediate files separate before atomically publishing.
    temporary = target.with_name('firmware.%s.tmp.wasm' % os.getpid())
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT / ".tools/zig-cache"))
    subprocess.run([sys.executable, "-m", "ziglang", "c++", "-target", "wasm32-wasi", "-mexec-model=reactor",
                    "-std=c++17", "-Os", "-fno-exceptions", "-Wl,--strip-all", "-DPOCKET_BOARD_ESP32",
                    "-I" + str(ROOT / "simulator/hal"), "-I" + str(ROOT / "include"), "-I" + str(ROOT / "lib/PocketCore/src"),
                    str(ROOT / "simulator/firmware_bridge.cpp"), str(ROOT / "src/reader.cpp"), str(ROOT / "src/music_info.cpp"), str(ROOT / "simulator/audio_adapter.cpp"),
                    *["-Wl,--export="+name for name in ('sim_setup','sim_clock','sim_loop','sim_key','sim_serial','sim_alloc','sim_free','sim_file','sim_navigate','sim_open_book','sim_play_track','sim_volume','sim_save','sim_snapshot','sim_page_lines','sim_page_line')],
                    "-Wl,--export-memory", "-Wl,-z,stack-size=65536", "-Wl,--initial-memory=2097152", "-Wl,--max-memory=67108864",
                    "-o", str(temporary)], check=True, env=env, cwd=ROOT)
    if source_digest() != inputs_before:
        raise RuntimeError('Sources changed during compilation; the watcher will build again.')
    temporary.replace(target)
    encoded = base64.b64encode(target.read_bytes()).decode("ascii")
    js = ROOT / "simulator/firmware.js"
    temporary_js = js.with_name('firmware.%s.tmp' % os.getpid())
    temporary_js.write_text(
        "// Generated from src/main.cpp + src/reader.cpp; do not edit.\n"
        "globalThis.POCKET_FIRMWARE_BASE64 = \"" + encoded + "\";\n", encoding="utf-8")
    temporary_js.replace(js)
    (ROOT / 'simulator/build-info.json').write_text(json.dumps({'version':hashlib.sha256(target.read_bytes()).hexdigest(),
        'sourceHash':inputs_before, 'builtAt':time.time(), 'sources':[p.relative_to(ROOT).as_posix() for p in source_files()]},indent=2)+'\n',encoding='utf-8')
    print("Built firmware simulator:", target.stat().st_size, "bytes", flush=True)

if __name__ == "__main__":
    main()
