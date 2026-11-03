#include "UiThemeDlg.h"
#include "DisplaySettingDlg.h"
#include "Settings.h"
#include "Utils.h"
#include "resource.h"

UiThemeDlg::UiThemeDlg(HINSTANCE instance, HWND parent)
    : Dialog(instance, parent, IDD_UI_THEME), m_theme(Settings::instance().data().uiTheme)
{
}

void UiThemeDlg::updateLabels()
{
    SetDlgItemTextW(m_hWnd, IDC_UI_ACCENT, (L"强调色 " + Utils::toRGB(m_theme.accent)).c_str());
    SetDlgItemTextW(m_hWnd, IDC_UI_LIGHT_BG, (L"浅色底色 " + Utils::toRGB(m_theme.lightBackground)).c_str());
    SetDlgItemTextW(m_hWnd, IDC_UI_DARK_BG, (L"深色底色 " + Utils::toRGB(m_theme.darkBackground)).c_str());
    CheckDlgButton(m_hWnd, IDC_UI_THEME_DARK, m_theme.darkMode ? BST_CHECKED : BST_UNCHECKED);
    InvalidateRect(GetDlgItem(m_hWnd, IDC_UI_THEME_PREVIEW), nullptr, FALSE);
}

void UiThemeDlg::chooseColor(int id)
{
    COLORREF color = id == IDC_UI_ACCENT ? m_theme.accent : id == IDC_UI_LIGHT_BG ? m_theme.lightBackground : m_theme.darkBackground;
    if (!DisplaySettingDlg::openColorDialog(m_hWnd, color)) return;
    if (id == IDC_UI_ACCENT) m_theme.accent = color;
    else if (id == IDC_UI_LIGHT_BG) m_theme.lightBackground = color;
    else m_theme.darkBackground = color;
    updateLabels();
}

void UiThemeDlg::drawPreview(HDC dc, RECT rect)
{
    auto colors = UiColors::make(m_theme);
    using namespace ModernUi;
    fill(dc, rect, colors.background);
    RECT header = rect; header.bottom = header.top + px(28);
    fill(dc, header, colors.surface);
    header.left += px(12); text(dc, L"Reader   软件界面预览", header, colors.text, 9, FW_SEMIBOLD);
    RECT button = {rect.right - px(70), rect.top + px(6), rect.right - px(10), rect.top + px(22)};
    round(dc, button, colors.accent, colors.accent, 6);
    RECT page = {rect.left + px(16), rect.top + px(40), rect.right - px(16), rect.bottom - px(12)};
    // Use existing reading colors only to illustrate that they are untouched.
    auto& data = Settings::instance().data();
    round(dc, page, data.background.color, colors.border);
    text(dc, L"阅读背景与文字保持原设置", page, data.styles.textColor, 9, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

LRESULT UiThemeDlg::handleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INITDIALOG) { updateLabels(); centerParent(); return TRUE; }
    if (message == WM_DRAWITEM) {
        auto item = (DRAWITEMSTRUCT*)lParam;
        if (item->CtlID == IDC_UI_THEME_PREVIEW) { drawPreview(item->hDC, item->rcItem); return TRUE; }
    }
    if (message == WM_COMMAND)
    {
        switch (LOWORD(wParam))
        {
        case IDC_UI_ACCENT: case IDC_UI_LIGHT_BG: case IDC_UI_DARK_BG:
            chooseColor(LOWORD(wParam)); return TRUE;
        case IDC_UI_THEME_DARK:
            m_theme.darkMode = IsDlgButtonChecked(m_hWnd, IDC_UI_THEME_DARK) == BST_CHECKED;
            updateLabels(); return TRUE;
        case IDC_UI_THEME_RESET:
        {
            bool dark = m_theme.darkMode;
            m_theme = UiThemeConfig(); m_theme.darkMode = dark;
            updateLabels(); return TRUE;
        }
        case IDOK:
            Settings::instance().data().uiTheme = m_theme;
            Settings::instance().save();
            // Notify the shell even when this dialog was opened from Appearance.
            SendMessage(GetAncestor(m_hWnd, GA_ROOTOWNER), WM_UI_THEME_CHANGED, 0, 0);
            EndDialog(m_hWnd, IDOK); return TRUE;
        case IDCANCEL:
            EndDialog(m_hWnd, IDCANCEL); return TRUE;
        }
    }
    return FALSE;
}
