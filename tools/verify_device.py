"""Verify the diagnostic firmware on a USB-only board, including reset persistence."""
import argparse
import pathlib
import re
import sys
import time
import serial

def receive(port, marker, seconds=12):
    deadline = time.monotonic() + seconds
    result = bytearray()
    while time.monotonic() < deadline:
        result.extend(port.read(port.in_waiting or 1))
        if marker in result:
            time.sleep(0.1)
            result.extend(port.read(port.in_waiting))
            return result.decode("utf-8", errors="replace")
    raise RuntimeError("Diagnostic timeout: " + result.decode("utf-8", errors="replace"))

def boot(port):
    port.reset_input_buffer()
    port.dtr = False
    port.rts = True
    time.sleep(0.1)
    port.rts = False
    log = receive(port, b"READY:")
    for required in ("SELFTEST UTF8_PAGE PASS", "SELFTEST MP3_DECODE PASS", "SELFTEST NVS PASS", "SELFTEST RESULT PASS"):
        if required not in log:
            raise RuntimeError("Missing " + required + ": " + log)
    match = re.search(r"NVS PASS boots=(\d+)", log)
    return int(match.group(1)), log

def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", default=".local/device-verification.log")
    args = parser.parse_args()
    port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    port.dtr = False; port.rts = False; port.port = args.port
    port.open()
    logs = []
    try:
        before, log = boot(port); logs.append(log)
        heaps = []
        for _ in range(3):
            port.write(b"t\n")
            logs.append(receive(port, b"SELFTEST RESULT PASS"))
            port.write(b"?\n")
            memory = receive(port, b"uptime_ms=")
            logs.append(memory)
            heaps.append(int(re.search(r"MEM heap=(\d+)", memory).group(1)))
        if min(heaps) < 100000 or max(heaps) - min(heaps) > 128:
            raise RuntimeError("Unexpected idle heap change: %r" % heaps)
        after, log = boot(port); logs.append(log)
        if after != before + 1:
            raise RuntimeError("NVS did not survive reset: %d -> %d" % (before, after))
        summary = "DEVICE PASS: MP3 decode, UTF-8 pagination, 3 repeats, stable heap=%s, NVS boots=%d->%d" % (heaps, before, after)
        logs.append(summary + "\n")
        target = pathlib.Path(args.log); target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("\n".join(logs), encoding="utf-8")
        print("\n".join(logs))
    finally:
        port.close()

if __name__ == "__main__":
    main()
