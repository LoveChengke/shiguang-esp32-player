"""Package compiled firmware and its source without local tools or device backups."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-s3", action="store_true")
    parser.add_argument("--players-only", action="store_true", help="Package player firmware without requiring a diagnostic build")
    args = parser.parse_args()
    output = ROOT / "dist"
    environments = ["esp32"] + ([] if args.players_only else ["diagnostic"]) + (["esp32s3"] if args.include_s3 else [])
    for env in environments:
        for filename in ("bootloader.bin", "partitions.bin", "firmware.bin"):
            if not (ROOT / ".pio/build" / env / filename).is_file():
                raise RuntimeError("Build %s before packaging" % env)
    output.mkdir(exist_ok=True)
    for env in environments:
        target = output / env
        target.mkdir(exist_ok=True)
        for filename in ("bootloader.bin", "partitions.bin", "firmware.bin"):
            shutil.copyfile(ROOT / ".pio/build" / env / filename, target / filename)
        s3 = env == "esp32s3"
        subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3" if s3 else "esp32",
                        "merge_bin", "-o", str(target / "full.bin"), "--flash_mode", "dio",
                        "--flash_freq", "40m", "--flash_size", "8MB" if s3 else "4MB",
                        "0x0" if s3 else "0x1000", str(target / "bootloader.bin"),
                        "0x8000", str(target / "partitions.bin"), "0x10000", str(target / "firmware.bin")], check=True)
    folders = ["include", "src", "lib", "tools", "tests", "docs", "sdcard", "simulator"]
    source_files = [ROOT / f for f in ("README.md", "AGENTS.md", "NOTICE.md", "LICENSE", "platformio.ini", "partitions.csv", "requirements-dev.txt", ".gitignore", "打开模拟器.cmd", "开发模拟器.cmd", "停止模拟器.cmd")]
    source_files += [p for name in folders for p in (ROOT / name).rglob("*")
                     if p.is_file() and "__pycache__" not in p.parts and not p.name.endswith((".tmp", ".tmp.wasm"))]
    with zipfile.ZipFile(output / "esp32mp4-source.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for file in sorted(source_files):
            archive.write(file, file.relative_to(ROOT).as_posix())
    hashes = {str(p.relative_to(output)).replace("\\", "/"): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in output.rglob("*") if p.is_file() and p.name != "SHA256SUMS.json"}
    (output / "SHA256SUMS.json").write_text(json.dumps(hashes, indent=2) + "\n", encoding="utf-8")
    print("Packaged", environments, "and", len(source_files), "source files in", output)

if __name__ == "__main__":
    main()
