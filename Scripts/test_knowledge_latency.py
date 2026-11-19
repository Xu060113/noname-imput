"""Phase 0: measure DeepSeek latency, cache hits and answer quality when every request carries the course files.

The request mirrors what the Windows IME will send in file mode. The system message starts
with all course text (identical every time, so that long prefix hits the cache even when the
rules below it change), followed by the answering rules; the user's question comes last.
Candidate rows are validated with the same rules as Windows/model.hpp, so a protocol break
here would also break the IME. Each answer is then checked automatically.

Usage (in your own terminal, from the repository root):
  $env:DEEPSEEK_API_KEY = "<your key>"
  uv run --with pymupdf python -X utf8 Scripts/test_knowledge_latency.py
Offline self-check, sends nothing:
  uv run --with pymupdf python -X utf8 Scripts/test_knowledge_latency.py --dry-run
"""
import hashlib
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

from build_knowledge import build_knowledge as build_knowledge_file, data_dir, read_folders

ROOT = Path(__file__).resolve().parent.parent
REPORT = ROOT / ".build/phase0_latency_report.json"  # holds quotes from your files; not committed
# Same folder list the IME's knowledge file is built from (Scripts/build_knowledge.py).
FOLDERS = read_folders(data_dir() / "knowledge_folders.txt")
NOT_FOUND = "Not found in your course files."


def load_questions():
    """Questions about your own files stay in Scripts/test_questions.local.json (ignored by git);
    copy Scripts/test_questions.example.json there and fill in terms, files and pages.
    Each entry: label, question, and for answerable questions the expected source file
    (without .pdf), page and phrases the answer must contain."""
    local = ROOT / "Scripts/test_questions.local.json"
    path = local if local.exists() else ROOT / "Scripts/test_questions.example.json"
    rows = json.loads(path.read_text(encoding="utf-8"))
    return [(q["label"], q["question"], q.get("source"), q.get("page"), q.get("phrases")) for q in rows]


QUESTIONS = load_questions()
ENDPOINT = "https://api.deepseek.com/chat/completions"
MAX_SENTENCES = 6
# Measured ~6 tokens per candidate row and ~13 rows per sentence, so 6 sentences plus the
# source need roughly 500-800 tokens; the cap still stops runaway repetition.
MAX_TOKENS = 900

# The file-mode rules live in Windows/file_rules.txt, which the installer copies next to the IME,
# so this test and the IME always send the same text.
RULES = (ROOT / "Windows/file_rules.txt").read_text(encoding="utf-8").replace("\r\n", "\n")
assert NOT_FOUND in RULES, "file_rules.txt must contain the not-found reply"


def build_knowledge(folders):
    """Same text the IME will send: produced by Scripts/build_knowledge.py."""
    pages, knowledge, _, _ = build_knowledge_file(folders)
    return pages, knowledge


def system_message(knowledge):
    return "<course_files>\n" + knowledge + "\n</course_files>\n" + RULES


def request_body(system, question):
    user = json.dumps({"input": question, "accepted": "", "mode": "answer"}, ensure_ascii=False)
    return {
        "model": "deepseek-flash", "stream": True, "thinking": {"type": "disabled"},
        "stream_options": {"include_usage": True},
        "max_tokens": MAX_TOKENS, "temperature": 0.45,
        "messages": [{"role": "system", "content": system}, {"role": "user", "content": user}],
    }


