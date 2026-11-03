#pragma once
#include <cstdint>
#include <cmath>
#include <initializer_list>

namespace UiColors
{
using Color = std::uint32_t; // COLORREF channel order; independent of Win32 headers.
constexpr Color rgb(unsigned r, unsigned g, unsigned b) { return r | (g << 8) | (b << 16); }
inline Color mix(Color a, Color b, unsigned percent)
{
    Color out = 0;
    for (unsigned shift : {0u, 8u, 16u})
        out |= (((((a >> shift) & 255) * (100 - percent)) + (((b >> shift) & 255) * percent)) / 100) << shift;
    return out;
}
inline double luminance(Color color)
{
    auto channel = [](unsigned v) { double s = v / 255.0; return s <= .04045 ? s / 12.92 : std::pow((s + .055) / 1.055, 2.4); };
    return .2126 * channel(color & 255) + .7152 * channel((color >> 8) & 255) + .0722 * channel((color >> 16) & 255);
}
inline double contrast(Color a, Color b)
{
    double x = luminance(a), y = luminance(b);
    return x > y ? (x + .05) / (y + .05) : (y + .05) / (x + .05);
}
inline Color readable(Color background)
{
    Color preferred = luminance(background) > .179 ? rgb(31, 41, 61) : rgb(239, 241, 247);
    if (contrast(preferred, background) >= 4.5) return preferred;
    return luminance(background) > .179 ? rgb(0, 0, 0) : rgb(255, 255, 255);
}
}
struct UiThemeConfig
{
    bool darkMode = false;
    UiColors::Color lightBackground = UiColors::rgb(245, 246, 250);
    UiColors::Color darkBackground = UiColors::rgb(24, 26, 34);
    UiColors::Color accent = UiColors::rgb(101, 86, 213);
};
namespace UiColors
{
inline Color mutedOn(Color background)
{
    Color text = readable(background), muted = mix(background, text, 65);
    for (unsigned weight = 70; contrast(muted, background) < 4.5 && weight <= 100; weight += 5) muted = mix(background, text, weight);
    return muted;
}
struct Palette { Color background, surface, text, muted, accent, border, tint, track, onAccent; };
inline Palette make(const UiThemeConfig& theme)
{
    Palette p = {};
    p.background = (theme.darkMode ? theme.darkBackground : theme.lightBackground) & 0xffffff;
    p.surface = theme.darkMode ? mix(p.background, rgb(255,255,255), 6)
        : theme.lightBackground == rgb(245,246,250) ? rgb(255,255,255) : mix(p.background, rgb(255,255,255), 85);
    p.text = readable(p.surface);
    p.muted = mutedOn(p.surface);
    p.accent = theme.accent & 0xffffff;
    p.onAccent = readable(p.accent);
    p.border = mix(p.surface, p.text, 15);
    p.tint = mix(p.surface, p.accent, 15);
    p.track = theme.darkMode ? mix(p.surface, p.text, 22) : rgb(223,225,235);
    return p;
}
}
