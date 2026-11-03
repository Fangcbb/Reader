#include "ReadingPresetsDlg.h"
#include "DpiAwareness.h"
#include "Utils.h"
#include "resource.h"
#include <cwctype>

ReadingPresetsDlg::ReadingPresetsDlg(HINSTANCE instance, HWND parent, const ReadingPreset& current)
    : Dialog(instance, parent, IDD_READING_PRESETS), m_current(current) {}
ReadingPreset ReadingPresetsDlg::current()
{
    auto& data = Settings::instance().data();
    ReadingPreset result; result.styles = data.styles; result.background = data.background;
    result.layout = data.layout; result.alpha = data.window.alpha; result.textAlpha = data.window.textAlpha;
    return result;
}
void ReadingPresetsDlg::apply(const ReadingPreset& preset)
{
    auto& data = Settings::instance().data();
    data.styles = preset.styles; data.background = preset.background; data.layout = preset.layout;
    data.window.alpha = preset.alpha; data.window.textAlpha = preset.textAlpha;
    Settings::instance().save();
}
void ReadingPresetsDlg::refresh(int selected)
{
    SendDlgItemMessage(m_hWnd, IDC_PRESETS_LIST, LB_RESETCONTENT, 0, 0);
    const auto& presets = Settings::instance().data().readingPresets;
    for (const auto& preset : presets) SendDlgItemMessageW(m_hWnd, IDC_PRESETS_LIST, LB_ADDSTRING, 0, (LPARAM)preset.name.c_str());
    SendDlgItemMessage(m_hWnd, IDC_PRESETS_LIST, LB_SETCURSEL, selected, 0);
    EnableWindow(GetDlgItem(m_hWnd, IDOK), selected >= 0);
    EnableWindow(GetDlgItem(m_hWnd, IDC_PRESETS_DELETE), selected >= 0);
}
void ReadingPresetsDlg::saveCurrent()
{
    std::wstring name = getDlgItemTextW(IDC_PRESETS_NAME);
    while (!name.empty() && iswspace(name.front())) name.erase(name.begin());
    while (!name.empty() && iswspace(name.back())) name.pop_back();
    if (name.empty() || name.size() > 64) { MessageBoxW(m_hWnd, L"请输入 1–64 个字符的预设名称。", L"阅读预设", MB_OK | MB_ICONINFORMATION); return; }
    auto& presets = Settings::instance().data().readingPresets;
    int index = -1;
    for (int i = 0; i < (int)presets.size(); ++i) if (presets[i].name == name) index = i;
    if (index >= 0 && MessageBoxW(m_hWnd, L"同名预设已存在，是否用当前阅读设置覆盖？", L"更新预设", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    if (index < 0 && presets.size() >= 64) { MessageBoxW(m_hWnd, L"最多保存 64 个预设，请先删除不再使用的预设。", L"阅读预设", MB_OK); return; }
    ReadingPreset saved = m_current; saved.name = name;
    // Fonts are persisted in logical units, so presets remain portable across DPI.
    DpiAwareness::instance().physicalToLogical(saved.styles.textFont, saved.styles.textFont);
    DpiAwareness::instance().physicalToLogical(saved.styles.titleFont, saved.styles.titleFont);
    if (index < 0) { index = (int)presets.size(); presets.push_back(saved); } else presets[index] = saved;
    Settings::instance().save(); refresh(index);
    SetDlgItemTextW(m_hWnd, IDC_PRESETS_STATUS, L"预设已保存。可继续保存其他场景，或应用列表中的预设。");
}
void ReadingPresetsDlg::applySelected()
{
    int index = (int)SendDlgItemMessage(m_hWnd, IDC_PRESETS_LIST, LB_GETCURSEL, 0, 0);
    const auto& presets = Settings::instance().data().readingPresets;
    if (index < 0 || index >= (int)presets.size()) return;
    m_selected = presets[index];
    if (m_selected.background.enableImage && !Utils::isFileExist(m_selected.background.fileName)) {
        if (MessageBoxW(m_hWnd, L"预设的背景图片不存在。是否暂用该预设的纯色背景？", L"背景图片", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        m_selected.background.enableImage = false;
    }
    DpiAwareness::instance().logicalToPhysical(m_selected.styles.textFont, m_selected.styles.textFont);
    DpiAwareness::instance().logicalToPhysical(m_selected.styles.titleFont, m_selected.styles.titleFont);
    EndDialog(m_hWnd, IDOK);
}
LRESULT ReadingPresetsDlg::handleMessage(UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_INITDIALOG) { SendDlgItemMessage(m_hWnd, IDC_PRESETS_NAME, EM_SETLIMITTEXT, 64, 0); refresh(); centerParent(); return TRUE; }
    if (message != WM_COMMAND) return FALSE;
    switch (LOWORD(wp)) {
    case IDC_PRESETS_SAVE: saveCurrent(); return TRUE;
    case IDC_PRESETS_DELETE: {
        auto& presets = Settings::instance().data().readingPresets;
        int index = (int)SendDlgItemMessage(m_hWnd, IDC_PRESETS_LIST, LB_GETCURSEL, 0, 0);
        if (index >= 0 && index < (int)presets.size() && MessageBoxW(m_hWnd, L"删除所选阅读预设？", L"阅读预设", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            presets.erase(presets.begin() + index); Settings::instance().save(); refresh();
        }
        return TRUE;
    }
    case IDC_PRESETS_LIST:
        if (HIWORD(wp) == LBN_SELCHANGE) {
            int index = (int)SendDlgItemMessage(m_hWnd, IDC_PRESETS_LIST, LB_GETCURSEL, 0, 0);
            const auto& presets = Settings::instance().data().readingPresets;
            if (index >= 0 && index < (int)presets.size()) SetDlgItemTextW(m_hWnd, IDC_PRESETS_NAME, presets[index].name.c_str());
            EnableWindow(GetDlgItem(m_hWnd, IDOK), index >= 0); EnableWindow(GetDlgItem(m_hWnd, IDC_PRESETS_DELETE), index >= 0);
        }
        if (HIWORD(wp) == LBN_DBLCLK) applySelected();
        return TRUE;
    case IDOK: applySelected(); return TRUE;
    case IDCANCEL: EndDialog(m_hWnd, IDCANCEL); return TRUE;
    }
    return FALSE;
}
