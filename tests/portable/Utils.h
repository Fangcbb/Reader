#pragma once
#include <string>
class Utils
{
public:
    static bool isCr(wchar_t);
    static bool isLf(wchar_t);
    static bool isCrLf(wchar_t);
    static bool isBlank(wchar_t);
    static bool isSpace(wchar_t);
    static bool isHyphen(wchar_t);
    static bool nextLine(const wchar_t*, int, int*, int*, bool*, int*, int*);
    static bool prevLine(const wchar_t*, int, int*, int*, bool*, int*, int*);
    static std::string trim(const std::string& s)
    {
        auto begin = s.find_first_not_of(" \t\r\n");
        return begin == std::string::npos ? "" : s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);
    }
};
