#include "DisplaySettingDlg.h"
#include "IRenderer.h"
#include "resource.h"
#include "DpiAwareness.h"
#include "Utils.h"
#include "LayoutText.h"
#include "IBook.h"

namespace { const UINT_PTR SCREEN_PICKER_TIMER = 0x5343; }

DisplaySettingDlg::DisplaySettingDlg(HINSTANCE hInstance, HWND hParent)
    : Dialog(hInstance, hParent, IDD_DISPLAYSETTING)
    , m_pickerCursor(LoadCursor(hInstance, MAKEINTRESOURCE(IDC_CURSOR_PICKER)))
    , m_isPickingColor(false)
    , m_changed(false)
    , m_ignoreAlphaEditChange(false)
{
    auto &data = Settings::instance().data();
    m_styles = data.styles;
    m_alpha = data.window.alpha;
    m_textAlpha = data.window.textAlpha;
    m_background = data.background;
    m_layout = data.layout;
}

DisplaySettingDlg::~DisplaySettingDlg()
{
    stopPickColor();
}

bool DisplaySettingDlg::isChanged() const
{
    return m_changed;
}

LRESULT DisplaySettingDlg::onInitDialog()
{
    m_pickerTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, m_hWnd, nullptr, m_hInstance, nullptr);
    if (m_pickerTooltip)
    {
        TOOLINFOW tip = {}; tip.cbSize = sizeof(tip);
        tip.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tip.hwnd = m_hWnd; tip.uId = (UINT_PTR)GetDlgItem(m_hWnd, IDC_STATIC_PICKER);
        tip.lpszText = const_cast<wchar_t*>(L"点击吸管后选择屏幕颜色，也可按住拖动。Esc 或右键取消。");
        SendMessageW(m_pickerTooltip, TTM_ADDTOOLW, 0, (LPARAM)&tip);
        SendMessage(m_pickerTooltip, TTM_SETMAXTIPWIDTH, 0, 300);
    }
    // font settings
    auto &data = Settings::instance().data();
    CheckDlgButton(m_hWnd, IDC_CHECK_SAMETEXTSTYLE, m_styles.unified ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_hWnd, IDC_CHECK_MENU_FONT, m_styles.viewFont ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(m_hWnd, IDC_BUTTON_CHAPTER_FONT), !m_styles.unified);
    bindSliderEditPair(IDC_SLIDER_TEXTALPHA, IDC_EDIT_TEXTALPHA, m_textAlpha, MIN_TEXT_ALPHA, MAX_TEXT_ALPHA);

    // background settings
    CheckDlgButton(m_hWnd, IDC_CHECK_BIENABLE, m_background.enableImage ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(m_hWnd, IDC_STATIC_RGB, Utils::toRGB(m_background.color).c_str());
    SendMessage(GetDlgItem(m_hWnd, IDC_COMBO_BIMODE), CB_ADDSTRING, 0, (LPARAM)loadString(IDS_STRETCH).c_str());
    SendMessage(GetDlgItem(m_hWnd, IDC_COMBO_BIMODE), CB_ADDSTRING, 0, (LPARAM)loadString(IDS_TILE).c_str());
    SendMessage(GetDlgItem(m_hWnd, IDC_COMBO_BIMODE), CB_ADDSTRING, 0, (LPARAM)loadString(IDS_TILEFLIP).c_str());
    SendMessage(GetDlgItem(m_hWnd, IDC_COMBO_BIMODE), CB_SETCURSEL, (int)m_background.mode, NULL);
    SetDlgItemTextW(m_hWnd, IDC_EDIT_BIFILE, m_background.fileName.c_str());
    EnableWindow(GetDlgItem(m_hWnd, IDC_COMBO_BIMODE), m_background.enableImage);
    EnableWindow(GetDlgItem(m_hWnd, IDC_EDIT_BIFILE), m_background.enableImage);
    EnableWindow(GetDlgItem(m_hWnd, IDC_BUTTON_BISEL), m_background.enableImage);
    bindSliderEditPair(IDC_SLIDER_BGALPHA, IDC_EDIT_BGALPHA, m_alpha, MIN_ALPHA, MAX_ALPHA);

    // layout settings
    SetDlgItemInt(m_hWnd, IDC_EDIT_CHARGAP, m_layout.charGap, FALSE);
    SetDlgItemInt(m_hWnd, IDC_EDIT_LINEGAP, m_layout.lineGap, FALSE);
    SetDlgItemInt(m_hWnd, IDC_EDIT_PARAGRAPHGAP, m_layout.paragraphGap, FALSE);
    SetDlgItemTextW(m_hWnd, IDC_EDIT_BORDER, getBorder({m_layout.borderL, m_layout.borderT, m_layout.borderR, m_layout.borderB}).c_str());
    CheckDlgButton(m_hWnd, IDC_CHECK_WORDWRAP, m_layout.wordWrap ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_hWnd, IDC_CHECK_INDENT, m_layout.indent ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_hWnd, IDC_CHECK_BLANKLINES, m_layout.noBlansLine ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_hWnd, IDC_CHECK_SPLITCHAPTER, m_layout.splitChapter ? BST_CHECKED : BST_UNCHECKED);
    SendMessage(GetDlgItem(m_hWnd, IDC_COMBO_ALIGNMENT), CB_ADDSTRING, 0, (LPARAM)loadString(IDS_LEFT).c_str());
    SendMessage(GetDlgItem(m_hWnd, IDC_COMBO_ALIGNMENT), CB_ADDSTRING, 0, (LPARAM)loadString(IDS_CENTER).c_str());
    SendMessage(GetDlgItem(m_hWnd, IDC_COMBO_ALIGNMENT), CB_SETCURSEL, (int)m_layout.alignment, NULL);

    CheckDlgButton(m_hWnd, IDC_CHECK_EPUB_IMAGES, m_layout.epubImages ? BST_CHECKED : BST_UNCHECKED);
    centerParent();
    return TRUE;
}

