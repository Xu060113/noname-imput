#include "common.hpp"

static HWND edit, enabled, status;
static HWND control(HWND parent, const wchar_t *type, const wchar_t *label,
                    DWORD flags, int id, int x, int y, int w, int h) {
  HWND child = CreateWindowExW(0, type, label, WS_VISIBLE | WS_CHILD | flags, x,
                               y, w, h, parent, (HMENU)(INT_PTR)id,
                               GetModuleHandleW(nullptr), nullptr);
  SendMessageW(child, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT),
               TRUE);
  return child;
}
static LRESULT CALLBACK handler(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_CREATE:
    control(w, L"STATIC", L"DeepSeek API Key", 0, 0, 20, 20, 360, 22);
    edit = control(w, L"EDIT", L"",
                   WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL | WS_TABSTOP, 1, 20,
                   47, 385, 26);
    SendMessageW(edit, EM_SETLIMITTEXT, 512, 0);
    SetWindowTextW(edit, wide(readKey()).c_str());
    enabled = control(w, L"BUTTON", L"停顿后显示 AI 候选",
                      BS_AUTOCHECKBOX | WS_TABSTOP, 2, 20, 86, 365, 23);
    SendMessageW(enabled, BM_SETCHECK,
                 aiEnabled() || GetWindowTextLengthW(edit) == 0 ? BST_CHECKED
                                                                : BST_UNCHECKED,
                 0);
    control(w, L"STATIC",
            L"开启后，本次输入的片段会发送给 DeepSeek。密钥保存在当前用户的 "
            L"Windows 凭据管理器中。",
            0, 0, 20, 122, 380, 50);
    control(w, L"BUTTON", L"保存", BS_DEFPUSHBUTTON | WS_TABSTOP, 3, 315, 180,
            90, 30);
    status = control(w, L"STATIC", L"", 0, 0, 20, 186, 285, 42);
    return 0;
  case WM_COMMAND:
    if (LOWORD(wp) == 3) {
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
      if (!ok ||
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
  WNDCLASSW c{};
  c.hInstance = instance;
  c.lpszClassName = L"YujuPreferences";
  c.lpfnWndProc = handler;
  c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  c.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  RegisterClassW(&c);
  HWND w = CreateWindowW(c.lpszClassName, L"语句拼音 · DeepSeek 设置",
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                         CW_USEDEFAULT, 445, 280, nullptr, nullptr, instance,
                         nullptr);
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
