#pragma once
// Linux test adapter for the Windows-only book owner. EPUB parsing itself is
// compiled unchanged from BookEpub.cpp; this adapter does not render anything.
#include "IBook.h"
#include "EpubContent.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>
class BookBase : public ISource
{
public:
    explicit BookBase(const std::wstring& path) : m_fileName(path) {}
    virtual ~BookBase() { free(m_text); freeCover(); }
    BookErrno open() { return parseBook(); }
    int length() const override { return m_length; }
    const wchar_t* text() const override { return m_text; }
    virtual bool isTitleLine(int) const { return false; }
    bool isChapterStart(int index) const override { for (const auto& c : m_chapters) if(c.index == index) return true; return false; }
    bool isChapterContinuous(bool, int) const override { return true; }
    bool isCover(int index) const override { return m_cover && index == 0; }
    bool hasCover() const override { return m_cover != nullptr; }
    const Cover* cover() const { return m_cover; }
    const std::vector<Chapter>& chapters() const { return m_chapters; }
protected:
    virtual BookErrno parseBook() = 0;
    bool decodeText(const char* src, int length, wchar_t** out, int* size)
    {
        auto text = EpubContent::utf16(std::string(src, length));
        *size = (int)text.size();
        *out = (wchar_t*)calloc(text.size() + 1, sizeof(wchar_t));
        std::copy(text.begin(), text.end(), *out);
        return *out != nullptr;
    }
    void freeCover() { if (m_cover) { free(m_cover->data); free(m_cover); m_cover = nullptr; } }
    wchar_t* m_text = nullptr;
    int m_length = 0;
    std::wstring m_fileName, m_bookName;
    std::vector<Chapter> m_chapters;
    Cover* m_cover = nullptr;
    bool m_abort = false;
};
