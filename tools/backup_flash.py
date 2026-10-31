"""Read-only classic ESP32 backup using reliable ROM reads and device MD5 checks.

Blank 64KiB regions and an optional seed are used only after matching device MD5.
A failed transfer never becomes a valid backup. No erase/write commands are used.
"""
import argparse
import hashlib
import pathlib
from esptool.targets.esp32 import ESP32ROM

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--size", type=lambda value: int(value, 0), default=0x400000)
    parser.add_argument("--seed", help="Optional prefix from a prior verified ROM read")
    args = parser.parse_args()
    target = pathlib.Path(args.output)
    if target.exists():
        parser.error("Backup already exists; choose another filename")
    target.parent.mkdir(parents=True, exist_ok=True)
    if args.size <= 0 or args.size > 0x400000:
        parser.error("This helper supports up to 4MiB of a confirmed classic ESP32")
    esp = ESP32ROM(args.port, 115200)
    try:
        esp.connect()
        print("BACKUP chip", esp.get_chip_description(), flush=True)
        esp.flash_spi_attach(0)
        esp.flash_set_parameters(0x400000)
        data = bytearray(b"\xff" * args.size)
        covered = 0
        if args.seed:
            seed = pathlib.Path(args.seed).read_bytes()
            if len(seed) > args.size or len(seed) % 65536:
                raise ValueError("Seed must contain a whole number of 64KiB regions")
            if hashlib.md5(seed).hexdigest() != esp.flash_md5sum(0, len(seed)):
                raise RuntimeError("Seed no longer matches device flash")
            data[:len(seed)] = seed
            covered = len(seed)
            print("BACKUP seed MD5 verified", covered, flush=True)
        for offset in range(covered, args.size, 65536):
            length = min(65536, args.size - offset)
            device_md5 = esp.flash_md5sum(offset, length)
            if device_md5 == hashlib.md5(b"\xff" * length).hexdigest():
                kind = "blank"
            else:
                chunk = esp.read_flash(offset, length)
                if len(chunk) != length or hashlib.md5(chunk).hexdigest() != device_md5:
                    raise RuntimeError("ROM block verification failed at 0x%x" % offset)
                data[offset:offset + length] = chunk
                kind = "read"
            print("BACKUP %s %d/%d" % (kind, offset + length, args.size), flush=True)
        if hashlib.md5(data).hexdigest() != esp.flash_md5sum(0, args.size):
            raise RuntimeError("Whole-flash MD5 mismatch; backup rejected")
    finally:
        try:
            esp.hard_reset()
        finally:
            esp._port.close()
    target.write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    target.with_suffix(target.suffix + ".sha256").write_text(digest + "  " + target.name + "\n", encoding="ascii")
    print("BACKUP VERIFIED", len(data), "bytes SHA256", digest)

if __name__ == "__main__":
    main()
