#include "../Windows/length.hpp"
#include <cassert>
#include <iostream>

int main() {
  LengthLimits limits;
  assert(limits.forMode("answer") == 80);
  assert(limits.forMode("continuation") == 24);
  assert(!LengthLimits::valid(7) && LengthLimits::valid(8));
  assert(LengthLimits::valid(300) && !LengthLimits::valid(301));

  PredictionBudget answer(8, L"什么是大模型", L"", "answer");
  assert(answer.next({L"大模型", L"它"}).words.size() == 2);
  assert(answer.next({L"是", L"属于"}).words.size() == 2);
  auto end = answer.next({L"学习模型", L"非常复杂的系统", L"系统"});
  assert(end.words.size() == 2 && !end.firstIsBranch && !answer.canContinue());

  // A shorter displayed first option can actually be the model's second option.
  PredictionBudget partial(8, L"起点", L"一二三四五六", "continuation");
  auto alternate = partial.next({L"太长了", L"好", L"很好"});
  assert(alternate.firstIsBranch && alternate.words.front() == L"好" && !partial.canContinue());
  PredictionBudget branch(8, L"起点", L"一二三四五六好", "continuation");
  assert(branch.next({L"。", L"继续"}).words.size() == 1 && !branch.canContinue());

  // Space inserted after the user's English input also consumes output budget.
  PredictionBudget english(8, L"Hello", L"", "continuation");
  assert(english.next({L"world", L"everyone"}).words.size() == 1);
  assert(english.next({L"!", L" ok"}).words.size() == 1);
  assert(english.canContinue());
  PredictionBudget exact(8, L"", L"12345678", "answer");
  assert(!exact.canContinue() && exact.next({L"."}).words.empty());
  assert(characterCount(L"中🙂！") == 3);
  assert(characterCount(std::wstring{wchar_t(0xd83d), wchar_t(0xde00)}) == 1);

  const std::wstring question = L"什么是数据库";
  auto committed = question + L"\r\n数据库";
  for (auto newline : {L"\r\n", L"\n", L"\r"}) {
    assert(matchesCaretText(committed, L"较早的文字" + question + newline + L"数据库"));
    assert(!matchesCaretText(committed, question + newline + L"数据"));
    assert(!matchesCaretText(committed, question + newline + L"数据库。"));
  }
  assert(matchesCaretText(std::wstring(18, L'\n'), std::wstring(16, L'\r')));
  assert(!matchesCaretText(committed, L"数据库"));

  PredictionBudget greeting(24, L"你好", L"", "continuation");
  assert(greeting.next({L"你好"}).words.empty() && greeting.canContinue());
  assert(greeting.next({L"，", L"呀"}).words.front() == L"，");
  PredictionBudget greetingReply(80, L"你好", L"", "answer");
  assert(greetingReply.next({L"你好"}).words.empty() && greetingReply.canContinue());

  PredictionBudget database(80, question, L"", "answer");
  assert(database.next({L"数据库"}).words.front() == L"数据库");
  assert(database.next({L"数据库"}).words.empty() && database.canContinue());
  assert(database.next({L"是", L"通常"}).words.front() == L"是");

  PredictionBudget restarted(80, question, L"关系型数据库", "answer", L"数据库");
  auto alternatives = restarted.next({L"数据库", L"可以", L"用于"});
  assert(alternatives.firstIsBranch && alternatives.words.front() == L"可以");
  assert(!restarted.canContinue());

  for (unsigned limit : {8u, 24u, 80u, 300u}) {
    PredictionBudget longAnswer(limit, L"原问题不占长度", L"", "answer");
    for (unsigned i=0; i<limit; ++i) {
      assert(longAnswer.canContinue());
      assert(longAnswer.next({L"字"}).words.size() == 1);
    }
    assert(!longAnswer.canContinue());
  }
  std::cout << "Windows prediction length checks passed\n";
}
