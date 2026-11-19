// Checks for the text helpers in common.hpp. Scripts/build_windows.py builds and runs
// this before packaging; the spacing cases match self_check() in
// Scripts/test_knowledge_latency.py so the IME and the phase 0 test join words alike.
#include "common.hpp"
#include <cstdio>

static int failures = 0;
static void expect(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}
static std::wstring join(std::initializer_list<const wchar_t *> words) {
  std::wstring text;
  for (auto w : words)
    text += spaced(w, text);
  return text;
}

int main() {
  expect(join({L"Phishing", L"is", L"fraud", L"."}) == L"Phishing is fraud.", "plain sentence");
  expect(join({L"information", L"(Source:", L"CS101", L"Lecture04", L"Computer", L"Crime,",
               L"p.22)"}) == L"information (Source: CS101 Lecture04 Computer Crime, p.22)",
         "source after a word");
  expect(join({L"Week", L"5"}) == L"Week 5", "digit after a word");
  expect(join({L"hello", L"123"}) == L"hello 123", "number after a word");
  expect(join({L"purposes", L".", L"The"}) == L"purposes. The", "after a period");
  expect(join({L"tool", L".", L"(Source:", L"Week5_Lecture,", L"p.12)"}) ==
             L"tool. (Source: Week5_Lecture, p.12)",
         "source after a period");
  expect(join({L"我", L"今天"}) == L"我今天", "no spaces in Chinese");
  expect(join({L"(Source:", L"Tutorial03_笔记,", L"p.5)"}) ==
             L"(Source: Tutorial03_笔记, p.5)",
         "space after a comma that follows Chinese");
  expect(join({L"他说", L"。", L"OK"}) == L"他说。OK", "no space after a Chinese period");
  expect(join({L"sense:", L"“social", L"engineering”", L".", L"Instead"}) ==
             L"sense: “social engineering”. Instead",
         "curly quotes");
  expect(join({L"engineering”", L".", L"(Source:"}) == L"engineering”. (Source:",
         "source after a closing quote");
  expect(join({L"3", L".", L"5"}) == L"3.5", "decimal number");
  expect(join({L"what", L"is", L"x", L"?"}) == L"what is x?", "question mark");
  expect(join({L"hello", L"("}) == L"hello (", "opening parenthesis");
  expect(join({L"don", L"'t"}) == L"don't", "apostrophe");

  expect(currentSentence(L"what is phishing?") == L"what is phishing", "sentence without ?");
  expect(currentSentence(L"我今天想吃饭。what is x?") == L"what is x", "after a Chinese period");
  expect(currentSentence(L"First. Second one ?") == L"Second one", "after an English period");
  expect(currentSentence(L"line one\r\nline two") == L"line two", "after a line break");
  expect(currentSentence(L"。") == L"", "only an end mark");
  expect(currentSentence(L"") == L"", "empty text");
  expect(hasHan(L"Lecture 4 讲了什么"), "mixed text is Chinese");
  expect(!hasHan(L"What is a DSN？"), "full-width ? alone is not Chinese");
  expect(endsWithQuestionMark(L"what is x? "), "half-width ? with a trailing space");
  expect(endsWithQuestionMark(L"what is x？"), "full-width ?");
  expect(!endsWithQuestionMark(L"what is x."), "period");
  std::printf(failures ? "%d check(s) failed\n" : "common_test OK\n", failures);
  return failures ? 1 : 0;
}
