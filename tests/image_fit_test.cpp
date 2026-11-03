#include "ImageFit.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main()
{
    using namespace ImageFit;
    Rect bounds = {20, 30, 220, 330};
    auto equal = contain(400, 600, bounds);
    assert(equal.left == 20 && equal.top == 30 && equal.right == 220 && equal.bottom == 330);
    auto small = contain(40, 60, bounds);
    assert(small.right - small.left == 40 && small.bottom - small.top == 60);
    assert(small.left == 100 && small.top == 150);
    auto landscape = contain(800, 400, bounds);
    assert(landscape.left == 20 && landscape.top == 130 && landscape.right == 220 && landscape.bottom == 230);
    auto invalid = contain(0, 400, bounds);
    assert(invalid.left == invalid.right && invalid.top == invalid.bottom);
    auto empty = contain(100, 100, {20, 30, 20, 30});
    assert(empty.left == empty.right && empty.top == empty.bottom);
    for (int iw : {1, 5, 30, 300, 4000})
        for (int ih : {1, 8, 50, 500, 6000})
            for (int w : {1, 3, 120, 900})
                for (int h : {1, 3, 180, 1600})
                {
                    Rect box = {13, 29, 13 + w, 29 + h};
                    auto fit = contain(iw, ih, box);
                    assert(fit.left >= box.left && fit.right <= box.right);
                    assert(fit.top >= box.top && fit.bottom <= box.bottom);
                    assert(fit.right > fit.left && fit.bottom > fit.top);
                    assert(fit.right - fit.left <= iw && fit.bottom - fit.top <= ih);
                    // Pixel rounding can differ by at most one pixel per axis.
                    double scale = (std::min)(1.0, (std::min)((double)w / iw, (double)h / ih));
                    assert(std::abs((fit.right - fit.left) - iw * scale) <= 1.0);
                    assert(std::abs((fit.bottom - fit.top) - ih * scale) <= 1.0);
                }
    std::cout << "Image fit: offsets, centering, native-size limit and 400 edge combinations passed\n";
}