LRESULT DisplaySettingDlg::onSave()
{
    stopPickColor();
    if (!loadData())
    {
        return FALSE;
    }

    auto &data = Settings::instance().data();
    if (m_alpha != data.window.alpha)
    {
        m_changed = true;
        data.window.alpha = m_alpha;
    }
    if (m_textAlpha != data.window.textAlpha)
    {
        m_changed = true;
        data.window.textAlpha = m_textAlpha;
    }
    if (m_background.enableImage != data.background.enableImage
        || (m_background.enableImage && (m_background.fileName != data.background.fileName || m_background.mode != data.background.mode))
        || (!m_background.enableImage && m_background.color != data.background.color))
    {
        m_changed = true;
    }
    data.background = m_background;
    if (m_layout != data.layout)
    {
        m_changed = true;
        data.layout = m_layout;
    }
    if (m_styles != data.styles)
    {
        m_changed = true;
        data.styles = m_styles;
    }

    Settings::instance().save();
    EndDialog(m_hWnd, IDOK);
    return TRUE;
}

LRESULT DisplaySettingDlg::onUnifiedTextStyle()
{
    m_styles.unified = IsDlgButtonChecked(m_hWnd, IDC_CHECK_SAMETEXTSTYLE) == BST_CHECKED;
    EnableWindow(GetDlgItem(m_hWnd, IDC_BUTTON_CHAPTER_FONT), !m_styles.unified);
    updatePreview();
    return TRUE;
}

LRESULT DisplaySettingDlg::onTextFont()
{
    if (openFontDialog(m_hWnd, m_styles.textFont, m_styles.textColor))
    {
        updatePreview();
    }
    return TRUE;
}

LRESULT DisplaySettingDlg::onTitleFont()
{
    if (openFontDialog(m_hWnd, m_styles.titleFont, m_styles.titleColor))
    {
        updatePreview();
    }
    return TRUE;
}

LRESULT DisplaySettingDlg::onBackgroundColor()
{
    if (openColorDialog(m_hWnd, m_background.color))
    {
        updatePreview();
    }
    return TRUE;
}

LRESULT DisplaySettingDlg::onBackgroundImageEnable()
{
    m_background.enableImage = IsDlgButtonChecked(m_hWnd, IDC_CHECK_BIENABLE) == BST_CHECKED;
    EnableWindow(GetDlgItem(m_hWnd, IDC_COMBO_BIMODE), m_background.enableImage);
    EnableWindow(GetDlgItem(m_hWnd, IDC_EDIT_BIFILE), m_background.enableImage);
    EnableWindow(GetDlgItem(m_hWnd, IDC_BUTTON_BISEL), m_background.enableImage);
    updatePreview();
    return TRUE;
}

LRESULT DisplaySettingDlg::onBrowserBackgroundImage()
{
    if (openFileDialog(m_background.fileName))
    {
        SetDlgItemTextW(m_hWnd, IDC_EDIT_BIFILE, m_background.fileName.c_str());
    }
    return TRUE;
}

LRESULT DisplaySettingDlg::onBackgroundImageFileChanged()
{
    m_background.fileName = getDlgItemTextW(IDC_EDIT_BIFILE);
    updatePreview();
    return TRUE;
}

