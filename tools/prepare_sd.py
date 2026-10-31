"""Preserve media folders and companions in an SD staging directory."""
import argparse
import pathlib

def gather(paths, suffixes):
    files = []
    for value in paths:
        path = pathlib.Path(value)
        if not path.exists():
            raise ValueError("Input does not exist: %s" % path)
        candidates = sorted(path.rglob("*")) if path.is_dir() else [path]
        for candidate in candidates:
            if candidate.is_file() and candidate.suffix.lower() in suffixes:
                relative = pathlib.Path(path.name) / candidate.relative_to(path) if path.is_dir() else pathlib.Path(candidate.name)
                files.append((candidate, relative))
        # Selecting a single song also imports its adjacent lyrics and cover.
        if path.is_file() and path.suffix.lower() == ".mp3":
            for sibling in path.parent.iterdir():
                if sibling.is_file() and sibling.suffix.lower() in (".lrc", ".jpg", ".jpeg", ".png") and (sibling.stem.casefold() == path.stem.casefold() or sibling.stem.casefold() in ("cover", "folder")):
                    files.append((sibling, pathlib.Path(sibling.name)))
    return files

def prepare(output, music=(), books=(), encoding="utf-8", overwrite=False):
    output = pathlib.Path(output)
    inputs = [("music", path, relative) for path, relative in gather(music, (".mp3", ".lrc", ".jpg", ".jpeg", ".png"))]
    inputs += [("books", path, relative) for path, relative in gather(books, (".txt",))]
    for kind in ("music", "books"):
        if len({relative.as_posix().casefold() for k, path, relative in inputs if k == kind and path.suffix.lower() in (".mp3", ".txt")}) > 128:
            raise ValueError("Firmware supports at most 128 files per library")
    planned = {}
    sources = {}
    for kind, source, relative in inputs:
        target = output / kind / relative
        if len(relative.parts) > 4:
            raise ValueError("Firmware supports at most 3 folder levels: %s" % relative)
        if len(("/" + kind + "/" + relative.as_posix()).encode("utf-8")) >= 192:
            raise ValueError("Path is too long: %s" % source.name)
        normalized = str(target).casefold()
        if normalized in planned:
            if sources[normalized] == source.resolve():
                continue
            raise ValueError("Duplicate destination filename: %s" % source.name)
        data = source.read_bytes()
        if not data:
            raise ValueError("Empty media file: %s" % source)
        if source.suffix.lower() in (".txt", ".lrc"):
            actual_encoding = "utf-16" if data[:2] in (b"\xff\xfe", b"\xfe\xff") else encoding
            text = data.decode(actual_encoding).lstrip("\ufeff")
            if "\x00" in text:
                raise ValueError("Embedded NUL; check input encoding: %s" % source)
            data = text.replace("\r\n", "\n").replace("\r", "\n").encode("utf-8")
            if not text.strip():
                raise ValueError("Empty novel: %s" % source)
        if source.suffix.lower() == ".lrc" and len(data) > 16384:
            raise ValueError("Lyrics must be at most 16KB: %s" % source)
        if source.suffix.lower() in (".jpg", ".jpeg", ".png") and len(data) > 49152:
            raise ValueError("Cover must be at most 48KB; resize it first: %s" % source)
        if target.exists() and not overwrite and target.read_bytes() != data:
            raise ValueError("Destination exists; use --overwrite: %s" % target)
        if source.resolve() == target.resolve():
            raise ValueError("Use a separate output folder: %s" % target)
        planned[normalized] = (target, data)
        sources[normalized] = source.resolve()
    # Validate the entire plan before the first destination write.
    for kind in ("music", "books"):
        (output / kind).mkdir(parents=True, exist_ok=True)
    for target, data in planned.values():
        target.parent.mkdir(parents=True, exist_ok=True)
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
