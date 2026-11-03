#include "BookEpub.h"
#include "LayoutText.h"
#include "Settings.h"
#include "IRenderer.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

class FixedMeasurer : public IMeasurer
{
public:
    TextMetrics measureText(const wchar_t*, int length, int, int) override { return {length * 8, 16, length}; }
};
std::vector<char> read(const std::string& file)
{
    std::ifstream stream(file, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}
class SampleSource : public ISource
{
public:
    std::wstring content;
    int headingStart = -1, headingEnd = -1;
    const wchar_t* text() const override { return content.c_str(); }
    int length() const override { return (int)content.size(); }
    bool isTitleLine(int i) const override { return i >= headingStart && i < headingEnd; }
    bool isChapterStart(int) const override { return false; }
    bool isChapterContinuous(bool, int) const override { return true; }
    bool isCover(int) const override { return false; }
    bool hasCover() const override { return false; }
};
void checkTextRendering()
{
    auto& config = Settings::instance().data().layout;
    config = {};
    FixedMeasurer measurer;
    SampleSource source;
    LayoutText layout;
    layout.setSource(&source);
    layout.setMeasurer(&measurer);
    layout.setTextStyle({0, 1});
    source.content = L"AB\xD83D\xDCD6" L"CD\n";
    auto page = layout.layout(0, 32, 200);
    int units = 0, pairs = 0;
    for (const auto& line : page.lines)
        for (const auto& glyph : line.chars)
        {
            units += glyph.length;
            if (glyph.length == 2) { ++pairs; assert(glyph.index == 2); }
            else assert(source.content[glyph.index] < 0xD800 || source.content[glyph.index] > 0xDFFF);
        }
    assert(units == 6 && pairs == 1);
    source.content = L"AB\nC\n\n";
    config.alignment = Alignment::CENTER;
    config.indent = true;
    page = layout.layout(0, 80, 200);
    assert(page.x == 0);
    assert(page.lines.size() == 3);
    for (const auto& line : page.lines) assert(line.x == (80 - line.w) / 2);
    source.content = L"A\nHeading\nBody\n";
    source.headingStart = 2;
    source.headingEnd = 9;
    config = {};
    page = layout.layout(0, 160, 32);
    assert(page.length == 2); // keep Heading with at least its first body line
    page = layout.layout(2, 160, 32);
    assert(page.length == source.length() - 2);
    std::cout << "Text rendering: UTF-16 pairs, per-line centering, blank lines and heading pagination passed\n";
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    checkTextRendering();
    std::string folder = argv[1];
    auto& config = Settings::instance().data().layout;
    config = {};
    config.epubImages = true;
    config.wordWrap = true;
    config.noBlansLine = true;
    config.splitChapter = true;
    FixedMeasurer measurer;
    for (std::string name : {"epub3", "epub2", "guide", "no_cover", "missing_image", "encoded_path", "jpeg_cover", "root_package", "invalid_spine"})
    {
        std::string path = folder + "/" + name + ".epub";
        BookEpub book(std::wstring(path.begin(), path.end()));
        auto result = book.open();
        if (name == "invalid_spine")
        {
            assert(result == BookErrno::PARSE_FAIL);
            std::cout << name << ": rejected missing spine content as expected\n";
            continue;
        }
        assert(result == BookErrno::SUCC);
        std::wstring text(book.text(), book.length());
        assert(text.find(L"Hello EPUB reader.") != std::wstring::npos);
        assert(text.find(L"Chapter two text.") != std::wstring::npos);
        assert(text.find(L"NONLINEAR SUPPLEMENT") == std::wstring::npos);
        bool expectCover = name != "no_cover";
        bool expectPoster = name != "missing_image";
        assert(book.hasCover() == expectCover);
        std::string coverFile = name == "jpeg_cover" ? "/cover.jpg" : "/cover.png";
        if (expectCover)
        {
            auto bytes = read(folder + coverFile);
            assert(book.cover()->size == (int)bytes.size());
            assert(std::equal(bytes.begin(), bytes.end(), book.cover()->data));
            assert(book.isCover(0));
        }
        int chapterOne = -1, chapterTwo = -1, art = -1;
        for (const auto& chapter : book.chapters())
        {
            if (chapter.title == L"第一章") chapterOne = chapter.index;
            if (chapter.title == L"第二章") chapterTwo = chapter.index;
            if (chapter.title == L"海报章节") art = chapter.index;
        }
        assert(chapterOne >= 0 && chapterTwo > chapterOne && art > chapterTwo);
        assert(text.substr(chapterOne, 3) == L"第一章");
        assert(text.substr(chapterTwo, 3) == L"第二章");
        assert(book.isTitleLine(chapterOne) && book.isTitleLine(chapterTwo));
        assert(!book.isTitleLine(chapterOne + 4));
        for (int variant = 0; variant < 4; ++variant)
        {
        config.borderL = config.borderR = variant ? 18 : 0;
        config.borderT = config.borderB = variant ? 18 : 0;
        config.lineGap = variant ? 8 : 0;
        config.paragraphGap = variant ? 15 : 0;
        config.splitChapter = variant != 2;
        config.noBlansLine = variant != 3;
        const int width = variant == 1 ? 300 : 160;
        const int height = variant == 1 ? 500 : 100;
        LayoutText layout;
        layout.setSource(&book);
        layout.setMeasurer(&measurer);
        layout.setTextStyle({0, 1});
        std::vector<int> starts;
        int index = 0, pictures = 0;
        while (index < book.length())
        {
            assert(starts.size() < 100);
            starts.push_back(index);
            auto page = layout.layout(index, width, height);
            assert(page.start == index && page.length > 0);
            assert(index + page.length <= book.length());
            if (book.isCover(index))
            {
                ++pictures;
                assert(page.length == 1);
                const auto* picture = book.coverAt(index);
                assert(picture && picture->data && picture->size > 0);
                auto bytes = read(folder + (expectCover && index == 0 ? coverFile : "/poster.png"));
                assert(picture->size == (int)bytes.size());
                assert(std::equal(bytes.begin(), bytes.end(), picture->data));
                std::ofstream extracted(folder + "/" + name + "-" + std::to_string(index) + ".png", std::ios::binary);
                extracted.write(picture->data, picture->size);
            }
            else
            {
                // A text page must never eat the marker of the following poster.
                for (int i = index; i < index + page.length; ++i) assert(!book.isCover(i));
            }
            index += page.length;
        }
        assert(pictures == (expectCover ? 1 : 0) + (expectPoster ? 2 : 0));
        // Text pages are filled backwards and can overlap the prior forward
        // page; the invariant is progress without crossing/skipping image pages.
        for (size_t i = 1; i < starts.size(); ++i)
        {
            int previous = layout.findPrevPageIndex(starts[i], width, height);
            assert(previous >= 0 && previous < starts[i]);
            auto previousPage = layout.layout(previous, width, height);
            assert(previous + previousPage.length >= starts[i]);
            for (int j = previous + 1; j < starts[i]; ++j) assert(!book.isCover(j));
            if (book.isCover(starts[i - 1]))
            {
                assert(previous == starts[i - 1]);
                assert(layout.findPrevLineIndex(starts[i], 1, width) == starts[i - 1]);
            }
        }
        if (expectPoster)
        {
            assert(book.isCover(art));
            config.epubImages = false;
            assert(!book.isCover(art));
            assert(book.hasCover() == expectCover);
            if (expectCover) assert(book.isCover(0));
            int position = 0;
            while (position < book.length())
            {
                auto page = layout.layout(position, width, height);
                assert(page.length > 0);
                position += page.length;
            }
            config.epubImages = true;
        }
        std::cout << name << " [layout " << variant << "]: text + " << book.chapters().size() << " chapters + " << pictures << " image pages + forward/backward pagination passed\n";
        }
    }
}