LRESULT DisplaySettingDlg::onBackgroundImageModeChanged()
{
    m_background.mode = (WrapMode)SendMessage(GetDlgItem(m_hWnd, IDC_COMBO_BIMODE), CB_GETCURSEL, 0, NULL);
    updatePreview();
    return TRUE;
}

LRESULT DisplaySettingDlg::onBackgroundAlphaChanged()
{
    if (m_ignoreAlphaEditChange)
    {
        return TRUE;
    }
    int value = GetDlgItemInt(m_hWnd, IDC_EDIT_BGALPHA, NULL, FALSE);
    if (value < 1) value = 1;
    if (value > 255) value = 255;
    m_alpha = (BYTE)value;
    handleEditChange(IDC_EDIT_BGALPHA, IDC_SLIDER_BGALPHA, m_alpha, 1, 255);
    return TRUE;
}

LRESULT DisplaySettingDlg::onTextAlphaChanged()
{
    if (m_ignoreAlphaEditChange)
    {
        return TRUE;
    }
    int value = GetDlgItemInt(m_hWnd, IDC_EDIT_TEXTALPHA, NULL, FALSE);
    if (value < 1) value = 1;
    if (value > 255) value = 255;
    m_textAlpha = (BYTE)value;
    handleEditChange(IDC_EDIT_TEXTALPHA, IDC_SLIDER_TEXTALPHA, m_textAlpha, 1, 255);
    return TRUE;
}

LRESULT DisplaySettingDlg::onUpdatePreview(HDC hdc, RECT rect)
{
    // fill background
    HDC hMemdc = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, rect.right - rect.left, rect.bottom - rect.top);
    HGDIOBJ oldBitmap = SelectObject(hMemdc, hBitmap);
    FillRect(hMemdc, &rect, GetSysColorBrush(COLOR_BTNFACE));

    class PreviewSource : public ISource
    {
    public:
        const wchar_t* text() const override { return L"第一章 · 阅读时光\n翻开一本书，让文字自然流动。调整字体、留白与行距，找到适合自己的阅读节奏。\nA quiet page, a little space, and time to read.\n新的章节，从这里开始。\n"; }
        int length() const override { return (int)wcslen(text()); }
        bool isTitleLine(int index) const override { return index < (int)(wcschr(text(), L'\n') - text()); }
        bool isChapterStart(int index) const override { return index == 0; }
        bool isChapterContinuous(bool, int) const override { return true; }
        bool isCover(int) const override { return false; }
        bool hasCover() const override { return false; }
    } source;
    TextStyles textStyles(2);
    textStyles[0] = TextStyle(m_styles.textFont, m_textAlpha << 24 | m_styles.textColor);
    textStyles[1] = TextStyle(m_styles.titleFont, m_textAlpha << 24 | m_styles.titleColor);
    auto* renderer = IRenderer::instance();
    renderer->beginPaint(m_hWnd, hMemdc, rect, textStyles, true);
    if (m_background.enableImage)
    {
        Image* image = renderer->loadImageFromPath(m_background.fileName.c_str());
        Image* background = renderer->createImageFromBGImage(image, m_alpha, rect.right, rect.bottom, m_background.mode);
        delete image;
        renderer->drawImage(background, rect);
        delete background;
    }
    else renderer->fillRect(m_alpha << 24 | m_background.color, rect);
    LayoutText layout;
    layout.setSource(&source);
    layout.setMeasurer(renderer->measurer());
    layout.setTextStyle({0, m_styles.unified ? 0 : 1});
    layout.setConfigOverride(&m_layout);
    auto page = layout.layout(0, rect.right - rect.left, rect.bottom - rect.top);
    int y = page.y;
    for (const auto& line : page.lines)
    {
        int x = page.x + line.x;
        for (const auto& ch : line.chars)
        {
            RECT glyph = {x, y + line.h - ch.h, x + ch.w, y + line.h};
            renderer->drawText(source.text() + ch.index, ch.length, glyph, ch.tsIdx);
            x += ch.w + page.cGap;
        }
        y += line.h + line.gap;
    }
    renderer->endPaint();

    BitBlt(hdc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, hMemdc, 0, 0, SRCCOPY);
    SelectObject(hMemdc, oldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemdc);
    return (INT_PTR)TRUE;
}

