#pragma once
#include "Dialog.h"
#include "UiPalette.h"

class UiThemeDlg : public Dialog
{
public:
    UiThemeDlg(HINSTANCE instance, HWND parent);
protected:
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
private:
    void updateLabels();
    void chooseColor(int id);
    void drawPreview(HDC dc, RECT rect);
    UiThemeConfig m_theme;
};
