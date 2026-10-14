#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#define WINVER 0x0601
#define _WIN32_WINNT 0x0601
#define _WIN32_DCOM
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <uiautomation.h>
#include "relayout.rc.h"

/* The SDK declares CLSID_CUIAutomation as extern; its data would come
   from a lib we skip under /NODEFAULTLIB.  Define our own copy. */
static const CLSID kCLSID_CUIAutomation =
    {0xff48dba4, 0x60ef, 0x4201,
     {0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e}};

/* ---------------- constants ---------------- */

static constexpr LPCWSTR APP_ID    = L"Relayout";
static constexpr LPCWSTR APP_TITLE = L"Relayout";

static constexpr UINT WM_TRAY  = WM_APP + 1;
static constexpr UINT TIMER_ID = 1;

static constexpr UINT MENU_AUTO = 100;
static constexpr UINT MENU_NOW  = 101;
static constexpr UINT MENU_EXIT = 102;

static constexpr UINT POLL_MS = 100;

/* ---------------- library + OS helpers ---------------- */

struct LibraryLoader
{
    HMODULE module;
    LibraryLoader(const LibraryLoader &)            = delete;
    LibraryLoader &operator=(const LibraryLoader &) = delete;

    LibraryLoader(LPCWSTR name)
    {
        module = GetModuleHandleW(name);
        if (!module) module = LoadLibraryW(name);
    }

    template <typename T>
    T proc(const char *name) const
    {
        return reinterpret_cast<T>(reinterpret_cast<void *>(GetProcAddress(module, name)));
    }
};

static void enable_hidpi()
{
    LibraryLoader user32(L"user32.dll");
    if (auto pSetProcessDpiAwarenessContext = user32.proc<BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT)>("SetProcessDpiAwarenessContext"))
        pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    else if (auto pSetProcessDPIAware = user32.proc<BOOL(WINAPI *)()>("SetProcessDPIAware"))
        pSetProcessDPIAware();
}

static void enable_dark_mode_support()
{
    DWORD build = 0;
    {
        LibraryLoader ntdll(L"ntdll.dll");
        if (auto pRtlGetVersion = ntdll.proc<LONG(WINAPI *)(OSVERSIONINFOW *)>("RtlGetVersion")) {
            OSVERSIONINFOW osvi;
            SecureZeroMemory(&osvi, sizeof(osvi));
            osvi.dwOSVersionInfoSize = sizeof(osvi);
            if (pRtlGetVersion(&osvi) == 0) build = osvi.dwBuildNumber;
        }
    }
    if (build < 17763)
        return;
    LibraryLoader uxtheme(L"uxtheme.dll");
    if (build < 18362)
    {
        if (auto pAllowDarkModeForApp = uxtheme.proc<bool(WINAPI *)(bool allow)>(
                MAKEINTRESOURCEA(135))) // undocumented AllowDarkModeForApp
            pAllowDarkModeForApp(true);
    }
    else
    {
        enum class PreferredAppMode : INT
        {
            Default,
            AllowDark,
            ForceDark,
            ForceLight,
            Max
        };
        if (auto pSetPreferredAppMode = uxtheme.proc<INT(WINAPI *)(PreferredAppMode value)>(
                MAKEINTRESOURCEA(135))) // undocumented SetPreferredAppMode
            pSetPreferredAppMode(PreferredAppMode::AllowDark);
    }
}

/* ---------------- input simulation ---------------- */

/* Type a string as literal Unicode.  Bypasses the active layout and any
   IME, so what we send is what appears -- independent of whatever
   layout the target window currently has selected. */
static void send_unicode_string(const WCHAR *s)
{
    for (; *s; ++s) {
        INPUT in[2];
        SecureZeroMemory(in, sizeof(in));
        in[0].type       = INPUT_KEYBOARD;
        in[0].ki.wScan   = *s;
        in[0].ki.dwFlags = KEYEVENTF_UNICODE;
        in[1]            = in[0];
        in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
    }
}

