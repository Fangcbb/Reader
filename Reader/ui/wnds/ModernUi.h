#pragma once
#include "framework.h"
#include "DpiAwareness.h"
#include "Settings.h"
#include <map>
#include <string>

// Shared shell and dialog primitives. Native controls retain their keyboard,
// accessibility and notification behavior; only push-button painting changes.
namespace ModernUi
{
inline int px(int value) { int result; DpiAwareness::instance().logicalToPhysical(value, result); return result; }
inline UiColors::Palette palette() { return UiColors::make(Settings::instance().data().uiTheme); }
inline COLORREF background() { return palette().background; }
inline COLORREF surface() { return palette().surface; }
inline COLORREF ink() { return palette().text; }
inline COLORREF muted() { return palette().muted; }
inline COLORREF accent() { return palette().accent; }
inline COLORREF line() { return palette().border; }
inline COLORREF tint() { return palette().tint; }
inline bool dark() { return UiColors::luminance(surface()) < .179; }
inline HFONT font(int size = 10, int weight = FW_NORMAL)
{
    struct Fonts {
        std::map<int, HFONT> values;
        ~Fonts() { for (auto& entry : values) DeleteObject(entry.second); }
    };
    static Fonts fonts;
    int key = px(size) * 1000 + weight;
    auto it = fonts.values.find(key);
    if (it != fonts.values.end()) return it->second;
    HFONT value = CreateFontW(-MulDiv(size, DpiAwareness::instance().dpi(), 72), 0, 0, 0, weight,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    fonts.values[key] = value;
    return value;
}
inline void fill(HDC dc, RECT rect, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color); FillRect(dc, &rect, brush); DeleteObject(brush);
}
inline void round(HDC dc, RECT rect, COLORREF color, COLORREF border, int radius = 10)
{
    HBRUSH brush = CreateSolidBrush(color); HPEN pen = CreatePen(PS_SOLID, 1, border);
    auto oldBrush = SelectObject(dc, brush); auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, px(radius), px(radius));
    SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(pen); DeleteObject(brush);
}
inline void text(HDC dc, const std::wstring& value, RECT rect, COLORREF color, int size = 10,
    int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS)
{
    auto old = SelectObject(dc, font(size, weight));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), (int)value.size(), &rect, flags);
    SelectObject(dc, old);
}
inline HBRUSH brush(bool card)
{
    struct Brushes {
        HBRUSH values[2] = {}; COLORREF colors[2] = {CLR_INVALID, CLR_INVALID};
        ~Brushes() { for (auto value : values) if (value) DeleteObject(value); }
    };
    static Brushes brushes;
    int index = card ? 1 : 0; COLORREF color = card ? surface() : background();
    if (brushes.colors[index] != color) {
        if (brushes.values[index]) DeleteObject(brushes.values[index]);
        brushes.values[index] = CreateSolidBrush(color); brushes.colors[index] = color;
    }
    return brushes.values[index];
}
inline void nativeTheme(HWND window)
{
    using SetTheme = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    static HMODULE library = LoadLibraryW(L"uxtheme.dll");
    static auto set = library ? (SetTheme)GetProcAddress(library, "SetWindowTheme") : nullptr;
    if (set) set(window, dark() ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}
inline void windowTheme(HWND root)
{
    if (HMODULE library = LoadLibraryW(L"dwmapi.dll")) {
        using SetAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto set = (SetAttribute)GetProcAddress(library, "DwmSetWindowAttribute");
        BOOL enabled = dark();
        if (set && FAILED(set(root, 20, &enabled, sizeof(enabled)))) set(root, 19, &enabled, sizeof(enabled));
        FreeLibrary(library);
    }
    EnumChildWindows(root, [](HWND child, LPARAM) -> BOOL { nativeTheme(child); return TRUE; }, 0);
    RedrawWindow(root, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME | RDW_ERASE);
}
inline std::wstring caption(HWND window)
{
    int length = GetWindowTextLengthW(window);
    std::wstring value(length + 1, 0);
    GetWindowTextW(window, &value[0], length + 1); value.resize(length); return value;
}
inline bool inCard(HWND parent, HWND child);
inline LRESULT CALLBACK buttonProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (message == WM_PAINT || message == WM_PRINTCLIENT)
    {
        PAINTSTRUCT ps; HDC dc = message == WM_PRINTCLIENT ? (HDC)wp : BeginPaint(window, &ps); RECT rect; GetClientRect(window, &rect);
        bool primary = GetDlgCtrlID(window) == IDOK || GetPropW(window, L"Reader.Primary");
        bool hover = GetPropW(window, L"Reader.Hover") != nullptr;
        bool pressed = (SendMessage(window, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
        bool enabled = IsWindowEnabled(window) != FALSE;
        COLORREF color = !enabled ? UiColors::mix(surface(), ink(), 6) : primary
            ? (pressed ? UiColors::mix(accent(), RGB(0,0,0), 16) : hover ? UiColors::mix(accent(), RGB(255,255,255), 12) : accent())
            : pressed ? UiColors::mix(surface(), accent(), 24) : hover ? tint() : surface();
        fill(dc, rect, (GetPropW(GetParent(window), L"Reader.Surface") || inCard(GetParent(window), window)) ? surface() : background()); InflateRect(&rect, -1, -1);
        round(dc, rect, color, primary ? color : line(), 10);
        if (GetPropW(window, L"Reader.ThemeToggle"))
        {
            int x = (rect.left + rect.right) / 2, y = (rect.top + rect.bottom) / 2;
            HPEN pen = CreatePen(PS_SOLID, px(1), ink()); auto oldPen = SelectObject(dc, pen);
            auto oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            if (Settings::instance().data().uiTheme.darkMode) {
                Ellipse(dc, x-px(5), y-px(5), x+px(5), y+px(5));
                const int points[][2] = {{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
                for (const auto& p : points) { MoveToEx(dc, x+p[0]*px(8), y+p[1]*px(8), nullptr); LineTo(dc, x+p[0]*px(11), y+p[1]*px(11)); }
            } else {
                HBRUSH moon = CreateSolidBrush(ink()); SelectObject(dc, moon);
                Ellipse(dc, x-px(8), y-px(8), x+px(8), y+px(8));
                HBRUSH cut = CreateSolidBrush(color); HPEN cutPen = CreatePen(PS_SOLID, 1, color);
                SelectObject(dc, cut); SelectObject(dc, cutPen);
                Ellipse(dc, x-px(3), y-px(11), x+px(12), y+px(4));
                SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(cutPen); DeleteObject(cut); DeleteObject(moon);
            }
            SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(pen);
        }
        else text(dc, caption(window), rect, !enabled ? muted() : primary ? UiColors::readable(color) : ink(), 9,
            primary ? FW_SEMIBOLD : FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (GetFocus() == window && !(SendMessage(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
        {
            RECT focus = rect; InflateRect(&focus, -px(4), -px(4)); DrawFocusRect(dc, &focus);
        }
        if (message == WM_PAINT) EndPaint(window, &ps); return 0;
    }
    if (message == WM_MOUSEMOVE)
    {
        if (!GetPropW(window, L"Reader.Hover")) {
            SetPropW(window, L"Reader.Hover", (HANDLE)1);
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
            InvalidateRect(window, nullptr, FALSE);
        }
    }
    if (message == WM_MOUSELEAVE) { RemovePropW(window, L"Reader.Hover"); InvalidateRect(window, nullptr, FALSE); }
    if (message == WM_NCDESTROY) { RemovePropW(window, L"Reader.Hover"); RemovePropW(window, L"Reader.Primary"); RemovePropW(window, L"Reader.ThemeToggle"); RemoveWindowSubclass(window, buttonProc, 1); }
    LRESULT result = DefSubclassProc(window, message, wp, lp);
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE || message == WM_KEYDOWN || message == WM_KEYUP)
        InvalidateRect(window, nullptr, FALSE);
    return result;
}
inline void button(HWND window, bool primary = false)
{
    if (primary) SetPropW(window, L"Reader.Primary", (HANDLE)1);
    SetWindowSubclass(window, buttonProc, 1, 0);
}
inline LRESULT CALLBACK groupProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (message == WM_PAINT || message == WM_PRINTCLIENT) {
        PAINTSTRUCT ps; auto dc = message == WM_PRINTCLIENT ? (HDC)wp : BeginPaint(window, &ps); RECT rect; GetClientRect(window, &rect);
        RECT label = rect; label.left += px(12); label.top += px(2); label.bottom = label.top + px(23);
        text(dc, caption(window), label, ink(), 9, FW_SEMIBOLD); if (message == WM_PAINT) EndPaint(window, &ps); return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, groupProc, 2);
    return DefSubclassProc(window, message, wp, lp);
}
inline LRESULT CALLBACK choiceProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (message == WM_PAINT || message == WM_PRINTCLIENT)
    {
        PAINTSTRUCT ps; HDC dc = message == WM_PRINTCLIENT ? (HDC)wp : BeginPaint(window, &ps);
        RECT rect; GetClientRect(window, &rect);
        COLORREF base = inCard(GetParent(window), window) ? surface() : background();
        fill(dc, rect, base);
        bool enabled = IsWindowEnabled(window) != FALSE;
        auto style = GetWindowLongPtr(window, GWL_STYLE);
        int type = style & BS_TYPEMASK;
        bool radio = type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
        int state = (int)SendMessage(window, BM_GETCHECK, 0, 0);
        int size = px(14), y = (rect.bottom - size) / 2;
        RECT box = {px(1), y, px(1) + size, y + size};
        if (style & BS_LEFTTEXT) { box.right = rect.right - px(1); box.left = box.right - size; }
        COLORREF border = enabled ? UiColors::mutedOn(base) : line();
        COLORREF selected = enabled ? accent() : UiColors::mutedOn(base);
        HBRUSH brush = CreateSolidBrush(!radio && state != BST_UNCHECKED ? selected : base);
        HPEN pen = CreatePen(PS_SOLID, 1, !radio && state != BST_UNCHECKED ? selected : border);
        auto oldBrush = SelectObject(dc, brush); auto oldPen = SelectObject(dc, pen);
        if (radio) Ellipse(dc, box.left, box.top, box.right, box.bottom);
        else RoundRect(dc, box.left, box.top, box.right, box.bottom, px(3), px(3));
        if (state != BST_UNCHECKED)
        {
            if (radio) {
                HBRUSH dot = CreateSolidBrush(selected); HPEN dotPen = CreatePen(PS_SOLID, 1, selected);
                SelectObject(dc, dot); SelectObject(dc, dotPen);
                Ellipse(dc, box.left+px(4), box.top+px(4), box.right-px(4), box.bottom-px(4));
                SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(dot); DeleteObject(dotPen);
            } else {
                HPEN check = CreatePen(PS_SOLID, px(2), UiColors::readable(selected)); SelectObject(dc, check);
                if (state == BST_INDETERMINATE) { MoveToEx(dc, box.left+px(3), y+size/2, nullptr); LineTo(dc, box.right-px(3), y+size/2); }
                else { MoveToEx(dc, box.left+px(3), y+px(7), nullptr); LineTo(dc, box.left+px(6), y+px(10)); LineTo(dc, box.right-px(2), y+px(3)); }
                SelectObject(dc, oldPen); DeleteObject(check);
            }
        }
        SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(pen); DeleteObject(brush);
        RECT label = rect;
        if (style & BS_LEFTTEXT) label.right = box.left - px(5); else label.left = box.right + px(5);
        text(dc, caption(window), label, enabled ? UiColors::readable(base) : UiColors::mutedOn(base), 9);
        if (GetFocus() == window && !(SendMessage(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) DrawFocusRect(dc, &label);
        if (message == WM_PAINT) EndPaint(window, &ps);
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, choiceProc, 3);
    auto result = DefSubclassProc(window, message, wp, lp);
    if (message == BM_SETCHECK || message == WM_LBUTTONUP || message == WM_LBUTTONDOWN || message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_KEYUP || message == WM_ENABLE) InvalidateRect(window, nullptr, FALSE);
    return result;
}
inline bool inCard(HWND parent, HWND child)
{
    RECT item; GetWindowRect(child, &item); POINT point = {item.left, item.top}; ScreenToClient(parent, &point);
    for (HWND w = GetWindow(parent, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT))
    {
        wchar_t cls[32]; GetClassNameW(w, cls, ARRAYSIZE(cls));
        if (_wcsicmp(cls, L"Button") || (GetWindowLongPtr(w, GWL_STYLE) & BS_TYPEMASK) != BS_GROUPBOX) continue;
        RECT group; GetWindowRect(w, &group); MapWindowPoints(nullptr, parent, (POINT*)&group, 2);
        if (PtInRect(&group, point)) return true;
    }
    return false;
}
inline void initDialog(HWND parent)
{
    windowTheme(parent);
    for (HWND w = GetWindow(parent, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT))
    {
        wchar_t cls[32]; GetClassNameW(w, cls, ARRAYSIZE(cls));
        if (!_wcsicmp(cls, L"Button")) {
            auto type = GetWindowLongPtr(w, GWL_STYLE) & BS_TYPEMASK;
            if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON) button(w);
            else if (type == BS_GROUPBOX) SetWindowSubclass(w, groupProc, 2, 0);
            else if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_3STATE || type == BS_AUTO3STATE || type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON)
                SetWindowSubclass(w, choiceProc, 3, 0);
        }
    }
}
inline void paintDialog(HWND parent, HDC dc)
{
    RECT rect; GetClientRect(parent, &rect); fill(dc, rect, background());
    for (HWND w = GetWindow(parent, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT))
    {
        wchar_t cls[32]; GetClassNameW(w, cls, ARRAYSIZE(cls));
        if (!_wcsicmp(cls, L"Button") && (GetWindowLongPtr(w, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX) {
            RECT card; GetWindowRect(w, &card); MapWindowPoints(nullptr, parent, (POINT*)&card, 2);
            InflateRect(&card, px(2), px(2)); round(dc, card, surface(), line(), 12);
        }
    }
}
}
