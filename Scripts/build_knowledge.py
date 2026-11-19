"""Phase 1: turn the course PDFs into the single knowledge file the Windows IME reads in file mode.

Inputs:  %LOCALAPPDATA%\\Yuju\\knowledge_folders.txt  (one folder per line, lines starting with # ignored)
Outputs: %LOCALAPPDATA%\\Yuju\\knowledge.txt          (all text, fixed order, one marker per page)
         %LOCALAPPDATA%\\Yuju\\knowledge_report.txt   (per-file pages and characters, skipped pages/files)

Run it in your own terminal (not from inside the Claude desktop app, whose AppData is redirected):
  First time, saving the folder list:
    uv run --with pymupdf python -X utf8 Scripts/build_knowledge.py --set-folders "<folder>" "<folder>"
  After files change:
    uv run --with pymupdf python -X utf8 Scripts/build_knowledge.py
Options: --out-dir <dir> writes all three files to <dir> instead (used for testing).

The output text must stay byte-identical for unchanged inputs: the IME sends it at the start of every
file-mode request, and DeepSeek only reuses its cache when that start is exactly the same.
"""
import hashlib
import os
import re
import sys
import time
from pathlib import Path

import pymupdf

MAX_CHARS = 3_000_000  # about 750k tokens of English text; deepseek-flash accepts 1M including the answer


def build_knowledge(folders):
    """Return (pages per file stem, knowledge text, report rows, skipped files).

    PDFs from all folders, sorted by full path ignoring case, whitespace compacted,
    each non-empty page preceded by [File: name.pdf | Page n]."""
    parts, pages, report, skipped = [], {}, [], []
    files = sorted((p for f in folders for p in f.rglob("*") if p.is_file()), key=lambda p: str(p).lower())
    for path in files:
        if path.suffix.lower() != ".pdf":
            skipped.append((path, "not a PDF"))
            continue
        try:
            with pymupdf.open(path) as doc:
                chars, empty = 0, []
                for number, page in enumerate(doc, 1):
                    lines = (re.sub(r"[ \t ]+", " ", line).strip() for line in page.get_text("text").splitlines())
                    text = "\n".join(line for line in lines if line)
                    if text:
                        parts.append(f"[File: {path.name} | Page {number}]\n{text}")
                        chars += len(text)
                    if len(text) < 20:
                        empty.append(number)
                pages[path.stem] = doc.page_count
                report.append((path, doc.page_count, chars, empty))
        except Exception as e:  # encrypted or damaged PDF: leave it out, say so in the report
            skipped.append((path, f"cannot read: {type(e).__name__}: {e}"))
    return pages, "\n".join(parts), report, skipped


def data_dir():
    base = os.environ.get("LOCALAPPDATA")
    if not base:
        raise SystemExit("LOCALAPPDATA is not set; run this on Windows or pass --out-dir.")
    return Path(base) / "Yuju"


def write_atomically(path, text):
    """Write next to the target and swap it in, so the IME never reads a half-written file."""
    temp = path.with_name(path.name + ".tmp")
    temp.write_text(text, encoding="utf-8", newline="\n")
    os.replace(temp, path)


def read_folders(config):
    if not config.exists():
        raise SystemExit(f"No folder list at {config}.\nRun once with --set-folders \"<folder>\" ... to create it.")
    lines = config.read_text(encoding="utf-8-sig").splitlines()
    return [Path(line.strip().strip('"')) for line in lines if line.strip() and not line.lstrip().startswith("#")]


def main(argv):
    out_dir, new_folders = None, None
    args = list(argv)
    if "--out-dir" in args:
        i = args.index("--out-dir")
        out_dir = Path(args[i + 1])
        del args[i:i + 2]
    if "--set-folders" in args:
        i = args.index("--set-folders")
        new_folders = [Path(a) for a in args[i + 1:]]
        del args[i:]
        if not new_folders:
            raise SystemExit("--set-folders needs at least one folder.")
    if args:
        raise SystemExit(f"Unknown arguments: {args}")

    target = out_dir or data_dir()
    target.mkdir(parents=True, exist_ok=True)
    config = target / "knowledge_folders.txt"
    folders = new_folders if new_folders is not None else read_folders(config)
    missing = [f for f in folders if not f.is_dir()]
    if missing:
        raise SystemExit("These folders do not exist, nothing was changed:\n" + "\n".join(map(str, missing)))
    if new_folders is not None:
        write_atomically(config, "# One folder per line. Every PDF inside (including subfolders) is used.\n"
                         + "".join(f"{f}\n" for f in folders))

    pages, knowledge, report, skipped = build_knowledge(folders)
    if not knowledge:
        raise SystemExit("No text was extracted from these folders; the knowledge file was left unchanged.")
    if len(knowledge) > MAX_CHARS:
        raise SystemExit(f"Extracted {len(knowledge)} characters, over the {MAX_CHARS} limit; "
                         "the knowledge file was left unchanged. Remove some files from the folders.")
    duplicates = sorted({p.name for p, *_ in report if sum(q.name == p.name for q, *_ in report) > 1})

    digest = hashlib.sha256(knowledge.encode("utf-8")).hexdigest()
    words = len(re.findall(r"[A-Za-z]+|\d+", knowledge))
    lines = [f"Generated {time.strftime('%Y-%m-%d %H:%M:%S')}",
             f"Files used: {len(report)}, pages: {sum(r[1] for r in report)}, characters: {len(knowledge)}",
             f"Estimated tokens: {round(words * 1.6)} (phase 0 measured about 1.6 per word; DeepSeek usage has the real count)",
             f"SHA-256: {digest}", "", "Folders:"] + [f"  {f}" for f in folders] + ["", "Files (pages, characters, pages with little or no text):"]
    lines += [f"  {p.name}: {n} pages, {c} chars" + (f", little/no text on pages {e}" if e else "") for p, n, c, e in report]
    if skipped:
        lines += ["", "Skipped:"] + [f"  {p}: {why}" for p, why in skipped]
    if duplicates:
        lines += ["", "Warning: these file names appear more than once, so their sources look the same: "
                  + ", ".join(duplicates)]

    write_atomically(target / "knowledge.txt", knowledge)
    write_atomically(target / "knowledge_report.txt", "\n".join(lines) + "\n")
    print("\n".join(lines[:4]))
    if skipped:
        print(f"Skipped {len(skipped)} file(s); see the report.")
    if duplicates:
        print("Warning: duplicate file names: " + ", ".join(duplicates))
    print(f"Written to {target}")
    print("OK")


if __name__ == "__main__":
    main(sys.argv[1:])
