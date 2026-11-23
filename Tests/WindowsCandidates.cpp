// Online regression test. Run on an interactive Windows desktop with Yuju
// installed and AI enabled. Only this test process switches input profiles.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <msctf.h>
#include <richedit.h>
#include <cstdio>
#include <string>
#include "../Windows/common.hpp"

static void pumpMessages(unsigned duration) {
  auto until = GetTickCount64() + duration;
  do {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    Sleep(10);
  } while (GetTickCount64() < until);
}

static HWND visibleCandidates() {
  HWND result = nullptr;
  EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM output) -> BOOL {
    wchar_t type[64]{};
    GetClassNameW(window, type, 64);
    if (wcscmp(type, L"YujuWordCandidates") || !IsWindowVisible(window))
      return TRUE;
    *reinterpret_cast<HWND *>(output) = window;
    return FALSE;
  }, reinterpret_cast<LPARAM>(&result));
  return result;
}

static HWND waitForCandidates() {
  auto until = GetTickCount64() + 30000;
  while (!visibleCandidates() && GetTickCount64() < until)
    pumpMessages(75);
  return visibleCandidates();
}

static std::wstring textOf(HWND window) {
  wchar_t text[1024]{};
  GetWindowTextW(window, text, 1024);
  return text;
}

static bool press(ITfKeystrokeMgr *keyboard, int key) {
  LPARAM flags = 1 | (MapVirtualKeyW(key, MAPVK_VK_TO_VSC) << 16);
  BOOL handled = FALSE;
  if (FAILED(keyboard->TestKeyDown(key, flags, &handled)) || !handled ||
      FAILED(keyboard->KeyDown(key, flags, &handled)) || !handled)
    return false;
  flags |= 0xc0000000;
  if (SUCCEEDED(keyboard->TestKeyUp(key, flags, &handled)) && handled)
    keyboard->KeyUp(key, flags, &handled);
  return true;
}

static std::wstring candidateWord(HWND popup, int key = '1') {
  auto label = textOf(popup);
  auto marker = L"   " + std::wstring(1, wchar_t(key)) + L" ";
  auto start = label.find(marker);
  if (start == label.npos)
    return {};
  start += marker.size();
  auto end = label.find(L"   " + std::wstring(1, wchar_t(key + 1)) + L" ", start);
  return label.substr(start, end - start);
}

static bool checkInput(HWND editor, ITfKeystrokeMgr *keyboard, const std::string &scenario) {
  bool greeting = scenario == "greeting";
  for (int letter : std::string(greeting ? "NIHAO" : "SHENMESHISHUJUKU"))
    if (!press(keyboard, letter))
      return false;
  if (!press(keyboard, '1') || textOf(editor) != (greeting ? L"你好" : L"什么是数据库"))
    return false;
  HWND first = waitForCandidates();
  if (!first)
    return false;
  if (greeting) {
    auto row = textOf(first);
    pumpMessages(2200);
    if (!visibleCandidates() || row != textOf(first) || textOf(editor) != L"你好")
      return false;
    int choice = row.find(L"   2 ") != std::wstring::npos ? '2' : '1';
    if (!press(keyboard, choice))
      return false;
    auto result = textOf(editor);
    return result.size() > 2 && result.compare(0, 2, L"你好") == 0 &&
           result.find(L"你好", 2) == std::wstring::npos;
  }
  int firstKey = scenario == "branch3" ? '3' : scenario == "branch2" ? '2' : '1';
  auto firstChoice = candidateWord(first, firstKey);
  if (firstChoice.empty()) {
    puts("Model did not offer the requested alternative");
    return false;
  }
  if (!press(keyboard, firstKey))
    return false;
  auto answer = textOf(editor);
  auto start = answer.find_first_not_of(L"\r\n ", std::wstring(L"什么是数据库").size());
  if (start == std::wstring::npos || answer.substr(start) != firstChoice)
    return false;
  if (firstKey != '1' && (!press(keyboard, firstKey) || textOf(editor) != answer)) {
    puts("A key pressed while waiting changed the accepted text");
    return false;
  }
  pumpMessages(100);
  HWND second = waitForCandidates();
  if (!second)
    return false;
  auto row = textOf(second);
  auto choice = candidateWord(second);
  pumpMessages(2200);
  if (!visibleCandidates() || textOf(second) != row || textOf(editor) != answer ||
      !press(keyboard, '1'))
    return false;
  auto extended = textOf(editor);
  if (extended != answer + choice)
    return false;
  if (scenario != "long")
    return true;
  const auto limits = readLengthLimits();
  for (unsigned i = 0; i < limits.answer; ++i) {
    pumpMessages(100);
    HWND next = waitForCandidates();
    if (!next)
      break;
    choice = candidateWord(next);
    if (choice.empty() || !press(keyboard, '1'))
      return false;
    extended += choice;
    if (textOf(editor) != extended)
      return false;
  }
  size_t length = characterCount(extended.substr(start));
  std::printf("Committed %zu characters, configured maximum %u\n", length, limits.answer);
  return length >= limits.answerTarget() && length <= limits.answer;
}

int main(int argc, char **argv) {
  if (FAILED(OleInitialize(nullptr)))
    return 1;
  ITfThreadMgr *manager = nullptr;
  TfClientId client = 0;
  if (FAILED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&manager))) || FAILED(manager->Activate(&client)))
    return 1;
  HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!LoadLibraryW(L"Msftedit.dll"))
    return 1;
  HWND frame = CreateWindowW(L"STATIC", L"语句拼音 · 临时选词测试", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             180, 180, 480, 200, nullptr, nullptr, instance, nullptr);
  std::string scenario = argc > 1 ? argv[1] : "question";
  DWORD style = scenario == "single" ? ES_AUTOHSCROLL : ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL;
  HWND editor = CreateWindowW(MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | style | WS_BORDER,
                              8, 10, 440, 120, frame, nullptr, instance, nullptr);
  if (!editor)
    return 1;
  SendMessageW(editor, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);
  DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
  DWORD testThread = GetCurrentThreadId();
  bool attached = foregroundThread && foregroundThread != testThread &&
                  AttachThreadInput(testThread, foregroundThread, TRUE);
  SetForegroundWindow(frame);
  SetActiveWindow(frame);
  SetFocus(editor);
  if (attached)
    AttachThreadInput(testThread, foregroundThread, FALSE);
  pumpMessages(300);
  ITfInputProcessorProfileMgr *profiles = nullptr;
  ITfKeystrokeMgr *keyboard = nullptr;
  if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&profiles))) || FAILED(manager->QueryInterface(IID_PPV_ARGS(&keyboard))))
    return 1;
  const GUID service = {0xc5f7c10d, 0xd14c, 0x4326, {0x83,0x3a,0x45,0x3c,0xaf,0x22,0x17,0xb4}};
  const GUID profile = {0xdc966782, 0xf338, 0x49f6, {0xb1,0xf1,0xf8,0xac,0x41,0xa7,0x3e,0x55}};
  // TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE (Windows SDK).
  if (FAILED(profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0804, service, profile, nullptr, 0x10000004)))
    return 1;
  pumpMessages(450);
  if (GetForegroundWindow() != frame) {
    puts("Test window did not acquire keyboard focus");
    return 2;
  }
  bool passed = checkInput(editor, keyboard, scenario);
  keyboard->Release();
  profiles->Release();
  DestroyWindow(frame);
  manager->Deactivate();
  manager->Release();
  OleUninitialize();
  puts(passed ? "Windows online candidate selection passed" : "Windows online candidate selection failed");
  return passed ? 0 : 1;
}
