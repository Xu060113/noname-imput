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