/* ---------------- UI Automation ---------------- */

/* Read the current selection via TextPattern.  Returns a GlobalAlloc'd
   wide string (caller frees with GlobalFree) or NULL if the focused
   element doesn't support TextPattern or has no selection. */
static WCHAR *uia_read_selection(IUIAutomation *automation)
{
    IUIAutomationElement        *elem    = NULL;
    IUIAutomationTextPattern    *textpat = NULL;
    IUIAutomationTextRangeArray *ranges  = NULL;
    IUIAutomationTextRange      *range   = NULL;
    BSTR    sel    = NULL;
    WCHAR  *result = NULL;
    int     count  = 0;
    HRESULT hr;
    UINT    len, i;

    hr = automation->GetFocusedElement(&elem);
    if (FAILED(hr) || !elem) goto done;

    hr = elem->GetCurrentPatternAs(UIA_TextPatternId,
                                   IID_PPV_ARGS(&textpat));
    if (FAILED(hr) || !textpat) goto done;

    hr = textpat->GetSelection(&ranges);
    if (FAILED(hr) || !ranges) goto done;

    hr = ranges->get_Length(&count);
    if (FAILED(hr) || count == 0) goto done;

    hr = ranges->GetElement(0, &range);
    if (FAILED(hr) || !range) goto done;

    hr = range->GetText(-1, &sel);
    if (FAILED(hr) || !sel) goto done;

    len = SysStringLen(sel);
    if (len == 0) goto done;

    result = (WCHAR *)GlobalAlloc(GMEM_FIXED, (len + 1) * sizeof(WCHAR));
    if (result)
        for (i = 0; i <= len; ++i) result[i] = sel[i];

done:
    if (sel)     SysFreeString(sel);
    if (range)   range->Release();
    if (ranges)  ranges->Release();
    if (textpat) textpat->Release();
    if (elem)    elem->Release();
    return result;
}

/* ---------------- layout translation ---------------- */

/* Reinterpret a single character as if the same physical key press that
   produced it on `from` had been made on `to`, using the layout DLLs. */
static WCHAR translate_char(WCHAR c, HKL from, HKL to)
{
    SHORT vks;
    BYTE  keys[256];
    WCHAR buf[4];
    UINT  vk, sc;
    int   n;

    vks = VkKeyScanExW(c, from);
    if (vks == -1) return c;

    vk = (BYTE)vks;
    SecureZeroMemory(keys, sizeof(keys));
    if (vks & 0x0100) keys[VK_SHIFT]   = 0x80;
    if (vks & 0x0200) keys[VK_CONTROL] = 0x80;
    if (vks & 0x0400) keys[VK_MENU]    = 0x80;

    sc = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, from);
    n  = ToUnicodeEx(vk, sc, keys, buf, 4, 0, to);

    if (n == 1) return buf[0];
    if (n < 0)  ToUnicodeEx(VK_SPACE, 0, keys, buf, 4, 0, to);
    return c;
}

static void transform_text(WCHAR *s, HKL from, HKL to)
{
    BYTE  keys[256];
    WCHAR buf[4];

    if (from == to || !from || !to) return;

    SecureZeroMemory(keys, sizeof(keys));
    ToUnicodeEx(VK_SPACE, 0, keys, buf, 4, 0, from);
    ToUnicodeEx(VK_SPACE, 0, keys, buf, 4, 0, to);

    for (; *s; ++s) *s = translate_char(*s, from, to);
}

static HKL find_other_layout(HKL current)
{
    HKL list[16];
    int n = GetKeyboardLayoutList(16, list);
    for (int i = 0; i < n; ++i)
        if (list[i] != current) return list[i];
    return current;
}

/* ---------------- app state ---------------- */