void DisplaySettingDlg::applyPreset(int preset)
{
    const COLORREF backgrounds[] = {RGB(247, 243, 233), RGB(229, 239, 225), RGB(29, 33, 39)};
    const COLORREF foregrounds[] = {RGB(56, 51, 43), RGB(42, 62, 45), RGB(216, 216, 208)};
    m_background.color = backgrounds[preset];
    m_background.enableImage = false;
    m_styles.textColor = foregrounds[preset];
    m_styles.titleColor = foregrounds[preset];
    m_layout.borderL = m_layout.borderR = 20;
    m_layout.borderT = m_layout.borderB = 18;
    m_layout.charGap = 0;
    m_layout.lineGap = 8;
    m_layout.paragraphGap = 18;
    m_layout.wordWrap = true;
    m_layout.splitChapter = true;
    m_alpha = m_textAlpha = 255;
    CheckDlgButton(m_hWnd, IDC_CHECK_BIENABLE, BST_UNCHECKED);
    onBackgroundImageEnable();
    CheckDlgButton(m_hWnd, IDC_CHECK_WORDWRAP, BST_CHECKED);
    CheckDlgButton(m_hWnd, IDC_CHECK_SPLITCHAPTER, BST_CHECKED);
    SetDlgItemInt(m_hWnd, IDC_EDIT_CHARGAP, m_layout.charGap, FALSE);
    SetDlgItemInt(m_hWnd, IDC_EDIT_LINEGAP, m_layout.lineGap, FALSE);
    SetDlgItemInt(m_hWnd, IDC_EDIT_PARAGRAPHGAP, m_layout.paragraphGap, FALSE);
    SetDlgItemTextW(m_hWnd, IDC_EDIT_BORDER, getBorder({20, 18, 20, 18}).c_str());
    SetDlgItemTextW(m_hWnd, IDC_STATIC_RGB, Utils::toRGB(m_background.color).c_str());
    bindSliderEditPair(IDC_SLIDER_BGALPHA, IDC_EDIT_BGALPHA, m_alpha, MIN_ALPHA, MAX_ALPHA);
    bindSliderEditPair(IDC_SLIDER_TEXTALPHA, IDC_EDIT_TEXTALPHA, m_textAlpha, MIN_TEXT_ALPHA, MAX_TEXT_ALPHA);
    updatePreview();
}

void DisplaySettingDlg::updatePreview()
{
    InvalidateRect(GetDlgItem(m_hWnd, IDC_STATIC_PREVIEW), NULL, FALSE);
}

std::wstring DisplaySettingDlg::getBorder(const RECT &rect)
{
    wchar_t border[32] = {0};
    _snwprintf(border, 32, L"%d,%d,%d,%d", rect.left, rect.top, rect.right, rect.bottom);
    return border;
}

bool DisplaySettingDlg::getBorder(const wchar_t *str, RECT &rect, bool tips)
{
    wchar_t trailing = 0;
    if (swscanf(str, L"%ld,%ld,%ld,%ld %lc", &rect.left, &rect.top, &rect.right, &rect.bottom, &trailing) == 4
        && rect.left >= 0 && rect.top >= 0 && rect.right >= 0 && rect.bottom >= 0
        && rect.left <= 240 && rect.top <= 240 && rect.right <= 240 && rect.bottom <= 240)
    {
        return true;
    }
    if (tips)
    {
        messageBox(IDS_INVALID_INBORDER, IDS_WARN, MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(m_hWnd, IDC_EDIT_BORDER));
    }
    return false;
}

void DisplaySettingDlg::bindSliderEditPair(int sliderId, int editId, BYTE& value, int minValue, int maxValue)
{
    SendMessage(GetDlgItem(m_hWnd, sliderId), TBM_SETRANGE, TRUE, MAKELPARAM(minValue, maxValue));
    setEditValue(editId, value);
    SendMessage(GetDlgItem(m_hWnd, sliderId), TBM_SETPOS, TRUE, value);
}

void DisplaySettingDlg::setSliderValue(int sliderId, int value)
{
    SendMessage(GetDlgItem(m_hWnd, sliderId), TBM_SETPOS, TRUE, value);
}

