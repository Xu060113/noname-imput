#include "model.hpp"
#include "rime_api.h"
#include <functional>
#include <initguid.h>
#include <inputscope.h>
#include <msctf.h>
#include <shlobj.h>

HMODULE module;
std::atomic<long> moduleUsers{0};
const GUID clsid = {0xc5f7c10d,
                    0xd14c,
                    0x4326,
                    {0x83, 0x3a, 0x45, 0x3c, 0xaf, 0x22, 0x17, 0xb4}};
const GUID profile = {0xdc966782,
                      0xf338,
                      0x49f6,
                      {0xb1, 0xf1, 0xf8, 0xac, 0x41, 0xa7, 0x3e, 0x55}};
const wchar_t *comKey = L"CLSID\\{C5F7C10D-D14C-4326-833A-453CAF2217B4}";
static RimeApi *rime() {
  static RimeApi *api = []() -> RimeApi * {
    auto base = moduleDir(module);
    HMODULE lib = LoadLibraryExW((base + L"\\rime.dll").c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                     LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!lib)
      return nullptr;
    auto get = (decltype(&rime_get_api))GetProcAddress(lib, "rime_get_api");
    if (!get)
      return nullptr;
    auto r = get();
    wchar_t local[MAX_PATH];
    if (FAILED(
            SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local)))
      return nullptr;
    std::wstring dir = std::wstring(local) + L"\\Yuju\\Rime";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    static std::string shared = utf8(base + L"\\Rime"), user = utf8(dir);
    RIME_STRUCT(RimeTraits, t);
    t.shared_data_dir = shared.c_str();
    t.user_data_dir = user.c_str();
    t.app_name = "rime.yuju";
    t.distribution_code_name = "yuju";
    t.distribution_version = "1.0";
    t.log_dir = "";
    t.min_log_level = 2;
    // The engine and its background workers live for the host process lifetime.
    HMODULE pinned;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&rime), &pinned);
    r->setup(&t);
    r->initialize(&t);
    return r;
  }();
  return api;
}

class Edit final : public ITfEditSession {
  ULONG refs = 1;
  std::function<void(TfEditCookie)> action;

public:
  explicit Edit(std::function<void(TfEditCookie)> f) : action(std::move(f)) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **p) override {
    if (!p)
      return E_POINTER;
    *p = nullptr;
    if (id != IID_IUnknown && id != IID_ITfEditSession)
      return E_NOINTERFACE;
    *p = static_cast<ITfEditSession *>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    auto n = --refs;
    if (!n)
      delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie ec) override {
    action(ec);
    return S_OK;
  }
};

