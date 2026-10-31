"""Run the actual C++ firmware core and Python SD-preparation integration tests."""
import os
import pathlib
import subprocess
import sys
import unittest
import tempfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from prepare_sd import prepare

ROOT = pathlib.Path(__file__).resolve().parents[1]

class PrepareSDTests(unittest.TestCase):
    def test_chinese_conversion_and_audio_copy(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            book = root / "novel.txt"
            book.write_bytes("第一章\r\n山间的风。".encode("gb18030"))
            mp3 = root / "track.mp3"
            mp3.write_bytes(b"test-audio-data")
            output = root / "sd"
            self.assertEqual(len(prepare(output, [mp3], [book], "gb18030")), 2)
            self.assertEqual((output / "books/novel.txt").read_bytes(), "第一章\n山间的风。".encode())
            self.assertEqual((output / "music/track.mp3").read_bytes(), mp3.read_bytes())

    def test_validation_before_writes_and_utf16(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            book = root / "book.txt"
            book.write_bytes("中文小说".encode("utf-16"))
            output = root / "sd"
            prepare(output, books=[book])
            self.assertEqual((output / "books/book.txt").read_text(encoding="utf-8"), "中文小说")
            book.write_text("replacement", encoding="utf-8")
            with self.assertRaises(ValueError):
                prepare(output, books=[book])
            self.assertEqual((output / "books/book.txt").read_text(encoding="utf-8"), "中文小说")
            a = root / "a"; a.mkdir()
            (a / "book.txt").write_text("other", encoding="utf-8")
            with self.assertRaises(ValueError):
                prepare(root / "new", books=[root / "book.txt", a / "book.txt"])
            self.assertFalse((root / "new").exists())

def main():
    os.chdir(ROOT)
    build = ROOT / ".tools"
    build.mkdir(exist_ok=True)
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(build / "zig-cache"))
    exe = build / ("core_test.exe" if os.name == "nt" else "core_test")
    subprocess.run([sys.executable, "-m", "ziglang", "c++", "-std=c++17", "-Wall", "-Wextra",
                    "-Ilib/PocketCore/src", "tests/core_test.cpp", "-o", str(exe)], check=True, env=env)
    subprocess.run([str(exe)], check=True)
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(PrepareSDTests))
    return 0 if result.wasSuccessful() else 1

if __name__ == "__main__":
    sys.exit(main())
