#include "MainWin.h"
#include "ModernUi.h"
#include "resource.h"
#include <algorithm>

bool MainWin::dockedNavigation() const
{
    RECT rect; GetClientRect(m_hWnd, &rect);
    return DisplayMode::NORMAL == m_data.window.mode && rect.right >= ModernUi::px(760)
        && m_navigationOpen && m_session && m_session->isValid() && m_session->book()->chapterCount() > 0;
}

void MainWin::createModernUi()
{
    SetMenu(m_hWnd, nullptr);
    SetWindowLongPtr(m_hWnd, GWL_STYLE, GetWindowLongPtr(m_hWnd, GWL_STYLE) | WS_CLIPCHILDREN);
    m_header = CreateWindowW(L"STATIC", L"Reader 工具栏", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, m_hWnd, nullptr, m_hInstance, nullptr);
    m_welcome = CreateWindowW(L"STATIC", L"阅读空间", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, m_hWnd, nullptr, m_hInstance, nullptr);
    for (HWND bar : {m_header, m_hStatusBar, m_welcome})
    {
        SetWindowSubclass(bar, modernBarProc, 10, (DWORD_PTR)this);
        if (bar != m_hStatusBar) SetPropW(bar, L"Reader.Surface", (HANDLE)1);
    }
    auto add = [&](HWND parent, int id, const wchar_t* label, bool primary = false) {
        HWND button = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, m_hInstance, nullptr);
        ModernUi::button(button, primary); m_shellButtons[id] = button;
    };
    add(m_header, IDM_OPEN, L"打开书籍", true);
    add(m_header, IDM_VIEW, L"目录");
    add(m_header, IDM_DISPLAY, L"外观");
    add(m_header, IDM_UI_MORE, L"更多");
    add(m_hStatusBar, IDM_UI_FOCUS, L"专注");
    add(m_hStatusBar, IDM_UI_PREVIOUS, L"上一页");
    add(m_hStatusBar, IDM_UI_NEXT, L"下一页");
    add(m_welcome, IDC_UI_WELCOME_OPEN, L"打开一本书   Ctrl + O", true);
    // Rounded native title bar on supported Windows versions; older systems
    // keep their standard non-client frame without a new link-time dependency.
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        using SetAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto set = (SetAttribute)GetProcAddress(dwm, "DwmSetWindowAttribute");
        DWORD corners = 2;
        if (set) set(m_hWnd, 33, &corners, sizeof(corners));
        FreeLibrary(dwm);
    }
}

