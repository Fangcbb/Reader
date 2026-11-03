#pragma once
#include "types.h"
class Settings
{
public:
    struct Data { LayoutConfig layout = {}; };
    static Settings& instance() { static Settings settings; return settings; }
    Data& data() { return m_data; }
private:
    Data m_data;
};
