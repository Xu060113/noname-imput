#pragma once
#include <msctf.h>
#include <string>

inline std::wstring textBeforeCaret(ITfRange *selection, TfEditCookie cookie) {
  constexpr LONG limit = 32768;
  ITfRange *range = nullptr;
  if (FAILED(selection->Clone(&range))) return {};
  LONG shifted = 0;
  HRESULT hr = range->Collapse(cookie, TF_ANCHOR_START);
  if (SUCCEEDED(hr)) hr = range->ShiftStart(cookie, -limit, &shifted, nullptr);
  std::wstring text;
  while (SUCCEEDED(hr) && text.size() < limit) {
    wchar_t block[1024];
    ULONG count = 0;
    hr = range->GetText(cookie, TF_TF_MOVESTART, block, 1024, &count);
    if (FAILED(hr) || !count) break;
    text.append(block, count);
  }
  range->Release();
  if (FAILED(hr)) return {};
  if (!text.empty() && text.front() >= 0xdc00 && text.front() <= 0xdfff) text.erase(0, 1);
  return text;
}
