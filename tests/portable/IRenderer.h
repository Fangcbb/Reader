#pragma once
#include "types.h"
class IMeasurer
{
public:
    virtual ~IMeasurer() = default;
    virtual TextMetrics measureText(const wchar_t*, int, int, int) = 0;
};
