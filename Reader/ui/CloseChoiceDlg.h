#pragma once
#include "Dialog.h"
#include "resource.h"

class CloseChoiceDlg : public Dialog
{
public:
    CloseChoiceDlg(HINSTANCE instance, HWND parent, bool tray)
        : Dialog(instance, parent, IDD_CLOSE_CHOICE), toTray(tray) {}
    bool toTray;
    bool remember = false;
protected:
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        if (message == WM_INITDIALOG) {
            CheckRadioButton(m_hWnd, IDC_CLOSE_EXIT, IDC_CLOSE_TRAY,
                toTray ? IDC_CLOSE_TRAY : IDC_CLOSE_EXIT);
            centerParent(); return TRUE;
        }
        if (message == WM_CLOSE) { EndDialog(m_hWnd, IDCANCEL); return TRUE; }
        if (message == WM_COMMAND) {
            switch (LOWORD(wParam)) {
            case IDOK:
                toTray = IsDlgButtonChecked(m_hWnd, IDC_CLOSE_TRAY) == BST_CHECKED;
                remember = IsDlgButtonChecked(m_hWnd, IDC_CLOSE_REMEMBER) == BST_CHECKED;
                EndDialog(m_hWnd, IDOK); return TRUE;
            case IDCANCEL:
                EndDialog(m_hWnd, IDCANCEL); return TRUE;
            }
        }
        return FALSE;
    }
};
