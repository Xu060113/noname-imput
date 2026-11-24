// Online check using only synthetic reference material in a test directory.
#include "../Windows/model.hpp"
#include <cstdio>
std::atomic<long> moduleUsers{0};

int main() {
  auto library = references::fs::path(L".build/windows/test-language-references");
  if (!references::save(library, L"sample.md",
                        u8"Data 是对观察结果的记录。数据可来自传感器，质量取决于准确性和时效性。"))
    return 1;
  const std::string input = "Earlier notes: 这里是项目的中文笔记。\nWhat is data?";
  LengthLimits limits{999, 24};
  std::wstring accepted, last;
  std::string mode, language;
  bool selectedAlternative = false, checkedFollowup = false;
  int requests = 0;
  while (characterCount(accepted) < limits.answerTarget() && requests++ < 20) {
    auto stream = std::make_shared<Stream>();
    runModel(stream, input, utf8(accepted), mode, limits, last, library, language);
    if (stream->language != "en" || stream->mode != "answer" || stream->rows.empty()) return 1;
    mode = stream->mode;
    language = stream->language;
    if (selectedAlternative) checkedFollowup = true;
    for (auto &row : stream->rows) {
      for (auto &word : row.words)
        if (containsCjkText(word)) return 1;
      bool branch = !selectedAlternative && row.words.size() >= 2;
      last = row.words[branch ? 1 : 0];
      accepted += spaced(last, accepted.empty() ? wide(input) : accepted);
      if (branch) selectedAlternative = true;
      if (branch || row.firstIsBranch) break;
    }
  }
  std::printf("English characters=%zu requests=%d alternativeFollowup=%d\n",
              characterCount(accepted), requests, checkedFollowup);
  if (!checkedFollowup || characterCount(accepted) < limits.answerTarget() ||
      characterCount(accepted) > limits.answer) return 1;
  auto chinese = std::make_shared<Stream>();
  runModel(chinese, "Explain what data is. Please answer in Chinese.", "", "", {40,24}, L"", library);
  for (auto &row : chinese->rows)
    if (chinese->language == "zh" && containsCjkText(row.words.front())) return 0;
  return 1;
}