void DisplaySettingDlg::setEditValue(int editId, int value)
{
    bool oldIgnore = m_ignoreAlphaEditChange;
    m_ignoreAlphaEditChange = true;

    HWND hEdit = GetDlgItem(m_hWnd, editId);
    wchar_t text[16] = {0};
    _snwprintf(text, ARRAYSIZE(text), L"%d", value);

    wchar_t oldText[16] = {0};
    GetWindowTextW(hEdit, oldText, ARRAYSIZE(oldText));
    if (wcscmp(oldText, text) != 0)
    {
        int selStart = 0;
        int selEnd = 0;
        SendMessage(hEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        SetWindowTextW(hEdit, text);
        int newLen = lstrlenW(text);
        if (selStart > newLen) selStart = newLen;
        if (selEnd > newLen) selEnd = newLen;
        SendMessage(hEdit, EM_SETSEL, selStart, selEnd);
    }

    m_ignoreAlphaEditChange = oldIgnore;
}

bool DisplaySettingDlg::handleSliderChange(int sliderId, int editId, BYTE& value)
{
    value = (BYTE)SendMessage(GetDlgItem(m_hWnd, sliderId), TBM_GETPOS, 0, 0);
    setEditValue(editId, value);
    updatePreview();
    return true;
}

bool DisplaySettingDlg::handleEditChange(int editId, int sliderId, BYTE& value, int minValue, int maxValue)
{
    if (m_ignoreAlphaEditChange)
    {
        return true;
    }

    int rawValue = GetDlgItemInt(m_hWnd, editId, NULL, FALSE);
    int newValue = rawValue;
    if (newValue < minValue) newValue = minValue;
    if (newValue > maxValue) newValue = maxValue;
    BYTE newByte = (BYTE)newValue;
    value = newByte;
    setSliderValue(sliderId, value);
    if (newValue != rawValue)
    {
        setEditValue(editId, value);
    }
    updatePreview();
    return true;
}

void DisplaySettingDlg::startPickColor()
{
    if (m_isPickingColor || !GetCursorPos(&m_pickerOrigin)) return;
    m_colorBeforePick = m_background.color;
    m_pickerClickMode = m_pickerHasMoved = false;
    m_isPickingColor = true;
    SetFocus(m_hWnd);
    SetCapture(m_hWnd);
    if (GetCapture() != m_hWnd || !SetTimer(m_hWnd, SCREEN_PICKER_TIMER, 33, nullptr))
    {
        stopPickColor(true);
        return;
    }
    if (m_pickerTooltip) SendMessage(m_pickerTooltip, TTM_POP, 0, 0);
    SetCursor(m_pickerCursor ? m_pickerCursor : LoadCursor(nullptr, IDC_CROSS));
}

void DisplaySettingDlg::stopPickColor(bool cancel)
{
    if (!m_isPickingColor) return;
    m_isPickingColor = false; // ReleaseCapture synchronously sends WM_CAPTURECHANGED.
    KillTimer(m_hWnd, SCREEN_PICKER_TIMER);
    if (GetCapture() == m_hWnd) ReleaseCapture();
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    if (cancel) updatePickColor(m_colorBeforePick);
}

void DisplaySettingDlg::sampleScreenColor()
{
    if (!m_isPickingColor) return;
    POINT point;
    // The screen DC uses physical pixels. Resolve dynamically to retain the
    // existing XP-compatible build path, where GetCursorPos is the fallback.
    using PhysicalCursor = BOOL(WINAPI*)(LPPOINT);
    static auto physicalCursor = (PhysicalCursor)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetPhysicalCursorPos");
    if (physicalCursor ? !physicalCursor(&point) : !GetCursorPos(&point)) return;
    POINT logicalCursor;
    if (GetCursorPos(&logicalCursor) &&
        (abs(logicalCursor.x - m_pickerOrigin.x) > 4 || abs(logicalCursor.y - m_pickerOrigin.y) > 4))
        m_pickerHasMoved = true;
    HDC screen = GetDC(nullptr);
    if (!screen) return;
    COLORREF color = GetPixel(screen, point.x, point.y);
    ReleaseDC(nullptr, screen);
    if (color != CLR_INVALID) updatePickColor(color);
}

void DisplaySettingDlg::updatePickColor(COLORREF color)
{
    if (m_background.color == color) return;
    m_background.color = color;
    SetDlgItemTextW(m_hWnd, IDC_STATIC_RGB, Utils::toRGB(m_background.color).c_str());
    updatePreview();
}

bool DisplaySettingDlg::openFileDialog(std::wstring& fileName)
{
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = L"Image Files\0*.bmp;*.jpg;*.jpeg;*.png;*.gif\0All Files\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn))
    {
        fileName = szFile;
        return true;
    }
    return false;
}

bool DisplaySettingDlg::loadData()
{
    std::wstring borderText = getDlgItemTextW(IDC_EDIT_BORDER);
    RECT borderRect;
    if (!getBorder(borderText.c_str(), borderRect, false))
    {
        messageBox(IDS_INVALID_INBORDER, IDS_WARN, MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(m_hWnd, IDC_EDIT_BORDER));
        return false;
    }
    std::wstring imageFile = getDlgItemTextW(IDC_EDIT_BIFILE);

    m_layout.borderL = borderRect.left;
    m_layout.borderT = borderRect.top;
    m_layout.borderR = borderRect.right;
    m_layout.borderB = borderRect.bottom;
    BOOL result = FALSE;
    m_layout.charGap = GetDlgItemInt(m_hWnd, IDC_EDIT_CHARGAP, &result, TRUE);
    if (!result || m_layout.charGap < 0 || m_layout.charGap > 64)
    {
        messageBox(IDS_INVALID_CHARGAP, IDS_WARN, MB_OK|MB_ICONWARNING);
        SetFocus(GetDlgItem(m_hWnd, IDC_EDIT_CHARGAP));
        return false;
    }
    m_layout.lineGap = GetDlgItemInt(m_hWnd, IDC_EDIT_LINEGAP, &result, TRUE);
    if (!result || m_layout.lineGap < 0 || m_layout.lineGap > 128)
    {
        messageBox(IDS_INVALID_LINEGAP, IDS_WARN, MB_OK|MB_ICONWARNING);
        SetFocus(GetDlgItem(m_hWnd, IDC_EDIT_LINEGAP));
        return false;
    }
    m_layout.paragraphGap = GetDlgItemInt(m_hWnd, IDC_EDIT_PARAGRAPHGAP, &result, TRUE);
    if (!result || m_layout.paragraphGap < 0 || m_layout.paragraphGap > 256)
    {
        messageBox(IDS_INVALID_PARAGRAPHGAP, IDS_WARN, MB_OK|MB_ICONWARNING);
        SetFocus(GetDlgItem(m_hWnd, IDC_EDIT_PARAGRAPHGAP));
        return false;
    }
    m_layout.epubImages = IsDlgButtonChecked(m_hWnd, IDC_CHECK_EPUB_IMAGES) == BST_CHECKED;
    m_layout.indent = IsDlgButtonChecked(m_hWnd, IDC_CHECK_INDENT) == BST_CHECKED;
    m_layout.noBlansLine = IsDlgButtonChecked(m_hWnd, IDC_CHECK_BLANKLINES) == BST_CHECKED;
    m_layout.wordWrap = IsDlgButtonChecked(m_hWnd, IDC_CHECK_WORDWRAP) == BST_CHECKED;
    m_layout.splitChapter = IsDlgButtonChecked(m_hWnd, IDC_CHECK_SPLITCHAPTER) == BST_CHECKED;
    m_layout.alignment = (Alignment)SendMessage(GetDlgItem(m_hWnd, IDC_COMBO_ALIGNMENT), CB_GETCURSEL, 0, NULL);

    m_styles.unified = IsDlgButtonChecked(m_hWnd, IDC_CHECK_SAMETEXTSTYLE) == BST_CHECKED;
    m_styles.viewFont = IsDlgButtonChecked(m_hWnd, IDC_CHECK_MENU_FONT) == BST_CHECKED;
    m_background.enableImage = IsDlgButtonChecked(m_hWnd, IDC_CHECK_BIENABLE) == BST_CHECKED;
    m_background.fileName = imageFile;
    if (m_background.enableImage)
    {
        if (m_background.fileName.empty())
        {
            messageBox(IDS_PLS_SEL_IMG, IDS_WARN, MB_OK|MB_ICONWARNING);
            return false;
        }
        std::wstring ext = Utils::fileExt(m_background.fileName);
        if (ext != L".jpg" && ext != L".png" && ext != L".bmp" && ext != L".jpeg")
        {
            messageBox(IDS_INVALID_IMG, IDS_WARN, MB_OK | MB_ICONWARNING);
            return false;
        }
        if (!Utils::isFileExist(m_background.fileName))
        {
            messageBox(IDS_INVALID_IMG, IDS_WARN, MB_OK|MB_ICONWARNING);
            return false;
        }
    }
    m_background.mode = (WrapMode)SendMessage(GetDlgItem(m_hWnd, IDC_COMBO_BIMODE), CB_GETCURSEL, 0, NULL);
    // m_background.color is updated in real time in UpdatePickColor and OpenColorDialog, no need to get it here
    m_alpha = (BYTE)GetDlgItemInt(m_hWnd, IDC_EDIT_BGALPHA, NULL, FALSE);
    m_textAlpha = (BYTE)GetDlgItemInt(m_hWnd, IDC_EDIT_TEXTALPHA, NULL, FALSE);
    // m_textStyle is updated in real time in OpenFontDialog, no need to get it here
    return true;
}

LRESULT DisplaySettingDlg::handleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        return onInitDialog();

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            return onSave();
        case IDCANCEL:
            if (m_isPickingColor) { stopPickColor(true); return TRUE; }
            EndDialog(m_hWnd, LOWORD(wParam));
            return (INT_PTR)TRUE;
        case IDC_CHECK_SAMETEXTSTYLE:
            return onUnifiedTextStyle();
        case IDC_PRESET_PAPER: applyPreset(0); return TRUE;
        case IDC_PRESET_GREEN: applyPreset(1); return TRUE;
        case IDC_PRESET_NIGHT: applyPreset(2); return TRUE;
        case IDC_CHECK_WORDWRAP:
        case IDC_CHECK_INDENT:
        case IDC_CHECK_BLANKLINES:
        case IDC_CHECK_SPLITCHAPTER:
        case IDC_CHECK_EPUB_IMAGES:
            m_layout.wordWrap = IsDlgButtonChecked(m_hWnd, IDC_CHECK_WORDWRAP) == BST_CHECKED;
            m_layout.indent = IsDlgButtonChecked(m_hWnd, IDC_CHECK_INDENT) == BST_CHECKED;
            m_layout.noBlansLine = IsDlgButtonChecked(m_hWnd, IDC_CHECK_BLANKLINES) == BST_CHECKED;
            m_layout.splitChapter = IsDlgButtonChecked(m_hWnd, IDC_CHECK_SPLITCHAPTER) == BST_CHECKED;
            m_layout.epubImages = IsDlgButtonChecked(m_hWnd, IDC_CHECK_EPUB_IMAGES) == BST_CHECKED;
            updatePreview(); return TRUE;
        case IDC_COMBO_ALIGNMENT:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                m_layout.alignment = (Alignment)SendDlgItemMessage(m_hWnd, IDC_COMBO_ALIGNMENT, CB_GETCURSEL, 0, 0);
                updatePreview();
            }
            break;
        case IDC_BUTTON_TEXT_FONT:
            return onTextFont();
        case IDC_BUTTON_CHAPTER_FONT:
            return onTitleFont();
        case IDC_BUTTON_BGCOLOR:
            return onBackgroundColor();
        case IDC_CHECK_BIENABLE:
            return onBackgroundImageEnable();
        case IDC_BUTTON_BISEL:
            return onBrowserBackgroundImage();
        case IDC_EDIT_BIFILE:
            if (HIWORD(wParam) == EN_CHANGE)
            {
                return onBackgroundImageFileChanged();
            }
            break;
        case IDC_COMBO_BIMODE:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                return onBackgroundImageModeChanged();
            }
            break;
        case IDC_EDIT_BGALPHA:
            if (HIWORD(wParam) == EN_CHANGE)
            {
                return onBackgroundAlphaChanged();
            }
            break;
        case IDC_EDIT_TEXTALPHA:
            if (HIWORD(wParam) == EN_CHANGE)
            {
                return onTextAlphaChanged();
            }
            break;
        case IDC_EDIT_LINEGAP:
            if (HIWORD(wParam) == EN_CHANGE)
            {
                m_layout.lineGap = min(128, (int)GetDlgItemInt(m_hWnd, IDC_EDIT_LINEGAP, NULL, FALSE));
                updatePreview();
            }
            break;
        case IDC_EDIT_CHARGAP:
            if (HIWORD(wParam) == EN_CHANGE)
            {
                m_layout.charGap = min(64, (int)GetDlgItemInt(m_hWnd, IDC_EDIT_CHARGAP, NULL, FALSE));
                updatePreview();
            }
            break;
        case IDC_EDIT_PARAGRAPHGAP:
            if (HIWORD(wParam) == EN_CHANGE)
            {
                m_layout.paragraphGap = min(256, (int)GetDlgItemInt(m_hWnd, IDC_EDIT_PARAGRAPHGAP, NULL, FALSE));
                updatePreview();
            }
            break;
        case IDC_EDIT_BORDER:
            if (HIWORD(wParam) == EN_CHANGE)
            {
                std::wstring borderText = getDlgItemTextW(IDC_EDIT_BORDER);
                RECT borderRect;
                if (getBorder(borderText.c_str(), borderRect))
                {
                    m_layout.borderL = borderRect.left;
                    m_layout.borderT = borderRect.top;
                    m_layout.borderR = borderRect.right;
                    m_layout.borderB = borderRect.bottom;
                    updatePreview();
                }
            }
            break;
        default:
            break;
        }
        break;
    case WM_HSCROLL:
        if ((HWND)lParam == GetDlgItem(m_hWnd, IDC_SLIDER_BGALPHA))
        {
            handleSliderChange(IDC_SLIDER_BGALPHA, IDC_EDIT_BGALPHA, m_alpha);
            return (INT_PTR)TRUE;
        }
        else if ((HWND)lParam == GetDlgItem(m_hWnd, IDC_SLIDER_TEXTALPHA))
        {
            handleSliderChange(IDC_SLIDER_TEXTALPHA, IDC_EDIT_TEXTALPHA, m_textAlpha);
            return (INT_PTR)TRUE;
        }
        break;

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lParam;
        if (IDC_STATIC_PREVIEW == di->CtlID)
        {
            return onUpdatePreview(di->hDC, di->rcItem);
        }
        break;
    }
    case WM_LBUTTONDOWN:
    {
        if (m_isPickingColor) return TRUE;
        POINT point; RECT picker;
        if (GetCursorPos(&point) && GetWindowRect(GetDlgItem(m_hWnd, IDC_STATIC_PICKER), &picker) && PtInRect(&picker, point))
        {
            startPickColor();
            return TRUE;
        }
        break;
    }
    case WM_LBUTTONUP:
        if (m_isPickingColor)
        {
            sampleScreenColor();
            if (m_pickerClickMode || m_pickerHasMoved) stopPickColor();
            else m_pickerClickMode = true; // A click arms selection; a drag confirms on release.
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
        if (m_isPickingColor) { sampleScreenColor(); return TRUE; }
        break;
    case WM_TIMER:
        if (wParam == SCREEN_PICKER_TIMER)
        {
            if (GetCapture() != m_hWnd) stopPickColor(true);
            else sampleScreenColor();
            return TRUE;
        }
        break;
    case WM_SETCURSOR:
        if (m_isPickingColor)
        {
            SetCursor(m_pickerCursor ? m_pickerCursor : LoadCursor(nullptr, IDC_CROSS));
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
        if (m_isPickingColor && wParam == VK_ESCAPE) { stopPickColor(true); return TRUE; }
        break;
    case WM_RBUTTONDOWN:
        if (m_isPickingColor) { stopPickColor(true); return TRUE; }
        break;
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        stopPickColor(true);
        break;
    case WM_CLOSE:
        stopPickColor(true);
        EndDialog(m_hWnd, IDCANCEL);
        return TRUE;
    case WM_DESTROY:
        stopPickColor();
        break;

    default:
        break;
    }
    return (INT_PTR)FALSE;
}

