#pragma once
#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <algorithm>
#include <string>
#include <vector>
#include <windows.h>
#include <wincred.h>

inline std::wstring wide(const std::string &s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                              (int)s.size(), nullptr, 0);
  std::wstring result(n, L'\0');
  if (n)
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), result.data(), n);
  return result;
}
inline std::string utf8(const std::wstring &s) {
  if (s.empty())
    return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0,
                              nullptr, nullptr);
  std::string result(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), result.data(), n,
                      nullptr, nullptr);
  return result;
}
inline const wchar_t *credentialName = L"Yuju/DeepSeek";
inline std::string readKey() {
  PCREDENTIALW value = nullptr;
  if (!CredReadW(credentialName, CRED_TYPE_GENERIC, 0, &value))
    return {};
  std::string text((char *)value->CredentialBlob, value->CredentialBlobSize);
  CredFree(value);
  return text;
}
inline bool aiEnabled() {
  DWORD value = 0, bytes = sizeof(value);
  return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Yuju", L"AIEnabled",
                      RRF_RT_REG_DWORD, nullptr, &value,
                      &bytes) == ERROR_SUCCESS &&
         value == 1;
}
// Joins an English word to the text before it. ASCII letters and digits are word
// characters; "(" and an opening curly quote start a word too. A half-width , ; : ! ?
// is always followed by a space; a period only after a letter, ")" or a closing
// quote, so "3" "." "5" stays "3.5". Chinese text never gets spaces.
// Scripts/test_knowledge_latency.py join_words() mirrors this rule.
inline std::wstring spaced(std::wstring word, const std::wstring &previous) {
  if (previous.empty() || word.empty())
    return word;
  auto first = word.find_first_not_of(L' ');
  if (first == std::wstring::npos)
    return word;
  auto letter = [](wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
  };
  auto alnum = [&](wchar_t c) { return letter(c) || (c >= L'0' && c <= L'9'); };
  auto last = previous.back();
  wchar_t head = word[first];
  if (wcschr(L",.!?;:)]’'”", head) || iswspace(last))
    return word.substr(first);
  wchar_t before = previous.size() > 1 ? previous[previous.size() - 2] : 0;
  bool afterWord = alnum(last) || last == L')' || last == L'”';
  bool afterMark = wcschr(L",;:!?", last) != nullptr;
  bool afterPeriod =
      last == L'.' && (letter(before) || before == L')' || before == L'”');
  if ((alnum(head) || head == L'(' || head == L'“') &&
      (afterWord || afterMark || afterPeriod))
    return L" " + word.substr(first);
  return word;
}
inline bool isHan(wchar_t c) {
  return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) ||
         (c >= 0xF900 && c <= 0xFAFF);
}
// A sentence counts as Chinese as soon as it holds one Chinese character.
inline bool hasHan(const std::wstring &s) {
  return std::any_of(s.begin(), s.end(), isHan);
}
// The sentence the caret is in: text after the previous 。！？.!? or line break,
// without the end marks and spaces the text itself finishes with.
inline std::wstring currentSentence(const std::wstring &text) {
  const wchar_t *ends = L"。！？.!?\r\n";
  auto isEnd = [&](wchar_t c) { return c && wcschr(ends, c); };
  size_t stop = text.size();
  while (stop && (isEnd(text[stop - 1]) || text[stop - 1] == L' '))
    --stop;
  size_t start = stop;
  while (start && !isEnd(text[start - 1]))
    --start;
  while (start < stop && text[start] == L' ')
    ++start;
  return text.substr(start, stop - start);
}
inline bool endsWithQuestionMark(const std::wstring &text) {
  auto last = text.find_last_not_of(L' ');
  return last != std::wstring::npos &&
         (text[last] == L'?' || text[last] == L'？');
}
inline std::wstring moduleDir(HMODULE module) {
  wchar_t path[32768];
  DWORD length = GetModuleFileNameW(module, path, 32768);
  std::wstring result(path, length);
  return result.substr(0, result.find_last_of(L"\\/"));
}
template <class T> inline void release(T *&value) {
  if (value)
    value->Release();
  value = nullptr;
}