class Rows:
    """Same acceptance rules as runModel in Windows/model.hpp, plus the change planned for file mode:
    when the request already fixes the mode, a missing mode line is tolerated (phase 0 round 4 saw
    the model start a not-found reply directly with a word row)."""

    def __init__(self, requested_mode=None):
        self.buffer, self.mode_seen, self.ended = "", False, False
        self.rows, self.error = [], None
        self.requested_mode = requested_mode

    def feed(self, fragment):
        self.buffer += fragment
        new_rows = 0
        while "\n" in self.buffer and not self.error:
            record, self.buffer = self.buffer.split("\n", 1)
            if not record.strip(" \t\r"):
                continue
            try:
                frame = json.loads(record)
            except json.JSONDecodeError:
                self.error = f"not JSON: {record[:80]!r}"
                break
            if not self.mode_seen and self.requested_mode and isinstance(frame, list):
                self.mode_seen = True  # no mode line, but the request already said which mode; read this row below
            if not self.mode_seen:
                if not isinstance(frame, dict) or frame.get("mode") not in ("answer", "continuation"):
                    self.error = f"first line is not a mode line: {record[:80]!r}"
                    break
                self.mode_seen = True
            elif isinstance(frame, list) and 1 <= len(frame) <= 3:
                words = []
                for w in frame:
                    if not isinstance(w, str) or not w or len(w) > 32 or any(c in w for c in "\r\n\t\0"):
                        self.error = f"invalid candidate: {w!r}"
                        break
                    if w not in words:
                        words.append(w)
                if self.error:
                    break
                self.rows.append(words)
                new_rows += 1
            elif isinstance(frame, dict) and frame.get("end") is True:
                self.ended = True
            else:
                self.error = f"unexpected line: {record[:80]!r}"
        return new_rows


def join_words(words):
    """Same rule as spaced() in Windows/common.hpp, applied word by word."""
    def letter(c):
        return c.isascii() and c.isalpha()

    def alnum(c):
        return c.isascii() and c.isalnum()

    text = ""
    for word in words:
        stripped = word.lstrip(" ")
        if not text or not stripped:
            text += word
            continue
        last, head = text[-1], stripped[0]
        before = text[-2] if len(text) > 1 else ""
        if head in ",.!?;:)]’'”" or last.isspace():
            text += stripped
        elif (alnum(head) or head in "(“") and (
                alnum(last) or last in ")”" or last in ",;:!?"
                or (last == "." and (letter(before) or before in (")", "”")))):
            text += " " + stripped
        else:
            text += word
    return text


SOURCE = re.compile(r"\(Source:\s*(.+?),\s*p\.\s*(\d+)\)\s*$")


def norm(name):
    return re.sub(r"\s+", "", name).lower()


def check(answer, pages, expected_file, expected_page, phrase):
    """Return a list of problems; empty means the answer passes."""
    problems = []
    if expected_file is None:
        if answer.strip() != NOT_FOUND:
            problems.append(f"should be exactly {NOT_FOUND!r}")
        return problems
    match = SOURCE.search(answer)
    if not match:
        problems.append("no source at the end")
        body = answer
    else:
        body = answer[:match.start()].strip()
        file, page = match.group(1).strip(), int(match.group(2))
        known = {norm(stem): (stem, count) for stem, count in pages.items()}
        if norm(file) not in known:
            problems.append(f"source file does not exist: {file!r}")
        elif page > known[norm(file)][1]:
            problems.append(f"source page {page} beyond {known[norm(file)][1]} pages")
        if norm(file) != norm(expected_file) or page != expected_page:
            problems.append(f"source should be {expected_file}, p.{expected_page}")
    if not body or body[-1] not in ".!?":
        problems.append("last sentence has no ending punctuation")
    sentences = [s for s in re.split(r"(?<=[.!?])\s+", body) if s]
    if len(sentences) > MAX_SENTENCES:
        problems.append(f"{len(sentences)} sentences, limit is {MAX_SENTENCES}")
    if len(set(sentences)) < len(sentences):
        problems.append("a sentence is repeated")
    for p in ((phrase,) if isinstance(phrase, str) else phrase or ()):
        if p.lower() not in body.lower():
            problems.append(f"missing {p!r}")
    return problems