class Service final : public ITfTextInputProcessor,
                      public ITfKeyEventSink,
                      public ITfThreadMgrEventSink {
  ULONG refs = 1;
  ITfThreadMgr *manager = nullptr;
  TfClientId id = 0;
  DWORD cookie = TF_INVALID_COOKIE;
  ITfContext *context = nullptr;
  ITfRange *anchor = nullptr;
  HWND popup = nullptr;
  RimeSessionId ime = 0;
  bool restricted = false;
  std::wstring prior, raw, question, accepted;
  std::string mode;
  std::vector<std::wstring> words;
  std::shared_ptr<Stream> stream;
  ULONGLONG due = 0;
  bool fileMode = false; // this question carries the course files
  // Ctrl+V seen: after the pause, read the clipboard and ask about the pasted text.
  ITfContext *pasteContext = nullptr;
  ULONGLONG pasteDue = 0;
  static constexpr size_t maxPaste = 2000;
  static LRESULT CALLBACK windowProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto self = (Service *)GetWindowLongPtrW(w, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
      self = (Service *)((CREATESTRUCTW *)lp)->lpCreateParams;
      SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)self);
    }
    if (msg == WM_TIMER && self) {
      self->poll();
      return 0;
    }
    if (msg == WM_NCHITTEST)
      return HTTRANSPARENT;
    if (msg == WM_PAINT) {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(w, &ps);
      FillRect(dc, &ps.rcPaint, GetSysColorBrush(COLOR_INFOBK));
      SetTextColor(dc, GetSysColor(COLOR_INFOTEXT));
      SetBkMode(dc, TRANSPARENT);
      auto old = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
      wchar_t buf[1024];
      GetWindowTextW(w, buf, 1024);
      RECT rect;
      GetClientRect(w, &rect);
      rect.left += 8;
      DrawTextW(dc, buf, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
      SelectObject(dc, old);
      EndPaint(w, &ps);
      return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
  }
  void clearPrediction() {
    if (stream)
      stream->cancelled = true;
    stream.reset();
    due = 0;
    words.clear();
    question.clear();
    accepted.clear();
    mode.clear();
    fileMode = false;
    if (popup) {
      KillTimer(popup, 1);
      ShowWindow(popup, SW_HIDE);
    }
  }
  void clearAll() {
    clearPrediction();
    pasteDue = 0;
    release(pasteContext);
    release(anchor);
    release(context);
    if (ime)
      rime()->clear_composition(ime);
    raw.clear();
    prior.clear();
  }
  bool edit(ITfContext *ctx, DWORD access,
            std::function<void(TfEditCookie)> f) {
    auto session = new Edit(std::move(f));
    HRESULT hr = E_FAIL;
    auto request =
        ctx->RequestEditSession(id, session, TF_ES_SYNC | access, &hr);
    session->Release();
    return SUCCEEDED(request) && hr == S_OK;
  }
  bool secure(ITfContext *ctx, ITfRange *range, TfEditCookie ec) {
    if (restricted)
      return true;
    HWND focus = GetFocus();
    wchar_t name[40] = {};
    GetClassNameW(focus, name, 40);
    if ((!_wcsicmp(name, L"Edit") || wcsstr(name, L"RichEdit")) &&
        (GetWindowLongPtrW(focus, GWL_STYLE) & ES_PASSWORD))
      return true;
    ITfReadOnlyProperty *prop = nullptr;
    if (FAILED(ctx->GetAppProperty(GUID_PROP_INPUTSCOPE, &prop)))
      return false;
    VARIANT v;
    VariantInit(&v);
    bool blocked = false;
    if (SUCCEEDED(prop->GetValue(ec, range, &v)) && v.vt == VT_UNKNOWN &&
        v.punkVal) {
      ITfInputScope *scope = nullptr;
      if (SUCCEEDED(
              v.punkVal->QueryInterface(IID_ITfInputScope, (void **)&scope))) {
        InputScope *list = nullptr;
        UINT size = 0;
        if (SUCCEEDED(scope->GetInputScopes(&list, &size))) {
          for (UINT i = 0; i < size; i++)
            if (list[i] == IS_PASSWORD || list[i] == IS_PRIVATE ||
                list[i] == IS_NUMERIC_PASSWORD || list[i] == IS_NUMERIC_PIN ||
                list[i] == IS_ALPHANUMERIC_PIN ||
                list[i] == IS_ALPHANUMERIC_PIN_SET)
              blocked = true;
          CoTaskMemFree(list);
        }
        scope->Release();
      }
    }
    VariantClear(&v);
    prop->Release();
    return blocked;
  }
  bool sameCaret(ITfRange *sel, TfEditCookie ec) {
    BOOL same = FALSE, empty = FALSE;
    if (!anchor || FAILED(sel->IsEmpty(ec, &empty)) || !empty ||
        FAILED(sel->IsEqualStart(ec, anchor, TF_ANCHOR_START, &same)) || !same)
      return false;
    // Compare the last characters typed here with the document, ignoring line
    // breaks: editors store the "\r\n" this service inserts as "\r" or "\n".
    std::wstring expected = withoutBreaks(prior);
    if (expected.empty())
      return true;
    std::wstring actual = withoutBreaks(textBefore(sel, ec, 32));
    size_t number = std::min<size_t>(16, expected.size());
    return actual.size() >= number &&
           actual.compare(actual.size() - number, number, expected,
                          expected.size() - number, number) == 0;
  }
  static std::wstring withoutBreaks(const std::wstring &s) {
    std::wstring out;
    for (wchar_t c : s)
      if (c != L'\r' && c != L'\n')
        out += c;
    return out;
  }
  // How many document characters the text `s` occupies right after `origin`, or -1
  // if the document does not hold `s` there. A line break may be stored as "\r\n",
  // "\r" or "\n", so the count can be smaller than s.size().
  LONG insertedLength(ITfRange *origin, TfEditCookie ec, const std::wstring &s) {
    ITfRange *range = nullptr;
    if (FAILED(origin->Clone(&range)))
      return -1;
    LONG moved = 0;
    std::vector<wchar_t> doc(s.size() + 1);
    ULONG read = 0;
    bool ok = SUCCEEDED(range->ShiftEnd(ec, (LONG)s.size(), &moved, nullptr)) &&
              SUCCEEDED(range->GetText(ec, 0, doc.data(), (ULONG)s.size(), &read));
    range->Release();
    if (!ok)
      return -1;
    auto isBreak = [](wchar_t c) { return c == L'\r' || c == L'\n'; };
    size_t i = 0, j = 0;
    while (i < s.size()) {
      if (j >= read)
        return -1;
      if (isBreak(s[i]) && isBreak(doc[j])) {
        i += s[i] == L'\r' && i + 1 < s.size() && s[i + 1] == L'\n' ? 2 : 1;
        j += doc[j] == L'\r' && j + 1 < read && doc[j + 1] == L'\n' ? 2 : 1;
      } else if (s[i] == doc[j]) {
        i++;
        j++;
      } else
        return -1;
    }
    return (LONG)j;
  }
  void track(ITfContext *ctx, ITfRange *sel) {
    if (context != ctx) {
      release(context);
      context = ctx;
      ctx->AddRef();
    }
    release(anchor);
    sel->Clone(&anchor);
  }
  void paint(ITfRange *sel, TfEditCookie ec) {
    if (!popup || (raw.empty() && words.empty())) {
      if (popup)
        ShowWindow(popup, SW_HIDE);
      return;
    }
    std::wstring label = raw;
    for (size_t i = 0; i < words.size(); i++)
      label += L"   " + std::to_wstring(i + 1) + L" " + words[i];
    ITfContextView *view = nullptr;
    RECT rect{};
    BOOL clipped = FALSE;
    if (FAILED(context->GetActiveView(&view)))
      return;
    auto ok = view->GetTextExt(ec, sel, &rect, &clipped);
    view->Release();
    if (FAILED(ok) || clipped) {
      ShowWindow(popup, SW_HIDE);
      return;
    }
    HDC dc = GetDC(popup);
    auto old = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    SIZE size;
    GetTextExtentPoint32W(dc, label.c_str(), (int)label.size(), &size);
    SelectObject(dc, old);
    ReleaseDC(popup, dc);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &monitor);
    int width = size.cx + 20, height = size.cy + 14;
    int x = std::max(monitor.rcWork.left,
                     std::min(rect.left, monitor.rcWork.right - width));
    int y = rect.bottom + height <= monitor.rcWork.bottom ? rect.bottom
                                                          : rect.top - height;
    SetWindowTextW(popup, label.c_str());
    SetWindowPos(popup, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(popup, nullptr, TRUE);
  }
  bool insert(ITfContext *ctx, ITfRange *sel, TfEditCookie ec,
              const std::wstring &s) {
    // Remember where the text starts; backward gravity keeps this point in front of
    // the inserted text.
    ITfRange *origin = nullptr;
    if (FAILED(sel->Clone(&origin)))
      return false;
    origin->Collapse(ec, TF_ANCHOR_START);
    origin->SetGravity(ec, TF_GRAVITY_BACKWARD, TF_GRAVITY_BACKWARD);
    bool ok = SUCCEEDED(sel->SetText(ec, 0, s.data(), (LONG)s.size()));
    if (ok) {
      // Notepad left the caret before a "\r\n..." insertion when the range end was
      // trusted, so measure the inserted text in the document and put the caret
      // after it. Fall back to the range end if the document text differs.
      LONG length = insertedLength(origin, ec, s), moved = 0;
      ITfRange *after = nullptr;
      bool measured = length >= 0 && SUCCEEDED(origin->Clone(&after)) &&
                      SUCCEEDED(after->ShiftEnd(ec, length, &moved, nullptr)) &&
                      moved == length;
      if (measured) {
        after->Collapse(ec, TF_ANCHOR_END);
        // `sel` is the caller's range; move both of its ends onto the new caret.
        sel->ShiftEndToRange(ec, after, TF_ANCHOR_START);
        sel->ShiftStartToRange(ec, after, TF_ANCHOR_START);
      } else {
        sel->Collapse(ec, TF_ANCHOR_END);
      }
      release(after);
      TF_SELECTION cursor{sel, {TF_AE_NONE, FALSE}};
      ok = SUCCEEDED(ctx->SetSelection(ec, 1, &cursor));
    }
    origin->Release();
    if (!ok)
      return false;
    prior += s;
    if (prior.size() > 512)
      prior.erase(0, prior.size() - 512);
    track(ctx, sel);
    return true;
  }
  // Called after clearAll() for a key this service lets through. Ctrl+V (also with
  // Shift) starts the paste question timer; the application does the pasting itself.
  void notePaste(ITfContext *ctx, WPARAM vk) {
    if (vk != 'V' || GetKeyState(VK_CONTROL) >= 0 || GetKeyState(VK_MENU) < 0 ||
        GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0 || restricted ||
        !ime || !popup || !aiEnabled())
      return;
    release(pasteContext);
    pasteContext = ctx;
    ctx->AddRef();
    pasteDue = GetTickCount64() + 600;
    SetTimer(popup, 1, 75, nullptr);
  }
  std::wstring clipboardText() {
    std::wstring text;
    if (!OpenClipboard(popup))
      return text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
      if (auto p = (const wchar_t *)GlobalLock(data)) {
        size_t limit = GlobalSize(data) / sizeof(wchar_t);
        text.assign(p, wcsnlen(p, limit));
        GlobalUnlock(data);
      }
    }
    CloseClipboard();
    return text;
  }
  // Up to `count` characters immediately before the start of the selection.
  std::wstring textBefore(ITfRange *sel, TfEditCookie ec, LONG count) {
    std::wstring text;
    ITfRange *range = nullptr;
    LONG moved = 0;
    if (FAILED(sel->Clone(&range)))
      return text;
    range->Collapse(ec, TF_ANCHOR_START);
    if (SUCCEEDED(range->ShiftStart(ec, -count, &moved, nullptr)) && moved < 0) {
      std::vector<wchar_t> buffer((size_t)-moved);
      ULONG read = 0;
      if (SUCCEEDED(range->GetText(ec, 0, buffer.data(), (ULONG)buffer.size(), &read)))
        text.assign(buffer.data(), read);
    }
    range->Release();
    return text;
  }
  static std::wstring newlinesAsLf(std::wstring s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); i++) {
      if (s[i] == L'\r') {
        out += L'\n';
        if (i + 1 < s.size() && s[i + 1] == L'\n')
          i++;
      } else
        out += s[i];
    }
    return out;
  }
  // The pause after Ctrl+V is over: if the clipboard text (English, at most 2000
  // characters) sits right before the caret, ask about it with the course files.
  void startPasteQuestion() {
    ITfContext *ctx = pasteContext;
    pasteContext = nullptr;
    pasteDue = 0;
    ITfDocumentMgr *doc = nullptr;
    ITfContext *focused = nullptr;
    if (SUCCEEDED(manager->GetFocus(&doc)) && doc) {
      doc->GetTop(&focused);
      doc->Release();
    }
    bool active = ctx && focused == ctx;
    release(focused);
    std::wstring clip = active ? newlinesAsLf(clipboardText()) : L"";
    auto from = clip.find_first_not_of(L" \t\n");
    auto to = clip.find_last_not_of(L" \t\n");
    std::wstring asked =
        from == std::wstring::npos ? L"" : clip.substr(from, to - from + 1);
    if (asked.empty() || clip.size() > maxPaste || hasHan(clip) || !aiEnabled()) {
      release(ctx);
      clearAll();
      return;
    }
    edit(ctx, TF_ES_READ, [&](TfEditCookie ec) {
      TF_SELECTION sel{};
      ULONG count = 0;
      if (FAILED(ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &count)) ||
          !count)
        return;
      BOOL empty = FALSE;
      if (!secure(ctx, sel.range, ec) && SUCCEEDED(sel.range->IsEmpty(ec, &empty)) &&
          empty) {
        std::wstring before = textBefore(sel.range, ec, (LONG)clip.size() + 64);
        std::wstring lf = newlinesAsLf(before);
        if (lf.size() >= clip.size() &&
            lf.compare(lf.size() - clip.size(), clip.size(), clip) == 0) {
          track(ctx, sel.range);
          prior = before.size() > 512 ? before.substr(before.size() - 512) : before;
          question = asked;
          accepted.clear();
          mode = "answer";
          fileMode = true;
          stream = predict(question, accepted, mode, true);
        }
      }
      sel.range->Release();
    });
    release(ctx);
    if (!stream)
      clearAll();
  }
  void poll() {
    if (pasteDue) {
      if (GetTickCount64() >= pasteDue)
        startPasteQuestion();
      return;
    }
    if (!context || !anchor || !aiEnabled()) {
      clearPrediction();
      return;
    }
    ITfDocumentMgr *doc = nullptr;
    ITfContext *focused = nullptr;
    if (SUCCEEDED(manager->GetFocus(&doc)) && doc) {
      doc->GetTop(&focused);
      doc->Release();
    }
    bool active = focused && focused == context;
    release(focused);
    if (!active) {
      clearAll();
      return;
    }
    ITfContext *ctx = context;
    ctx->AddRef();
    edit(ctx, TF_ES_READ, [&](TfEditCookie ec) {
      TF_SELECTION sel{};
      ULONG count = 0;
      if (FAILED(
              ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &count)) ||
          !count) {
        clearAll();
        return;
      }
      if (!sameCaret(sel.range, ec) || secure(ctx, sel.range, ec)) {
        sel.range->Release();
        clearAll();
        return;
      }
      if (due && GetTickCount64() >= due) {
        due = 0;
        stream = predict(question, accepted, mode, fileMode);
      }
      if (stream) {
        bool completed;
        std::vector<std::wstring> next;
        {
          std::lock_guard<std::mutex> guard(stream->lock);
          if (!stream->rows.empty())
            next = stream->rows.front();
          mode = stream->mode;
          completed = stream->done && next.empty();
        }
        if (words != next) {
          words = next;
          paint(sel.range, ec);
        }
        if (completed)
          clearPrediction();
      }
      sel.range->Release();
    });
    ctx->Release();
  }
  int symbol(WPARAM vk, LPARAM flags) {
    switch (vk) {
    case VK_BACK:
      return 0xff08;
    case VK_ESCAPE:
      return 0xff1b;
    case VK_SPACE:
      return 32;
    case VK_LEFT:
      return 0xff51;
    case VK_RIGHT:
      return 0xff53;
    case VK_UP:
      return 0xff52;
    case VK_DOWN:
      return 0xff54;
    case VK_PRIOR:
      return 0xff55;
    case VK_NEXT:
      return 0xff56;
    }
    BYTE keys[256];
    wchar_t text[8];
    if (!GetKeyboardState(keys) ||
        ToUnicodeEx((UINT)vk, (UINT)((flags >> 16) & 255), keys, text, 8, 4,
                    GetKeyboardLayout(0)) != 1)
      return 0;
    return text[0] < 128 ? (int)text[0] : 0;
  }
  bool wants(WPARAM vk, LPARAM flags) {
    if (restricted || !ime || GetKeyState(VK_CONTROL) < 0 ||
        GetKeyState(VK_MENU) < 0 || GetKeyState(VK_LWIN) < 0 ||
        GetKeyState(VK_RWIN) < 0)
      return false;
    if (!raw.empty())
      return vk == VK_RETURN || symbol(vk, flags) != 0;
    if (!words.empty() && (vk == VK_TAB || vk == VK_ESCAPE ||
                           (vk >= '1' && vk < '1' + words.size())))
      return GetKeyState(VK_SHIFT) >= 0;
    if ((stream || due) && vk == VK_ESCAPE)
      return true;
    int ch = symbol(vk, flags);
    return (ch >= 'a' && ch <= 'z') ||
           (ch > 0 && ch < 128 && strchr(",.?!:;()[]/", ch));
  }
  bool key(ITfContext *ctx, ITfRange *sel, TfEditCookie ec, WPARAM vk,
           LPARAM flags) {
    if (context != ctx || (anchor && !sameCaret(sel, ec)))
      clearAll();
    if (secure(ctx, sel, ec) || !wants(vk, flags)) {
      clearAll();
      if (!secure(ctx, sel, ec))
        notePaste(ctx, vk);
      return false;
    }
    if (raw.empty() && vk == VK_ESCAPE) {
      clearPrediction();
      return true;
    }
    if (raw.empty() && !words.empty() &&
        (vk == VK_TAB || (vk >= '1' && vk < '1' + words.size()))) {
      size_t n = vk == VK_TAB ? 0 : vk - '1';
      const std::wstring &before = accepted.empty() ? question : accepted;
      std::wstring word = spaced(words[n], before);
      // An answer follows the question on the same line after one space. Edge
      // inserted the first word twice whenever the insertion began with a line break
      // (single "\n" or "\r\n"), so answers no longer start a new line.
      if (accepted.empty() && mode == "answer" && !before.empty() &&
          !iswspace(before.back()) && !word.empty() && word[0] != L' ')
        word = L" " + word;
      if (!insert(ctx, sel, ec, word)) {
        clearAll();
        return false;
      }
      accepted += word;
      if (n) {
        stream->cancelled = true;
        stream = predict(question, accepted, mode, fileMode);
        words.clear();
      } else {
        std::lock_guard<std::mutex> guard(stream->lock);
        stream->rows.pop_front();
        words = stream->rows.empty() ? std::vector<std::wstring>{}
                                     : stream->rows.front();
      }
      paint(sel, ec);
      return true;
    }
    clearPrediction();
    if (!anchor)
      track(ctx, sel);
    std::wstring commit;
    bool handled = true;
    int ch = symbol(vk, flags);
    // Judge the sentence from the document itself: letters typed with Shift or Caps
    // Lock reach the application directly and never enter `prior`.
    std::wstring typed = textBefore(sel, ec, 512);
    std::wstring sentence = currentSentence(typed.empty() ? prior : typed);
    if (vk == VK_RETURN && !raw.empty()) {
      const char *input = rime()->get_input(ime);
      commit = spaced(wide(input ? input : ""), prior);
      rime()->clear_composition(ime);
    } else if (raw.empty() && ch > 0 && ch < 128 && strchr(",.?!:;()[]/", ch) &&
               !sentence.empty() && !hasHan(sentence)) {
      // Inside an English sentence punctuation stays half-width ("?" not "？").
      commit = spaced(std::wstring(1, (wchar_t)ch), prior);
    } else {
      handled = rime()->process_key(ime, symbol(vk, flags), 0);
      RIME_STRUCT(RimeCommit, c);
      if (rime()->get_commit(ime, &c)) {
        if (c.text)
          commit = wide(c.text);
        rime()->free_commit(&c);
      }
    }
    if (!commit.empty() && !insert(ctx, sel, ec, commit)) {
      clearAll();
      return false;
    }
    raw.clear();
    words.clear();
    RIME_STRUCT(RimeContext, state);
    if (rime()->get_context(ime, &state)) {
      if (state.composition.preedit)
        raw = wide(state.composition.preedit);
      for (int i = 0; i < state.menu.num_candidates; i++)
        words.push_back(wide(state.menu.candidates[i].text));
      rime()->free_context(&state);
    }
    if (raw.empty() && !commit.empty() && aiEnabled()) {
      // Chinese sentence: continue or answer as before, without files. English
      // sentence: only a question ending in "?" is asked, with the course files.
      std::wstring now = textBefore(sel, ec, 512);
      if (now.empty())
        now = prior;
      std::wstring current = currentSentence(now);
      bool ask = true;
      if (hasHan(current)) {
        question = prior;
      } else if (!current.empty() && endsWithQuestionMark(now)) {
        question = current + L"?";
        mode = "answer";
        fileMode = true;
      } else {
        ask = false;
      }
      if (ask) {
        due = GetTickCount64() + 600;
        SetTimer(popup, 1, 75, nullptr);
      }
    }
    paint(sel, ec);
    return handled;
  }