void MainWin::layoutModernUi()
{
    if (!m_header || !m_session) return;
    using ModernUi::px;
    RECT client; GetClientRect(m_hWnd, &client);
    bool normal = m_data.window.mode == DisplayMode::NORMAL;
    ShowWindow(m_header, normal ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatusBar, normal ? SW_SHOW : SW_HIDE);
    SetWindowPos(m_header, HWND_TOP, 0, 0, client.right, px(72), SWP_NOACTIVATE);
    SetWindowPos(m_hStatusBar, HWND_TOP, 0, max(0, client.bottom - px(56)), client.right, px(56), SWP_NOACTIVATE);
    int x = client.right - px(20);
    for (auto entry : {std::make_pair(IDM_UI_MORE, 56), std::make_pair(IDM_DISPLAY, 64), std::make_pair(IDM_VIEW, 64), std::make_pair(IDM_OPEN, client.right >= px(620) ? 108 : 86)}) {
        x -= px(entry.second);
        SetWindowPos(m_shellButtons[entry.first], nullptr, x, px(20), px(entry.second), px(34), SWP_NOZORDER | SWP_NOACTIVATE);
        x -= px(8);
    }
    x = client.right - px(20);
    for (auto entry : {std::make_pair(IDM_UI_NEXT, 76), std::make_pair(IDM_UI_PREVIOUS, 76), std::make_pair(IDM_UI_FOCUS, 60)}) {
        x -= px(entry.second);
        SetWindowPos(m_shellButtons[entry.first], nullptr, x, px(11), px(entry.second), px(32), SWP_NOZORDER | SWP_NOACTIVATE);
        x -= px(8);
    }
    bool valid = m_session->isValid();
    for (int id : {IDM_VIEW, IDM_UI_PREVIOUS, IDM_UI_NEXT}) EnableWindow(m_shellButtons[id], valid);
    bool dock = dockedNavigation();
    RECT area = drawTextRect();
    bool contents = valid && (dock || m_overlayNavigation);
    ShowWindow(m_hTreeView, contents && !m_overlayMarks ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hTreeMark, valid && m_overlayMarks ? SW_SHOW : SW_HIDE);
    if (contents) {
        RECT tree = area;
        if (dock && !m_overlayNavigation) tree = {px(30), px(152), px(238), client.bottom - px(84)};
        else { tree.left += px(12); tree.top += px(16); tree.right -= px(12); tree.bottom -= px(16); }
        SetWindowPos(m_hTreeView, HWND_TOP, tree.left, tree.top, max(1L, tree.right - tree.left), max(1L, tree.bottom - tree.top), SWP_NOACTIVATE);
    }
    if (m_overlayMarks) SetWindowPos(m_hTreeMark, HWND_TOP, area.left + px(12), area.top + px(16), max(1L, area.right - area.left - px(24)), max(1L, area.bottom - area.top - px(32)), SWP_NOACTIVATE);
    bool welcome = normal && !valid && !m_session->isLoading();
    ShowWindow(m_welcome, welcome ? SW_SHOW : SW_HIDE);
    if (welcome) {
        int width = max(1L, area.right - area.left), height = max(1L, area.bottom - area.top);
        SetWindowPos(m_welcome, HWND_TOP, area.left, area.top, width, height, SWP_NOACTIVATE);
        SetWindowRgn(m_welcome, CreateRoundRectRgn(0, 0, width, height, px(14), px(14)), TRUE);
        int center = height / 2;
        SetWindowPos(m_shellButtons[IDC_UI_WELCOME_OPEN], nullptr, (width - px(220)) / 2, center + px(height >= px(360) ? 80 : 24), px(220), px(42), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    InvalidateRect(m_hWnd, nullptr, FALSE);
    InvalidateRect(m_header, nullptr, FALSE);
    InvalidateRect(m_welcome, nullptr, FALSE);
    InvalidateRect(m_hStatusBar, nullptr, FALSE);
}

void MainWin::paintModernFrame(HDC dc)
{
    using namespace ModernUi;
    RECT client; GetClientRect(m_hWnd, &client); fill(dc, client, background());
    RECT page = drawTextRect(); round(dc, page, surface(), line(), 14);
    if (dockedNavigation()) {
        RECT card = {px(20), px(88), px(248), client.bottom - px(72)};
        round(dc, card, surface(), line(), 14);
        RECT heading = {px(36), px(100), px(228), px(126)};
        text(dc, L"章节目录", heading, ink(), 11, FW_SEMIBOLD);
        RECT subtitle = {px(36), px(128), px(228), px(146)};
        text(dc, std::to_wstring(m_session->book()->chapterCount()) + L" 个章节 · 按顺序阅读", subtitle, muted(), 8);
    }
}

void MainWin::paintModernBar(HWND window, HDC dc)
{
    using namespace ModernUi;
    RECT rect; GetClientRect(window, &rect);
    const int width = rect.right, height = rect.bottom;
    if (window == m_header) {
        fill(dc, rect, surface());
        if (width >= px(620)) {
            RECT icon = {px(24), px(20), px(58), px(54)}; round(dc, icon, accent(), accent(), 10);
            text(dc, L"R", icon, RGB(255,255,255), 15, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            text(dc, L"Reader", {px(70), px(17), px(166), px(43)}, ink(), 17, FW_SEMIBOLD);
            text(dc, L"静下心，读好书", {px(72), px(42), px(200), px(60)}, muted(), 8);
            if (width >= px(880) && m_session->isValid()) text(dc, m_session->book()->bookName(), {px(210), px(24), width - px(370), px(51)}, muted(), 9);
        } else text(dc, L"Reader", {px(20), px(20), px(122), px(52)}, ink(), 15, FW_SEMIBOLD);
        fill(dc, {0, height - 1, width, height}, line());
    } else if (window == m_welcome) {
        fill(dc, rect, surface());
        int center = height / 2;
        if (height >= px(360)) {
        RECT icon = {width / 2 - px(36), center - px(116), width / 2 + px(36), center - px(44)};
        round(dc, icon, RGB(239,236,253), RGB(239,236,253), 24);
        RECT book = {width / 2 - px(17), center - px(100), width / 2 + px(17), center - px(60)};
        round(dc, book, accent(), accent(), 6);
        fill(dc, {book.left + px(6), book.top + px(10), book.right - px(6), book.top + px(12)}, RGB(221,215,255));
        fill(dc, {book.left + px(6), book.top + px(18), book.right - px(12), book.top + px(20)}, RGB(221,215,255));
        }
        bool roomy = height >= px(360);
        text(dc, L"下一段故事，从这里开始", {px(12), center - px(roomy ? 24 : 70), width - px(12), center + px(roomy ? 17 : -35)}, ink(), width >= px(550) ? 22 : 16, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        text(dc, L"为文字留一点空间，也为自己留一点时间。", {px(12), center + px(roomy ? 28 : -27), width - px(12), center + px(roomy ? 52 : 8)}, muted(), 10, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        text(dc, L"TXT  /  EPUB  /  MOBI     ·     拖入文件即可阅读", {px(12), center + px(roomy ? 142 : 78), width - px(12), center + px(roomy ? 166 : 102)}, muted(), 9, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        fill(dc, rect, background());
        int available = max(px(48), width - px(298));
        text(dc, caption(window), {px(24), px(6), available, px(28)}, muted(), 9);
        RECT track = {px(24), px(37), available, px(41)};
        round(dc, track, RGB(223,225,235), RGB(223,225,235), 4);
        if (m_session->isValid()) {
            track.right = track.left + (int)((track.right - track.left) * max(0.0f, min(1.0f, m_session->progress())));
            if (track.right > track.left) round(dc, track, accent(), accent(), 4);
        }
    }
}

LRESULT CALLBACK MainWin::modernBarProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
{
    auto self = (MainWin*)data;
    if (message == WM_COMMAND) return SendMessage(self->m_hWnd, message, wp, lp);
    if (message == WM_PRINTCLIENT) { self->paintModernBar(window, (HDC)wp); return 0; }
    if (message == WM_PAINT) {
        PAINTSTRUCT ps; auto dc = BeginPaint(window, &ps);
        self->paintModernBar(window, dc); EndPaint(window, &ps); return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_NCDESTROY) { RemovePropW(window, L"Reader.Surface"); RemoveWindowSubclass(window, modernBarProc, 10); }
    return DefSubclassProc(window, message, wp, lp);
}

void MainWin::showMoreMenu()
{
    HMENU menu = CreatePopupMenu();
    HMENU recent = m_hMenu ? GetSubMenu(m_hMenu, 0) : nullptr;
    if (recent) AppendMenuW(menu, MF_POPUP, (UINT_PTR)recent, L"文件与最近阅读");
    AppendMenuW(menu, MF_STRING, IDM_MARK, L"书签列表");
    AppendMenuW(menu, MF_STRING, IDM_UI_ADD_MARK, L"添加书签\tCtrl+M");
    AppendMenuW(menu, MF_STRING, IDM_UI_SEARCH, L"全文查找\tCtrl+F");
    AppendMenuW(menu, MF_STRING, IDM_UI_JUMP, L"跳转进度\tCtrl+G");
    AppendMenuW(menu, MF_STRING, IDM_UI_AUTOPAGE, L"自动翻页\tSpace");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_CONFIG, L"翻页与阅读行为");
    AppendMenuW(menu, MF_STRING, IDM_KEYSET, L"快捷键设置");
    AppendMenuW(menu, MF_STRING, IDM_ADVSET, L"高级设置");
    AppendMenuW(menu, MF_STRING, IDM_ONLINE, L"在线阅读");
    AppendMenuW(menu, MF_STRING, IDM_WEB, L"Web 书库");
    AppendMenuW(menu, MF_STRING, IDM_NETWORK, L"网络设置");
    AppendMenuW(menu, MF_STRING, IDM_UI_FULLSCREEN, L"全屏阅读\tF11");
    AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"关于 Reader");
    RECT button; GetWindowRect(m_shellButtons[IDM_UI_MORE], &button);
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN, button.right, button.bottom + ModernUi::px(6), 0, m_hWnd, nullptr);
    if (recent) RemoveMenu(menu, 0, MF_BYPOSITION); // borrowed submenu remains owned by m_hMenu
    DestroyMenu(menu);
    if (command) SendMessage(m_hWnd, WM_COMMAND, command, 0);
}
