#include "UiPalette.h"
#include <cassert>
#include <iostream>
int main()
{
    UiThemeConfig theme;
    auto light = UiColors::make(theme);
    theme.darkMode = true;
    auto dark = UiColors::make(theme);
    assert(UiColors::luminance(dark.background) < UiColors::luminance(light.background));
    assert(UiColors::luminance(dark.surface) < .04);
    for (unsigned r : {0u, 64u, 128u, 192u, 255u})
        for (unsigned g : {0u, 64u, 128u, 192u, 255u})
            for (unsigned b : {0u, 64u, 128u, 192u, 255u})
                for (bool mode : {false, true}) {
                    theme.darkMode = mode;
                    theme.accent = theme.lightBackground = theme.darkBackground = UiColors::rgb(r, g, b);
                    auto palette = UiColors::make(theme);
                    assert(UiColors::contrast(palette.text, palette.surface) >= 4.5);
                    assert(UiColors::contrast(palette.muted, palette.surface) >= 4.5);
                    assert(UiColors::contrast(palette.onAccent, palette.accent) >= 4.5);
                }
    std::cout << "Independent UI palette: dark mode and 250 custom-color contrast combinations passed\n";
}
