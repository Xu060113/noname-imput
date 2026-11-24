#pragma once
#include "references.hpp"
#include <msopc.h>
#include <wrl/client.h>
#include <xmllite.h>

namespace references {
using Microsoft::WRL::ComPtr;
// Excel stores strings either inside each cell or in a shared string table.
inline bool readSheetPart(IOpcPart *part, bool table, std::vector<std::wstring> &strings,
                          std::wstring &text) {
  ComPtr<IStream> stream;
  ComPtr<IXmlReader> reader;
  STATSTG stat{};
  if (FAILED(part->GetContentStream(&stream)) || FAILED(stream->Stat(&stat, STATFLAG_NONAME)) ||
      stat.cbSize.QuadPart > 32 * 1024 * 1024 ||
      FAILED(CreateXmlReader(IID_PPV_ARGS(&reader), nullptr)) || FAILED(reader->SetInput(stream.Get())))
    return false;
  XmlNodeType type;
  std::wstring value;
  bool capture = false, shared = false;
  HRESULT hr;
  while ((hr = reader->Read(&type)) == S_OK) {
    LPCWSTR p = nullptr;
    if (type == XmlNodeType_Text && capture) {
      UINT n = 0;
      if (FAILED(reader->GetValue(&p, &n))) return false;
      value.append(p, n);
    } else if (type == XmlNodeType_Element || type == XmlNodeType_EndElement) {
      if (FAILED(reader->GetLocalName(&p, nullptr))) return false;
      std::wstring name(p);
      bool open = type == XmlNodeType_Element;
      if (open && (name == L"c" || name == L"si")) {
        value.clear();
        shared = false;
        if (name == L"c" && reader->MoveToAttributeByName(L"t", nullptr) == S_OK) {
          shared = SUCCEEDED(reader->GetValue(&p, nullptr)) && !wcscmp(p, L"s");
          reader->MoveToElement();
        }
      }
      if (name == L"v" || name == L"t") capture = open && !reader->IsEmptyElement();
      if (table && name == L"si" && (!open || reader->IsEmptyElement())) strings.push_back(value);
      if (!table && !open && name == L"c") {
        if (shared) {
          wchar_t *end = nullptr;
          auto index = wcstoul(value.c_str(), &end, 10);
          if (end == value.c_str() || *end || index >= strings.size()) return false;
          text += strings[index];
        } else text += value;
        text += L'\t';
      }
      if (!table && !open && name == L"row") text += L'\n';
    }
    if (value.size() > maxBytes || text.size() > maxBytes || strings.size() > 500000) return false;
  }
  return hr == S_FALSE;
}
inline std::wstring spreadsheetText(const fs::path &path) {
  const CLSID factoryId = {0x6b2d6ba0,0x9f3e,0x4f27,{0x92,0x0b,0x31,0x3c,0xc4,0x26,0xa3,0x9e}};
  ComPtr<IOpcFactory> factory;
  ComPtr<IStream> stream;
  ComPtr<IOpcPackage> package;
  ComPtr<IOpcPartSet> parts;
  ComPtr<IOpcPartEnumerator> it;
  if (FAILED(CoCreateInstance(factoryId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
      FAILED(factory->CreateStreamOnFile(path.c_str(), OPC_STREAM_IO_READ, nullptr, 0, &stream)) ||
      FAILED(factory->ReadPackageFromStream(stream.Get(), OPC_CACHE_ON_ACCESS, &package)) ||
      FAILED(package->GetPartSet(&parts)) || FAILED(parts->GetEnumerator(&it))) return {};
  std::vector<std::wstring> strings;
  std::vector<ComPtr<IOpcPart>> sheets;
  std::wstring text;
  BOOL next = FALSE;
  HRESULT hr;
  while (SUCCEEDED(hr = it->MoveNext(&next)) && next) {
    ComPtr<IOpcPart> part;
    LPWSTR type = nullptr;
    if (FAILED(it->GetCurrent(&part)) || FAILED(part->GetContentType(&type))) return {};
    std::wstring kind(type);
    CoTaskMemFree(type);
    if (kind == L"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml" &&
        !readSheetPart(part.Get(), true, strings, text)) return {};
    if (kind == L"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml")
      sheets.push_back(part);
  }
  if (FAILED(hr)) return {};
  for (auto &sheet : sheets)
    if (!readSheetPart(sheet.Get(), false, strings, text)) return {};
  return text;
}
}