UINT_PTR CALLBACK DisplaySettingDlg::chooseFontProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        CHOOSEFONT* cf = (CHOOSEFONT*)lParam;
        COLORREF textColor = (COLORREF)cf->lCustData;
        HWND hComb = GetDlgItem(hWnd, 0x473);
        SendMessage(hComb, CB_SETITEMDATA, 0, textColor);
        SendMessage(hComb, CB_SETCURSEL, 0, textColor);
        SetWindowSubclass(hComb, chooseFontComboProc, 0, 0);
        break;
    }
    }
    return (INT_PTR)FALSE;
}

LRESULT CALLBACK DisplaySettingDlg::chooseFontComboProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubClass, DWORD_PTR udata)
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
    {
        DisplaySettingDlg *pThis = getThis<DisplaySettingDlg>(GetParent(GetParent(hWnd)));
        COLORREF color = (COLORREF)SendMessage(hWnd, CB_GETITEMDATA, 0, 0);
        if (pThis->openColorDialog(hWnd, color))
        {
            SendMessage(hWnd, CB_SETITEMDATA, 0, color);
            SendMessage(hWnd, CB_SETCURSEL, 0, color);
            InvalidateRect(GetParent(hWnd), NULL, TRUE);
        }
        return 0;
    }
    case CB_GETLBTEXTLEN:
        return 7;
    case CB_GETLBTEXT:
    {
        COLORREF color = (COLORREF)SendMessage(hWnd, CB_GETITEMDATA, 0, 0);
        std::wstring rgb = Utils::toRGB(color);
        _swprintf((TCHAR *)lParam, rgb.c_str());
        return 7;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, chooseFontComboProc, uIdSubClass);
        break;
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

bool DisplaySettingDlg::openFontDialog(HWND hWnd, LOGFONTW& font, COLORREF& textColor)
{
    CHOOSEFONT cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hWnd;
    cf.lpLogFont = &font;
    cf.rgbColors = textColor;
    cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_EFFECTS | CF_NOVERTFONTS | CF_ENABLEHOOK;
    cf.lpfnHook = DisplaySettingDlg::chooseFontProc;
    cf.lCustData = (LPARAM)textColor;
    if (ChooseFontW(&cf))
    {
        textColor = cf.rgbColors;
        font.lfQuality = PROOF_QUALITY;
        return true;
    }
    return false;
}

bool DisplaySettingDlg::openColorDialog(HWND hWnd, COLORREF& color)
{
    CHOOSECOLOR cc;
    ZeroMemory(&cc, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hWnd;
    cc.lpCustColors = &(Settings::instance().data().customColors[0]);
    cc.rgbResult = color;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc))
    {
        color = cc.rgbResult;
        return true;
    }
    return false;
}