public:
  Service() { ++moduleUsers; }
  ~Service() {
    Deactivate();
    --moduleUsers;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITfTextInputProcessor)
      *out = static_cast<ITfTextInputProcessor *>(this);
    else if (iid == IID_ITfKeyEventSink)
      *out = static_cast<ITfKeyEventSink *>(this);
    else if (iid == IID_ITfThreadMgrEventSink)
      *out = static_cast<ITfThreadMgrEventSink *>(this);
    else
      return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    auto n = --refs;
    if (!n)
      delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr *mgr,
                                     TfClientId client) override {
    manager = mgr;
    manager->AddRef();
    id = client;
    ITfThreadMgrEx *ex = nullptr;
    if (SUCCEEDED(mgr->QueryInterface(IID_ITfThreadMgrEx, (void **)&ex))) {
      DWORD flags = 0;
      ex->GetActiveFlags(&flags);
      restricted = (flags & TF_TMF_SECUREMODE) != 0;
      ex->Release();
    }
    if (restricted)
      return S_OK;
    auto api = rime();
    if (!api || !(ime = api->create_session()) ||
        !api->select_schema(ime, "yuju_pinyin"))
      return E_FAIL;
    WNDCLASSW wc{};
    wc.hInstance = module;
    wc.lpfnWndProc = windowProc;
    wc.lpszClassName = L"YujuWordCandidates";
    RegisterClassW(&wc);
    popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                            wc.lpszClassName, L"", WS_POPUP | WS_BORDER, 0, 0,
                            0, 0, nullptr, nullptr, module, this);
    if (!popup)
      return E_FAIL;
    ITfSource *src = nullptr;
    HRESULT hr = mgr->QueryInterface(IID_ITfSource, (void **)&src);
    if (SUCCEEDED(hr)) {
      hr = src->AdviseSink(IID_ITfThreadMgrEventSink,
                           static_cast<ITfThreadMgrEventSink *>(this), &cookie);
      src->Release();
    }
    if (FAILED(hr))
      return hr;
    ITfKeystrokeMgr *km = nullptr;
    hr = mgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&km);
    if (SUCCEEDED(hr)) {
      hr = km->AdviseKeyEventSink(id, this, TRUE);
      km->Release();
    }
    return hr;
  }
  HRESULT STDMETHODCALLTYPE Deactivate() override {
    if (!manager)
      return S_OK;
    clearAll();
    if (ime) {
      rime()->destroy_session(ime);
      ime = 0;
    }
    ITfKeystrokeMgr *km = nullptr;
    if (SUCCEEDED(manager->QueryInterface(IID_ITfKeystrokeMgr, (void **)&km))) {
      km->UnadviseKeyEventSink(id);
      km->Release();
    }
    ITfSource *src = nullptr;
    if (cookie != TF_INVALID_COOKIE &&
        SUCCEEDED(manager->QueryInterface(IID_ITfSource, (void **)&src))) {
      src->UnadviseSink(cookie);
      src->Release();
      cookie = TF_INVALID_COOKIE;
    }
    if (popup) {
      DestroyWindow(popup);
      popup = nullptr;
    }
    release(manager);
    return S_OK;
  }
  // Shift or Caps Lock on its own only prepares the next key (an upper-case letter,
  // "?"); it must not reset what has been typed so far.
  static bool modifierOnly(WPARAM vk) {
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_CAPITAL;
  }
  HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext *ctx, WPARAM vk, LPARAM lp,
                                          BOOL *eaten) override {
    *eaten = FALSE;
    if (modifierOnly(vk))
      return S_OK;
    if (!wants(vk, lp)) {
      clearAll();
      notePaste(ctx, vk);
      return S_OK;
    }
    edit(ctx, TF_ES_READ, [&](TfEditCookie ec) {
      TF_SELECTION sel{};
      ULONG n = 0;
      if (SUCCEEDED(ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &n)) &&
          n) {
        if (context != ctx || (anchor && !sameCaret(sel.range, ec)))
          clearAll();
        *eaten = !secure(ctx, sel.range, ec) && wants(vk, lp);
        sel.range->Release();
      }
    });
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext *ctx, WPARAM vk, LPARAM lp,
                                      BOOL *eaten) override {
    *eaten = FALSE;
    if (modifierOnly(vk))
      return S_OK;
    edit(ctx, TF_ES_READWRITE, [&](TfEditCookie ec) {
      TF_SELECTION sel{};
      ULONG n = 0;
      if (SUCCEEDED(ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &n)) &&
          n) {
        *eaten = key(ctx, sel.range, ec, vk, lp);
        sel.range->Release();
      }
    });
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext *, WPARAM, LPARAM,
                                        BOOL *e) override {
    *e = FALSE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext *, WPARAM, LPARAM,
                                    BOOL *e) override {
    *e = FALSE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext *, REFGUID,
                                           BOOL *e) override {
    *e = FALSE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL active) override {
    if (!active)
      clearAll();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnSetFocus(ITfDocumentMgr *,
                                       ITfDocumentMgr *) override {
    clearAll();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnInitDocumentMgr(ITfDocumentMgr *) override {
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnUninitDocumentMgr(ITfDocumentMgr *) override {
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnPushContext(ITfContext *) override {
    clearAll();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnPopContext(ITfContext *) override {
    clearAll();
    return S_OK;
  }
};

class Factory final : public IClassFactory {
  ULONG refs = 1;

public:
  Factory() { ++moduleUsers; }
  ~Factory() { --moduleUsers; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **p) override {
    *p = nullptr;
    if (iid != IID_IUnknown && iid != IID_IClassFactory)
      return E_NOINTERFACE;
    *p = this;
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    auto n = --refs;
    if (!n)
      delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID iid,
                                           void **p) override {
    if (outer)
      return CLASS_E_NOAGGREGATION;
    auto service = new Service;
    auto hr = service->QueryInterface(iid, p);
    service->Release();
    return hr;
  }
  HRESULT STDMETHODCALLTYPE LockServer(BOOL locked) override {
    if (locked)
      ++moduleUsers;
    else
      --moduleUsers;
    return S_OK;
  }
};

extern "C" __declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID cls,
                                                                  REFIID iid,
                                                                  void **p) {
  if (cls != clsid)
    return CLASS_E_CLASSNOTAVAILABLE;
  auto f = new Factory;
  auto hr = f->QueryInterface(iid, p);
  f->Release();
  return hr;
}
extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow() {
  return moduleUsers.load() ? S_FALSE : S_OK;
}
extern "C" __declspec(dllexport) HRESULT WINAPI DllRegisterServer() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(module, path, MAX_PATH);
  std::wstring key = std::wstring(comKey) + L"\\InprocServer32";
  auto put = [&](const wchar_t *name, const wchar_t *value) {
    return RegSetKeyValueW(HKEY_CLASSES_ROOT, key.c_str(), name, REG_SZ, value,
                           DWORD((wcslen(value) + 1) * sizeof(wchar_t)));
  };
  if (put(nullptr, path) != ERROR_SUCCESS ||
      put(L"ThreadingModel", L"Apartment") != ERROR_SUCCESS)
    return E_ACCESSDENIED;
  ITfInputProcessorProfiles *profiles = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
      IID_ITfInputProcessorProfiles, (void **)&profiles);
  if (FAILED(hr))
    return hr;
  hr = profiles->Register(clsid);
  const wchar_t *name = L"语句拼音";
  if (SUCCEEDED(hr))
    hr = profiles->AddLanguageProfile(clsid, 0x0804, profile, name,
                                      (ULONG)wcslen(name), path,
                                      (ULONG)wcslen(path), 0);
  if (SUCCEEDED(hr))
    hr = profiles->EnableLanguageProfile(clsid, 0x0804, profile, TRUE);
  profiles->Release();
  if (FAILED(hr))
    return hr;
  ITfCategoryMgr *category = nullptr;
  hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                        IID_ITfCategoryMgr, (void **)&category);
  if (SUCCEEDED(hr)) {
    hr = category->RegisterCategory(clsid, GUID_TFCAT_TIP_KEYBOARD, clsid);
    category->Release();
  }
  return hr;
}
extern "C" __declspec(dllexport) HRESULT WINAPI DllUnregisterServer() {
  ITfInputProcessorProfiles *profiles = nullptr;
  if (SUCCEEDED(CoCreateInstance(
          CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
          IID_ITfInputProcessorProfiles, (void **)&profiles))) {
    profiles->Unregister(clsid);
    profiles->Release();
  }
  ITfCategoryMgr *category = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                 CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                 (void **)&category))) {
    category->UnregisterCategory(clsid, GUID_TFCAT_TIP_KEYBOARD, clsid);
    category->Release();
  }
  auto r = RegDeleteTreeW(HKEY_CLASSES_ROOT, comKey);
  return r == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(r);
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH)
    module = h;
  return TRUE;
}
