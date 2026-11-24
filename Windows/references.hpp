#pragma once
#include "common.hpp"
#include <filesystem>
#include <fstream>
#include <set>
#include <shlobj.h>

namespace references {
namespace fs = std::filesystem;
constexpr size_t maxBytes = 2 * 1024 * 1024;

inline fs::path directory() {
  wchar_t local[MAX_PATH]{};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local)))
    return {};
  return fs::path(local) / L"Yuju" / L"References";
}
inline std::vector<fs::path> files(const fs::path &dir) {
  std::vector<fs::path> result;
  std::error_code ec;
  if (!dir.empty())
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
      if (it->path().extension() == L".ref" && it->is_regular_file(ec))
        result.push_back(it->path());
  std::sort(result.begin(), result.end());
  return result;
}
inline std::string readBytes(const fs::path &path, size_t limit) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  auto length = file.tellg();
  if (length <= 0 || static_cast<size_t>(length) > limit)
    return {};
  std::string bytes(static_cast<size_t>(length), '\0');
  file.seekg(0);
  return file.read(bytes.data(), length) ? bytes : std::string{};
}
inline bool save(const fs::path &dir, const fs::path &name, const std::string &bytes) {
  if (dir.empty() || bytes.empty() || bytes.size() > maxBytes || name != name.filename())
    return false;
  std::error_code ec;
  fs::create_directories(dir, ec);
  wchar_t temp[MAX_PATH]{};
  if (ec || !GetTempFileNameW(dir.c_str(), L"ref", 0, temp))
    return false;
  auto target = dir / (name.wstring() + L".ref");
  std::ofstream out(fs::path(temp), std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), bytes.size());
  out.close();
  if (out && MoveFileExW(temp, target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return true;
  DeleteFileW(temp);
  return false;
}
inline bool ideograph(wchar_t ch) { return ch >= 0x3400 && ch <= 0x9fff; }
inline std::wstring lower(std::wstring text) {
  if (!text.empty()) CharLowerBuffW(text.data(), (DWORD)text.size());
  return text;
}
inline std::set<std::wstring> keywords(const std::wstring &input) {
  auto q = lower(input.substr(input.size() > 256 ? input.size() - 256 : 0));
  std::set<std::wstring> terms;
  for (size_t i = 0; i < q.size();) {
    if (ideograph(q[i])) {
      size_t len = i + 1 < q.size() && ideograph(q[i+1]) ? 2 : 1;
      if (len == 2 || i == 0 || !ideograph(q[i-1]))
        terms.insert(q.substr(i, len));
      ++i;
    } else if (iswalnum(q[i])) {
      size_t start = i++;
      while (i < q.size() && iswalnum(q[i]) && !ideograph(q[i])) ++i;
      terms.insert(q.substr(start, i-start));
    } else ++i;
  }
  for (auto stop : {L"the", L"is", L"a", L"an", L"of", L"what", L"什么", L"怎么", L"如何"})
    terms.erase(stop);
  return terms;
}
struct Passage { int score; std::wstring source, text; };
inline std::vector<Passage> search(const fs::path &dir, const std::wstring &query) {
  std::vector<Passage> best;
  const auto terms = keywords(query);
  if (terms.empty()) return best;
  for (const auto &file : files(dir)) {
    auto body = wide(readBytes(file, maxBytes));
    for (size_t start = 0; start < body.size(); start += 512) {
      auto chunk = body.substr(start, 640);
      auto folded = lower(chunk);
      int score = 0;
      for (const auto &term : terms)
        if (folded.find(term) != folded.npos) score += (int)term.size();
      if (score && (best.size() < 3 || score > best.back().score)) {
        best.push_back({score, file.stem().wstring(), std::move(chunk)});
        std::stable_sort(best.begin(), best.end(), [](const Passage &a, const Passage &b) {
          return a.score > b.score;
        });
        if (best.size() > 3) best.pop_back();
      }
      if (body.size() - start <= 640) break;
    }
  }
  return best;
}
}
