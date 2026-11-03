#pragma once
#include <algorithm>

namespace ImageFit
{
struct Rect { int left, top, right, bottom; };
// Work in destination pixels. Keep offsets even for equal aspect ratios and
// avoid enlarging small ebook artwork beyond its native resolution.
inline Rect contain(int imageWidth, int imageHeight, const Rect& bounds, bool upscale = false)
{
    if (imageWidth <= 0 || imageHeight <= 0 || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        return {bounds.left, bounds.top, bounds.left, bounds.top};
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    double scale = (std::min)((double)width / imageWidth, (double)height / imageHeight);
    if (!upscale) scale = (std::min)(1.0, scale);
    int drawWidth = (std::max)(1, (int)(imageWidth * scale));
    int drawHeight = (std::max)(1, (int)(imageHeight * scale));
    const int left = bounds.left + (width - drawWidth) / 2;
    const int top = bounds.top + (height - drawHeight) / 2;
    return {left, top, left + drawWidth, top + drawHeight};
}
}