struct app_state_t
{
    HWND            hwnd;
    HINSTANCE       hInst;
    HKL             last_layout;
    HKL             prev_layout;
    BOOL            auto_transform = true;
    UINT            wm_taskbar_created;
    UINT            wm_show_menu;
    NOTIFYICONDATAW *nid;
    IUIAutomation   *automation;
    HICON           app_icon;

    app_state_t(NOTIFYICONDATAW *nid_, HINSTANCE hInst_)
        : hwnd(NULL), hInst(hInst_),
          last_layout(NULL), prev_layout(NULL),
          auto_transform(true), wm_taskbar_created(0),
          wm_show_menu(0), nid(nid_), automation(NULL),
          app_icon(NULL) {}

    app_state_t(const app_state_t &)            = delete;
    app_state_t &operator=(const app_state_t &) = delete;

    static LRESULT CALLBACK TrayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto *state = reinterpret_cast<app_state_t *>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (state) return state->handle_message(hwnd, msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    HKL foreground_layout() const
    {
        HWND fg = GetForegroundWindow();
        DWORD tid;
        HKL h;
        if (!fg) return GetKeyboardLayout(0);
        tid = GetWindowThreadProcessId(fg, NULL);
        if (!tid) return GetKeyboardLayout(0);
        h = GetKeyboardLayout(tid);
        return h ? h : GetKeyboardLayout(0);
    }

    void add_tray_icon()
    {
        if (!app_icon)
            app_icon = (HICON)LoadImageW(hInst,
                                         MAKEINTRESOURCEW(IDI_APP_ICON),
                                         IMAGE_ICON,
                                         GetSystemMetrics(SM_CXSMICON),
                                         GetSystemMetrics(SM_CYSMICON),
                                         0);
        nid->cbSize           = sizeof(*nid);
        nid->hWnd             = hwnd;
        nid->uID              = 1;
        nid->uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid->uCallbackMessage = WM_TRAY;
        nid->hIcon            = app_icon ? app_icon
                                         : LoadIconW(NULL, IDI_INFORMATION);
        wsprintfW(nid->szTip, L"%s - right-click for options", APP_TITLE);
        Shell_NotifyIconW(NIM_ADD, nid);
    }

private:
    void poll()
    {
        HKL h = foreground_layout();
        if (h == last_layout) return;
        prev_layout = last_layout;
        last_layout = h;
        if (auto_transform) transform_selection(prev_layout, last_layout);
    }

    /* The synthetic Unicode keystrokes we inject should not compete with
       the user's physical Alt/Shift/Ctrl still being held, or the target
       app may interpret some of them as accelerators. */
    void wait_for_modifiers_up()
    {
        for (int i = 0; i < 30; ++i) {
            if (!(GetAsyncKeyState(VK_MENU)    & 0x8000) &&
                !(GetAsyncKeyState(VK_SHIFT)   & 0x8000) &&
                !(GetAsyncKeyState(VK_CONTROL) & 0x8000))
                return;
            Sleep(10);
        }
    }

    void transform_selection(HKL from, HKL to)
    {
        WCHAR *text;
        HWND   fg_before, fg_after;

        if (!automation) return;

        wait_for_modifiers_up();

        fg_before = GetForegroundWindow();
        if (!fg_before) return;

        text = uia_read_selection(automation);
        if (!text) return;   /* nothing selected, or UIA unavailable here */

        transform_text(text, from, to);

        /* If focus moved while we were reading, the keystrokes would land
           somewhere unintended.  Bail out. */
        fg_after = GetForegroundWindow();
        if (fg_after != fg_before) { GlobalFree(text); return; }

        send_unicode_string(text);
        GlobalFree(text);
    }

    void manual_transform()
    {
        HKL cur = foreground_layout();
        HKL from;

        if (prev_layout && last_layout && cur == last_layout)
            from = prev_layout;
        else
            from = find_other_layout(cur);

        transform_selection(from, cur);
    }

    void show_tray_menu()
    {
        HMENU menu = CreatePopupMenu();
        POINT p;

        AppendMenuW(menu, static_cast<UINT>(MF_STRING) | (auto_transform ? MF_CHECKED : 0),
                    MENU_AUTO, L"Transform on layout switch");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, MENU_NOW, L"Transform selection now");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, MENU_EXIT, L"Exit");

