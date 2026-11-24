// Pass a fixture directory containing files whose body includes "Cobalt317".
#include "../Windows/reference_import.hpp"
#include <cassert>
#include <cstdio>

int wmain(int argc, wchar_t **argv) {
  using namespace references;
  if (argc != 2 || FAILED(OleInitialize(nullptr))) return 1;
  fs::path fixtures(argv[1]);
  auto library = fixtures / L"library";
  bool passed = true;
  for (auto ext : {L"txt", L"md", L"csv", L"pdf", L"docx", L"xlsx", L"pptx", L"rtf", L"html"}) {
    auto name = std::wstring(L"sample.") + ext;
    auto content = extract(fixtures / name);
    bool ok = content.find(L"Cobalt317") != content.npos && save(library, name, utf8(content));
    std::printf("%ls: %s\n", ext, ok ? "pass" : "fail");
    passed &= ok;
  }
  assert(save(library, L"中文资料.md", utf8(L"南桥项目的值班时间为十九点。")));
  auto passages = search(library, L"南桥项目几点值班？");
  assert(!passages.empty() && passages.front().text.find(L"十九点") != std::wstring::npos);
  assert(search(library, L"pineapple").empty());
  assert(!search(library, L"Cobalt317").empty());
  assert(save(library, L"中文资料.md", utf8(L"新的办公地址是柳州。")));
  assert(search(library, L"南桥项目几点值班？").empty());
  OleUninitialize();
  return passed ? 0 : 1;
}
