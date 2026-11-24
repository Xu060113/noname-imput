#pragma once
#define UNICODE
#define _UNICODE
#define NOMINMAX
#include "length.hpp"
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
inline LengthLimits readLengthLimits() {
  LengthLimits limits;
  DWORD pair[2]{};
  DWORD bytes = sizeof(pair);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Yuju", L"PredictionLengths",
                   RRF_RT_REG_BINARY, nullptr, pair, &bytes) == ERROR_SUCCESS &&
      bytes == sizeof(pair) && LengthLimits::valid(pair[0]) &&
      LengthLimits::valid(pair[1])) {
    limits = {pair[0], pair[1]};
  }
  return limits;
}

inline bool useCaretContext() {
  DWORD value = 0, bytes = sizeof(value);
  return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Yuju", L"CaretContext",
                      RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS && value == 1;
}

inline bool writeLengthLimits(const LengthLimits &limits) {
  const DWORD pair[] = {limits.answer, limits.continuation};
  return RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Yuju",
                         L"PredictionLengths", REG_BINARY, pair,
                         sizeof(pair)) == ERROR_SUCCESS;
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