        GetCursorPos(&p);
        SetForegroundWindow(hwnd);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, NULL);
        PostMessageW(hwnd, WM_NULL, 0, 0);
        DestroyMenu(menu);
    }

    LRESULT handle_message(HWND h, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (wm_taskbar_created && msg == wm_taskbar_created) {
            add_tray_icon();
            return 0;
        }

        /* A second instance asked us to surface the menu. */
        if (wm_show_menu && msg == wm_show_menu) {
            show_tray_menu();
            return 0;
        }

        switch (msg) {
        case WM_TRAY:
            if (LOWORD(lParam) == WM_RBUTTONUP ||
                LOWORD(lParam) == WM_CONTEXTMENU)
                show_tray_menu();
            else if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
                manual_transform();
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case MENU_AUTO: auto_transform = !auto_transform; return 0;
            case MENU_NOW:  manual_transform();               return 0;
            case MENU_EXIT: DestroyWindow(h);                 return 0;
            }
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_ID) poll();
            return 0;

        case WM_DESTROY:
            KillTimer(h, TIMER_ID);
            Shell_NotifyIconW(NIM_DELETE, nid);
            if (app_icon) { DestroyIcon(app_icon); app_icon = NULL; }
            if (automation) { automation->Release(); automation = NULL; }
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(h, msg, wParam, lParam);
    }
};

/* ---------------- entry point ---------------- */

extern "C" [[noreturn]] void start();

void start()
{
    /* Single-instance guard.  The default kernel namespace is
       per-session, so each logged-in user can run their own copy. */
    HANDLE instance_mutex = CreateMutexW(NULL, FALSE, APP_ID);
    if (instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        /* Another copy is already running.  Ask it to pop its tray
           menu, then step aside. */
        UINT msg = RegisterWindowMessageW(L"RelayoutShowMenu");
        if (msg) PostMessageW(HWND_BROADCAST, msg, 0, 0);
        CloseHandle(instance_mutex);
        ExitProcess(0);
    }

    enable_hidpi();
    enable_dark_mode_support();

    /* COM must be initialized on the thread that calls UI Automation,
       and that has to be the same thread that pumps the message loop. */
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        ExitProcess(1);

    HINSTANCE hInst = GetModuleHandleW(NULL);

    WNDCLASSEXW wc;
    SecureZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = app_state_t::TrayProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = APP_ID;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON),
                                 IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON),
                                 GetSystemMetrics(SM_CYICON), 0);
    if (!wc.hIcon) wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON),
                                   IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc))
        ExitProcess(1);

    NOTIFYICONDATAW nid;
    app_state_t state(&nid, hInst);
    state.wm_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    state.wm_show_menu       = RegisterWindowMessageW(L"RelayoutShowMenu");

    /* Create the automation object once and keep it for the process
       lifetime.  If this fails the app still runs, just does nothing. */
    {
        IUIAutomation *uia = NULL;
        HRESULT hr = CoCreateInstance(kCLSID_CUIAutomation, NULL,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&uia));
        if (SUCCEEDED(hr)) state.automation = uia;
    }

    state.hwnd = CreateWindowExW(0, APP_ID, APP_TITLE, 0, 0, 0, 0, 0,
                                 NULL, NULL, hInst, NULL);
    if (!state.hwnd)
        ExitProcess(1);

    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(&state));

    state.add_tray_icon();
    state.last_layout = state.foreground_layout();

    SetTimer(state.hwnd, TIMER_ID, POLL_MS, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    if (instance_mutex) CloseHandle(instance_mutex);
    ExitProcess(0);
}
