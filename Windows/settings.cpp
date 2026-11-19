#include "common.hpp"

static HWND edit, enabled, status, answerField, continuationField;
static HFONT messageFont;
static int dpi = 96;
static int scale(int pixels) { return MulDiv(pixels, dpi, 96); }
static HWND control(HWND parent, const wchar_t *type, const wchar_t *label,
                    DWORD flags, int id, int x, int y, int w, int h) {
  HWND child =
      CreateWindowExW(0, type, label, WS_VISIBLE | WS_CHILD | flags, scale(x),
                      scale(y), scale(w), scale(h), parent, (HMENU)(INT_PTR)id,
                      GetModuleHandleW(nullptr), nullptr);
  SendMessageW(child, WM_SETFONT, (WPARAM)messageFont, TRUE);
  return child;
}
static HWND lengthControl(HWND parent, const wchar_t *label, int id, int y,
                          unsigned value) {
  control(parent, L"STATIC", label, 0, 0, 20, y + 4, 135, 22);
  auto field = control(parent, L"EDIT", std::to_wstring(value).c_str(),
                       WS_BORDER | ES_NUMBER | WS_TABSTOP, id, 165, y, 72, 26);
  control(parent, L"STATIC", L"字符（8–300）", 0, 0, 248, y + 4, 146, 22);
  return field;
}

static bool readLimit(HWND field, std::uint32_t &value) {
  int size = GetWindowTextLengthW(field);
  if (size < 1 || size > 3)
    return false;
  wchar_t text[4]{};
  GetWindowTextW(field, text, 4);
  unsigned number = 0;
  for (int i = 0; i < size; ++i) {
    if (text[i] < L'0' || text[i] > L'9')
      return false;
    number = number * 10 + (text[i] - L'0');
  }
  if (!LengthLimits::valid(number))
    return false;
  value = number;
  return true;
}
static LRESULT CALLBACK handler(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_CREATE: {
    control(w, L"STATIC", L"DeepSeek API Key", 0, 0, 20, 20, 360, 22);
    edit = control(w, L"EDIT", L"",
                   WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL | WS_TABSTOP, 1, 20,
                   47, 385, 26);
    SendMessageW(edit, EM_SETLIMITTEXT, 512, 0);
    SetWindowTextW(edit, wide(readKey()).c_str());
    enabled = control(w, L"BUTTON", L"停顿后显示 AI 候选",
                      BS_AUTOCHECKBOX | WS_TABSTOP, 2, 20, 86, 365, 23);
    SendMessageW(enabled, BM_SETCHECK,
                 aiEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    control(w, L"STATIC",
            L"开启后，本次输入的片段会发送给 DeepSeek。密钥保存在当前用户的 "
            L"Windows 凭据管理器中。",
            0, 0, 20, 122, 380, 50);
    auto limits = readLengthLimits();
    answerField = lengthControl(w, L"问题回答上限", 4, 184, limits.answer);
    continuationField =
        lengthControl(w, L"语句续写上限", 5, 223, limits.continuation);
    control(w, L"STATIC", L"空格和标点均计数。保存后，从下一次预测开始生效。",
            0, 0, 20, 265, 390, 40);
    control(w, L"BUTTON", L"保存", BS_DEFPUSHBUTTON | WS_TABSTOP, 3, 315, 316,
            90, 30);
    status = control(w, L"STATIC", L"", 0, 0, 20, 316, 288, 44);
    return 0;
  }
  case WM_COMMAND:
    if (LOWORD(wp) == 3) {
      LengthLimits limits;
      HWND invalid = nullptr;
      if (!readLimit(answerField, limits.answer))
        invalid = answerField;
      else if (!readLimit(continuationField, limits.continuation))
        invalid = continuationField;
      if (invalid) {
        SetWindowTextW(status, L"请输入 8 至 300 之间的整数。");
        SetFocus(invalid);
        SendMessageW(invalid, EM_SETSEL, 0, -1);
        return 0;
      }
      wchar_t text[513] = {};
      GetWindowTextW(edit, text, 513);
      std::string key = utf8(text);
      SecureZeroMemory(text, sizeof(text));
      DWORD on = SendMessageW(enabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
      if (on && key.empty()) {
        SetWindowTextW(status, L"请填写 API Key 或关闭 AI 候选。");
        return 0;
      }
      BOOL ok;
      if (key.empty()) {
        ok = CredDeleteW(credentialName, CRED_TYPE_GENERIC, 0);
        if (GetLastError() == ERROR_NOT_FOUND)
          ok = TRUE;
      } else {
        CREDENTIALW cred{};
        cred.Type = CRED_TYPE_GENERIC;
        cred.TargetName = const_cast<wchar_t *>(credentialName);
        cred.CredentialBlob = (LPBYTE)key.data();
        cred.CredentialBlobSize = (DWORD)key.size();
        cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
        ok = CredWriteW(&cred, 0);
        SecureZeroMemory(key.data(), key.size());
      }
      if (!ok || !writeLengthLimits(limits) ||
          RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Yuju", L"AIEnabled",
                          REG_DWORD, &on, sizeof(on)) != ERROR_SUCCESS)
        SetWindowTextW(status, L"保存失败。请检查当前用户权限。");
      else {
        MessageBoxW(w, L"已保存。使用 Win + 空格选择「语句拼音」。",
                    L"语句拼音", MB_OK);
        DestroyWindow(w);
      }
      return 0;
    }
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(w, msg, wp, lp);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetProcessDPIAware();
  HDC screen = GetDC(nullptr);
  dpi = GetDeviceCaps(screen, LOGPIXELSY);
  ReleaseDC(nullptr, screen);
  NONCLIENTMETRICSW metrics{sizeof(metrics)};
  if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                            0))
    messageFont = CreateFontIndirectW(&metrics.lfMessageFont);
  WNDCLASSW c{};
  c.hInstance = instance;
  c.lpszClassName = L"YujuPreferences";
  c.lpfnWndProc = handler;
  c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  c.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  RegisterClassW(&c);
  HWND w = CreateWindowW(c.lpszClassName, L"语句拼音 · 控制后台",
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                         CW_USEDEFAULT, scale(451), scale(415), nullptr,
                         nullptr, instance, nullptr);
  if (!w)
    return 1;
  ShowWindow(w, show);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(w, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  return 0;
}
