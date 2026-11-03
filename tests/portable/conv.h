#pragma once
#include "EpubContent.h"
#include <cstdlib>
#include <cstring>
#include <codecvt>
#include <locale>
inline int conv_utf16_to_utf8(const wchar_t* src, int size, char** out)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    auto text = converter.to_bytes(src, src + size);
    *out = (char*)malloc(text.size() + 1);
    memcpy(*out, text.c_str(), text.size() + 1);
    return (int)text.size();
}
inline int conv_to_utf16(const char* src, int size, wchar_t** out)
{
    auto text = EpubContent::utf16(std::string(src, size));
    *out = (wchar_t*)calloc(text.size() + 1, sizeof(wchar_t));
    std::copy(text.begin(), text.end(), *out);
    return (int)text.size();
}
inline void conv_free(void* data) { free(data); }
