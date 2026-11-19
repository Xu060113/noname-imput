#pragma once
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <cwchar>
#include <string>
#include <vector>

struct LengthLimits {
  std::uint32_t answer = 80;
  std::uint32_t continuation = 24;
  static constexpr unsigned minimum = 8, maximum = 300;
  static bool valid(unsigned value) {
    return value >= minimum && value <= maximum;
  }
  unsigned forMode(const std::string &mode) const {
    return mode == "answer" ? answer : continuation;
  }
};

// Count Unicode scalar values. The initial answer line break is inserted by the
// host separately. Spaces between accepted English words are part of the text.
inline size_t characterCount(const std::wstring &text) {
  size_t count = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] >= 0xD800 && text[i] <= 0xDBFF && i + 1 < text.size() &&
        text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
      ++i;
    ++count;
  }
  return count;
}

inline std::wstring spaced(std::wstring word, const std::wstring &previous) {
  if (previous.empty() || word.empty())
    return word;
  auto first = word.find_first_not_of(L' ');
  if (first == std::wstring::npos)
    return word;
  auto letter = [](wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
  };
  auto last = previous.back();
  if (wcschr(L",.!?;:)]’'", word[first]) || iswspace(last))
    return word.substr(first);
  bool punctuation = wcschr(L",.!?;:", last) && previous.size() > 1 &&
                     letter(previous[previous.size() - 2]);
  if (letter(word[first]) && (letter(last) || punctuation))
    return L" " + word.substr(first);
  return word;
}

struct CandidateRow {
  std::vector<std::wstring> words;
  bool firstIsBranch = false;
};

// Text stores expose paragraph breaks as CRLF, CR, or LF. Compare the same
// logical suffix even when these representations have different UTF-16 lengths.
inline std::wstring caretSuffix(const std::wstring &text) {
  std::wstring suffix;
  for (size_t end = text.size(); end && suffix.size() < 16;) {
    wchar_t c = text[--end];
    if (c == L'\n' && end && text[end - 1] == L'\r')
      --end;
    suffix.push_back(c == L'\r' ? L'\n' : c);
  }
  return suffix;
}

inline bool matchesCaretText(const std::wstring &prior, const std::wstring &actual) {
  auto expected = caretSuffix(prior);
  return caretSuffix(actual).compare(0, expected.size(), expected) == 0;
}

// Rows supplied by the model assume its first choice was accepted. If that
// choice cannot fit, the remaining alternatives each start a fresh branch.
class PredictionBudget {
  std::wstring preceding;
  std::wstring lastWord;
  size_t remaining;

public:
  PredictionBudget(unsigned limit, const std::wstring &input,
                   const std::wstring &accepted, const std::string &mode,
                   const std::wstring &lastAccepted = L"")
      : preceding(accepted.empty() && mode == "continuation" ? input
                                                             : accepted),
        lastWord(accepted.empty() && mode == "continuation" ? input : lastAccepted),
        remaining(limit - std::min<size_t>(limit, characterCount(accepted))) {}

  bool canContinue() const { return remaining > 0; }

  CandidateRow next(const std::vector<std::wstring> &words) {
    CandidateRow row;
    std::wstring preferred;
    bool echoedFirst = false;
    for (size_t i = 0; i < words.size(); ++i) {
      if (words[i] == lastWord && characterCount(lastWord) > 1) {
        if (i == 0)
          echoedFirst = true;
        continue;
      }
      auto insertion = spaced(words[i], preceding);
      if (insertion.empty() || characterCount(insertion) > remaining)
        continue;
      if (i == 0)
        preferred = insertion;
      row.words.push_back(words[i]);
    }
    // An echo-only row consumes no budget. The next row of this stream
    // continues the already visible word. Other choices need a fresh branch.
    if (echoedFirst && row.words.empty())
      return row;
    row.firstIsBranch = preferred.empty();
    if (row.firstIsBranch)
      remaining = 0;
    else {
      remaining -= characterCount(preferred);
      preceding += preferred;
      lastWord = words.front();
    }
    return row;
  }
};
