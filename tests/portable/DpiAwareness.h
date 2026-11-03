#pragma once
class DpiAwareness
{
public:
    static DpiAwareness& instance() { static DpiAwareness dpi; return dpi; }
    void logicalToPhysical(int from, int& to) const { to = from; }
};