def ask(key, system, label, question):
    body = json.dumps(request_body(system, question), ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(ENDPOINT, data=body, method="POST", headers={
        "Content-Type": "application/json", "Authorization": "Bearer " + key})
    result = {"label": label, "question": question, "headers_ms": None, "first_row_ms": None,
              "total_ms": None, "usage": None, "answer": "", "rows": [], "ended": False,
              "error": None, "problems": []}
    rows = Rows("answer")
    raw_text = []
    start = time.perf_counter()
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            result["headers_ms"] = round((time.perf_counter() - start) * 1000)
            for raw in response:
                line = raw.decode("utf-8").strip()
                if not line.startswith("data:"):
                    continue
                data = line[5:].strip()
                if data == "[DONE]":
                    break
                packet = json.loads(data)
                if packet.get("usage"):
                    result["usage"] = packet["usage"]
                for choice in packet.get("choices") or []:
                    content = (choice.get("delta") or {}).get("content")
                    if isinstance(content, str):
                        raw_text.append(content)
                    if isinstance(content, str) and rows.feed(content) and result["first_row_ms"] is None:
                        result["first_row_ms"] = round((time.perf_counter() - start) * 1000)
                    if choice.get("finish_reason"):
                        result["finish_reason"] = choice["finish_reason"]
        rows.feed("\n")
    except urllib.error.HTTPError as e:
        result["error"] = f"HTTP {e.code}: {e.read().decode('utf-8', 'replace')[:300]}"
    except Exception as e:  # network errors, timeouts, bad JSON in the stream
        result["error"] = f"{type(e).__name__}: {e}"
    result["total_ms"] = round((time.perf_counter() - start) * 1000)
    result["rows"], result["ended"] = rows.rows, rows.ended
    result["answer"] = join_words([r[0] for r in rows.rows])
    if rows.error and not result["error"]:
        result["error"] = "protocol: " + rows.error
    if not rows.ended and not result["error"]:
        result["error"] = "stream ended without {\"end\":true}"
    extra = sum(1 for r in rows.rows if len(r) > 1)
    if extra and not result["error"]:
        result["error"] = f"{extra} rows carry alternative words; file mode allows one word per row"
    if result["error"]:
        result["raw_output"] = "".join(raw_text)[:4000]  # the model's own text, to see what broke
    return result


def self_check():
    rows = Rows()
    rows.feed('{"mode":"answer"}\n["Phishing","It"]\n["is"]\n["fraud",')
    rows.feed('"a scam"]\n["."]\n{"end":true}\n')
    assert rows.error is None and rows.ended and len(rows.rows) == 4, (rows.error, rows.rows)
    assert join_words([r[0] for r in rows.rows]) == "Phishing is fraud."
    bad = Rows()
    bad.feed('{"mode":"answer"}\n["a","b","c","d"]\n')
    assert bad.error, "four candidates must be rejected"
    lenient = Rows("answer")  # round 4: reply started without the mode line
    lenient.feed('["Not"]\n["found"]\n["in"]\n["your"]\n["course"]\n["files."]\n{"end":true}\n')
    assert lenient.error is None and lenient.ended and join_words([r[0] for r in lenient.rows]) == NOT_FOUND
    strict = Rows()  # without a requested mode the mode line is still required
    strict.feed('["Not"]\n')
    assert strict.error
    garbage = Rows("answer")  # tolerance covers only a missing mode line, not other junk
    garbage.feed('Not found in your course files.\n')
    assert garbage.error
    cases = {
        ("information", "(Source:", "CS101", "Lecture04", "Computer", "Crime,", "p.22)"):
            "information (Source: CS101 Lecture04 Computer Crime, p.22)",
        ("Week", "5"): "Week 5",
        ("hello", "123"): "hello 123",
        ("purposes", ".", "The"): "purposes. The",
        ("tool", ".", "(Source:", "Week5_Lecture,", "p.12)"): "tool. (Source: Week5_Lecture, p.12)",
        ("我", "今天"): "我今天",
        ("(Source:", "Tutorial03_笔记,", "p.5)"): "(Source: Tutorial03_笔记, p.5)",
        ("他说", "。", "OK"): "他说。OK",
        ("sense:", "“social", "engineering”", ".", "Instead"): "sense: “social engineering”. Instead",
        ("engineering”", ".", "(Source:"): "engineering”. (Source:",
        ("3", ".", "5"): "3.5",
        ("what", "is", "x", "?"): "what is x?",
        ("hello", "("): "hello (",
    }
    assert len(NOT_FOUND) <= 32, "the not-found reply must fit in one candidate"
    for words, expected in cases.items():
        assert join_words(words) == expected, (words, join_words(words))
    pages = {"CS101 Lecture04 Computer Crime": 60, "Tutorial04_笔记": 13}
    good = "Phishing is online fraud. (Source: CS101 Lecture04 Computer Crime, p.22)"
    assert check(good, pages, "CS101 Lecture04 Computer Crime", 22, "fraud") == []
    wrong = check("Phishing is online characterized (Source: Tutorial04_笔记, p.22)",
                  pages, "CS101 Lecture04 Computer Crime", 22, "fraud")
    assert any("beyond" in p for p in wrong) and any("should be" in p for p in wrong)
    assert any("punctuation" in p for p in wrong) and any("fraud" in p for p in wrong), wrong
    assert check("A. B. A. (Source: Tutorial04_笔记, p.2)", pages, "Tutorial04_笔记", 2, None)
    six = "A. B. C. D. E. F. (Source: Tutorial04_笔记, p.2)"
    assert check(six, pages, "Tutorial04_笔记", 2, None) == []
    assert check(six.replace("F.", "F. G."), pages, "Tutorial04_笔记", 2, None)
    two = "X is psychological manipulation. (Source: Tutorial04_笔记, p.2)"
    assert check(two, pages, "Tutorial04_笔记", 2, ("psychological manipulation", "product"))
    assert check(NOT_FOUND, pages, None, None, None) == []
    assert check("A DSN is not defined. " + NOT_FOUND, pages, None, None, None)


def main():
    dry = "--dry-run" in sys.argv
    self_check()
    pages, knowledge = build_knowledge(FOLDERS)
    system = system_message(knowledge)
    prefix = "<course_files>\n" + knowledge + "\n</course_files>\n"
    words = len(re.findall(r"[A-Za-z]+(?:['’-][A-Za-z]+)*|\d+", knowledge))
    body_bytes = len(json.dumps(request_body(system, QUESTIONS[0][1]), ensure_ascii=False).encode("utf-8"))
    print(f"files={len(pages)} knowledge_chars={len(knowledge)} english_words={words} request_bytes={body_bytes}")
    print(f"files_prefix_sha256={hashlib.sha256(prefix.encode('utf-8')).hexdigest()[:16]} "
          f"rules_sha256={hashlib.sha256(RULES.encode('utf-8')).hexdigest()[:16]}")
    for _, _, stem, _, _ in QUESTIONS:
        if stem is not None and stem not in pages:
            raise SystemExit(f"Expected source {stem!r} is not among your PDFs. "
                             "Edit Scripts/test_questions.local.json (see load_questions).")
    if dry:
        print("dry run: self-check passed, nothing sent. OK")
        return
    key = os.environ.get("DEEPSEEK_API_KEY", "").strip()
    if not key:
        raise SystemExit("Set DEEPSEEK_API_KEY in this terminal first.")
    results = []
    for i, (label, question, stem, page, phrase) in enumerate(QUESTIONS):
        if i:
            time.sleep(3)  # give the cache a moment to persist the shared prefix
        r = ask(key, system, label, question)
        if not r["error"]:
            r["problems"] = check(r["answer"], pages, stem, page, phrase)
        results.append(r)
        u = r["usage"] or {}
        print(f"\n[{i + 1}] {label}: {question}")
        print(f"    first candidate {r['first_row_ms']} ms | total {r['total_ms']} ms | "
              f"cache hit {u.get('prompt_cache_hit_tokens')} / prompt {u.get('prompt_tokens')} | "
              f"output {u.get('completion_tokens')} | finish {r.get('finish_reason')}")
        print(f"    answer: {r['answer'] or '(none)'}")
        if r["error"]:
            print(f"    ERROR: {r['error']}")
        print("    check: " + ("PASS" if not r["error"] and not r["problems"] else "; ".join(r["problems"]) or "FAIL"))
    REPORT.parent.mkdir(exist_ok=True)
    REPORT.write_text(json.dumps({"time": time.strftime("%Y-%m-%d %H:%M:%S"),
                                  "rules_sha256": hashlib.sha256(RULES.encode("utf-8")).hexdigest()[:16],
                                  "results": results}, ensure_ascii=False, indent=1), encoding="utf-8")
    passed = sum(1 for r in results if not r["error"] and not r["problems"])
    print(f"\nreport written to {REPORT}")
    print(f"{passed}/{len(results)} answers passed")
    print("OK" if passed == len(results) else "FINISHED WITH PROBLEMS")


if __name__ == "__main__":
    main()
