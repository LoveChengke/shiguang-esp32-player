"""Copy MP3/TXT to a staging directory; convert novels explicitly to UTF-8."""
import argparse
import pathlib

def gather(paths, suffix):
    files = []
    for value in paths:
        path = pathlib.Path(value)
        if not path.exists():
            raise ValueError("Input does not exist: %s" % path)
        candidates = sorted(path.rglob("*")) if path.is_dir() else [path]
        files.extend(p for p in candidates if p.is_file() and p.suffix.lower() == suffix)
    return files

def prepare(output, music=(), books=(), encoding="utf-8", overwrite=False):
    output = pathlib.Path(output)
    inputs = [("music", path) for path in gather(music, ".mp3")]
    inputs += [("books", path) for path in gather(books, ".txt")]
    for kind in ("music", "books"):
        if sum(k == kind for k, _ in inputs) > 128:
            raise ValueError("Firmware supports at most 128 files per library")
    planned = {}
    for kind, source in inputs:
        target = output / kind / source.name
        if len(("/" + kind + "/" + source.name).encode("utf-8")) >= 192:
            raise ValueError("Path is too long: %s" % source.name)
        normalized = str(target).casefold()
        if normalized in planned:
            raise ValueError("Duplicate destination filename: %s" % source.name)
        data = source.read_bytes()
        if kind == "books":
            actual_encoding = "utf-16" if data[:2] in (b"\xff\xfe", b"\xfe\xff") else encoding
            text = data.decode(actual_encoding).lstrip("\ufeff")
            if "\x00" in text:
                raise ValueError("Embedded NUL; check input encoding: %s" % source)
            data = text.replace("\r\n", "\n").replace("\r", "\n").encode("utf-8")
            if not data:
                raise ValueError("Empty novel: %s" % source)
        if target.exists() and not overwrite and target.read_bytes() != data:
            raise ValueError("Destination exists; use --overwrite: %s" % target)
        if source.resolve() == target.resolve():
            raise ValueError("Use a separate output folder: %s" % target)
        planned[normalized] = (target, data)
    # Validate the entire plan before the first destination write.
    for kind in ("music", "books"):
        (output / kind).mkdir(parents=True, exist_ok=True)
    for target, data in planned.values():
        target.write_bytes(data)
    return [str(target) for target, _ in planned.values()]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", required=True)
    p.add_argument("--music", nargs="*", default=[])
    p.add_argument("--books", nargs="*", default=[])
    p.add_argument("--encoding", default="utf-8", help="Source TXT encoding, e.g. gb18030; output is UTF-8")
    p.add_argument("--overwrite", action="store_true")
    args = p.parse_args()
    try:
        paths = prepare(args.output, args.music, args.books, args.encoding, args.overwrite)
    except (ValueError, UnicodeError, OSError) as exc:
        p.error(str(exc))
    print("Prepared %d files in %s" % (len(paths), args.output))

if __name__ == "__main__":
    main()
