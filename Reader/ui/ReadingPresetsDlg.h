#pragma once
#include "Dialog.h"
#include "Settings.h"
class ReadingPresetsDlg : public Dialog
{
public:
    ReadingPresetsDlg(HINSTANCE instance, HWND parent, const ReadingPreset& current);
    const ReadingPreset& selected() const { return m_selected; }
    static ReadingPreset current();
    static void apply(const ReadingPreset& preset);
protected:
    LRESULT handleMessage(UINT message, WPARAM wp, LPARAM lp) override;
private:
    void refresh(int selected = -1);
    void saveCurrent();
    void applySelected();
    ReadingPreset m_current, m_selected;
};
