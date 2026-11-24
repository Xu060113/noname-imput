#pragma once
#include "references.hpp"
#include "spreadsheet_text.hpp"
#include <filter.h>
#include <filterr.h>
#include <propsys.h>
#include <richedit.h>
#include <shlwapi.h>

namespace references {
inline IFilter *streamFilter(const fs::path &path) {
  // Some registered filters (including the Windows PDF filter) only accept streams.
  auto defaultValue = [](const std::wstring &key) {
    wchar_t value[64]{};
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CLASSES_ROOT, key.c_str(), nullptr, RRF_RT_REG_SZ,
                        nullptr, value, &size) == ERROR_SUCCESS ? std::wstring(value) : L"";
  };
  auto handler = defaultValue(path.extension().wstring() + L"\\PersistentHandler");
  if (handler.empty()) return nullptr;
  auto id = defaultValue(L"CLSID\\" + handler +
                         L"\\PersistentAddinsRegistered\\{89BCB740-6119-101A-BCB7-00DD010655AF}");
  CLSID cls{};
  ComPtr<IFilter> filter;
  ComPtr<IStream> stream;
  if (FAILED(CLSIDFromString(id.c_str(), &cls)) ||
      FAILED(CoCreateInstance(cls, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&filter))) ||
      FAILED(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
                                    FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &stream))) return nullptr;
  ComPtr<IInitializeWithStream> init;
  if (SUCCEEDED(filter.As(&init)) && SUCCEEDED(init->Initialize(stream.Get(), STGM_READ)))
    return filter.Detach();
  ComPtr<IPersistStream> persist;
  return SUCCEEDED(filter.As(&persist)) && SUCCEEDED(persist->Load(stream.Get())) ? filter.Detach() : nullptr;
}
inline std::wstring richText(const fs::path &path) {
  static HMODULE library = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  auto bytes = readBytes(path, 8 * maxBytes);
  if (!library || bytes.compare(0, 5, "{\\rtf") != 0) return {};
  HWND box = CreateWindowExW(0, MSFTEDIT_CLASS, L"", ES_MULTILINE, 0,0,0,0, HWND_MESSAGE,
                             nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!box) return {};
  SETTEXTEX set{ST_DEFAULT, CP_ACP};
  SendMessageW(box, EM_SETTEXTEX, (WPARAM)&set, (LPARAM)bytes.c_str());
  int length = GetWindowTextLengthW(box);
  std::wstring text;
  if (length > 0 && (size_t)length <= maxBytes) {
    text.resize(length + 1);
    text.resize(GetWindowTextW(box, text.data(), length + 1));
  }
  DestroyWindow(box);
  return text;
}
inline std::wstring extract(const fs::path &path) {
  auto ext = lower(path.extension().wstring());
  if (ext == L".xlsx" || ext == L".xlsm") return spreadsheetText(path);
  if (ext == L".rtf") return richText(path);
  for (auto plain : {L".txt", L".md", L".csv", L".tsv", L".json", L".xml",
                     L".yaml", L".yml", L".log", L".ini", L".srt"}) {
    if (ext != plain) continue;
    auto bytes = readBytes(path, maxBytes);
    if (bytes.size() >= 2 && (bytes.compare(0, 2, "\xff\xfe") == 0 ||
                              bytes.compare(0, 2, "\xfe\xff") == 0)) {
      if (bytes.size() % 2) return {};
      bool little = (unsigned char)bytes.front() == 0xff;
      std::wstring text;
      for (size_t i = 2; i < bytes.size(); i += 2) {
        unsigned char a = bytes[i], b = bytes[i+1];
        text.push_back((wchar_t)(little ? a | (b << 8) : b | (a << 8)));
      }
      return text;
    }
    if (bytes.compare(0, 3, "\xef\xbb\xbf") == 0) bytes.erase(0, 3);
    return wide(bytes);
  }
  // Use the registered text filter, including PDF and Office handlers.
  static HMODULE query = LoadLibraryExW(L"query.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  using LoadFilter = HRESULT (WINAPI *)(LPCWSTR, IUnknown *, void **);
  static auto load = query ? (LoadFilter)GetProcAddress(query, "LoadIFilter") : nullptr;
  IFilter *filter = nullptr;
  if (!load || FAILED(load(path.c_str(), nullptr, (void **)&filter)) || !filter)
    filter = streamFilter(path);
  if (!filter) return {};
  ULONG flags = 0;
  HRESULT hr = filter->Init(IFILTER_INIT_CANON_PARAGRAPHS | IFILTER_INIT_CANON_SPACES,
                            0, nullptr, &flags);
  std::wstring text;
  STAT_CHUNK chunk{};
  if (SUCCEEDED(hr)) {
    while (SUCCEEDED(hr = filter->GetChunk(&chunk))) {
      if (!(chunk.flags & CHUNK_TEXT)) continue;
      if (!text.empty() && chunk.breakType != CHUNK_NO_BREAK) text += L'\n';
      do {
        wchar_t part[2048];
        ULONG count = 2048;
        hr = filter->GetText(&count, part);
        if (FAILED(hr) || !count) break;
        text.append(part, count);
      } while (hr != FILTER_S_LAST_TEXT && text.size() <= maxBytes);
      if (text.size() > maxBytes ||
          (FAILED(hr) && hr != FILTER_E_NO_MORE_TEXT && hr != FILTER_E_NO_TEXT)) break;
    }
  }
  filter->Release();
  if (hr != FILTER_E_END_OF_CHUNKS || text.size() > maxBytes) return {};
  return text;
}
}
