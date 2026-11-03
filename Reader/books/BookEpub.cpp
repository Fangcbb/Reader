#include "BookEpub.h"
#include "miniz.h"
#include "conv.h"
#include "EpubContent.h"
#include "Settings.h"
#include <algorithm>
#include <limits>

BookEpub::BookEpub(const std::wstring& fileName)
    : BookBase(fileName)
    , m_epub(nullptr)
{
}

BookEpub::~BookEpub()
{
    if (m_epub)
    {
        delete m_epub;
        m_epub = nullptr;
    }
}

BookType BookEpub::type() const
{
    return BookType::EPUB;
}

BookErrno BookEpub::parseBook()
{
    Files files;

    if (!loadBook(files))             goto FAIL;
    if (!parseOcf(files))             goto FAIL;
    if (!parseOpf(files))             goto FAIL;
    if (!parseNav(files))             goto FAIL;
    if (!parseNcx(files))             goto FAIL;
    if (!parseCover(files))           goto FAIL;
    if (!splitHtmlByNavpoints(files)) goto FAIL; // for mobi
    if (!parseChapters(files))        goto FAIL;

    freeBook(files);
    return BookErrno::SUCC;

FAIL:
    freeBook(files);
    return BookErrno::PARSE_FAIL;
}

bool BookEpub::loadBook(Files& files)
{
    freeBook(files);

    m_epub = new Epub();
    if (!m_epub) return false;
    m_epub->ocf = "META-INF/container.xml";

    mz_zip_archive zipArchive;
    mz_zip_archive_file_stat fileStat;
    mz_uint fileCount = 0;
    mz_uint i;
    std::string fileName;
    char *buf = nullptr;
    FileData fdata;

#if 0
    conv_utf16_to_ansi(m_fileName.c_str(), (int)m_fileName.size(), &buf);
#else // miniz using utf8 for fileName
    conv_utf16_to_utf8(m_fileName.c_str(), (int)m_fileName.size(), &buf);
#endif
    if (!buf) return false;
    fileName = buf;
    conv_free(buf); buf = nullptr;

    memset(&zipArchive, 0, sizeof(zipArchive));
    if (!mz_zip_reader_init_file(&zipArchive, fileName.c_str(), 0)) return false;

    fileCount = mz_zip_reader_get_num_files(&zipArchive);
    if (fileCount == 0)
    {
        mz_zip_reader_end(&zipArchive);
        return false;
    }

    if (!mz_zip_reader_file_stat(&zipArchive, 0, &fileStat))
    {
        mz_zip_reader_end(&zipArchive);
        return false;
    }

    for (i = 0; i < fileCount; i++)
    {
        if (!mz_zip_reader_file_stat(&zipArchive, i, &fileStat)) continue;
        if (mz_zip_reader_is_file_a_directory(&zipArchive, i)) continue; // skip directories for now

        if (fileStat.m_uncomp_size == 0) continue;
        if (fileStat.m_uncomp_size > 128 * 1024 * 1024 || files.count(fileStat.m_filename))
        {
            mz_zip_reader_end(&zipArchive);
            return false;
        }
        // create memory to save this file
        buf = (char *)malloc((size_t)fileStat.m_uncomp_size);
        if (!buf)
        {
            mz_zip_reader_end(&zipArchive);
            return false;
        }

        // read file to memory
        if (!mz_zip_reader_extract_to_mem(&zipArchive, i, buf, (size_t)fileStat.m_uncomp_size, 0))
        {
            free(buf);
            mz_zip_reader_end(&zipArchive);
            return false;
        }

        // save to file map
        fdata.data = buf;
        fdata.size = (int)fileStat.m_uncomp_size;
        files.insert(std::make_pair(fileStat.m_filename, std::move(fdata)));
    }

    // close the archive, freeing any resources it was using
    mz_zip_reader_end(&zipArchive);
    return !files.empty();
}

void BookEpub::freeBook(Files& files)
{
    for (auto& i : files)
    {
        if (i.second.ownData && i.second.data)
        {
            free(i.second.data);
            i.second.data = nullptr;
            i.second.size = 0;
        }
    }
    files.clear();

    if (m_epub)
    {
        delete m_epub;
        m_epub = nullptr;
    }
}

bool BookEpub::parseOcf(Files& files)
{
    if (m_abort) return false;

    Files::const_iterator itor = files.find(m_epub->ocf);
    if (files.end() == itor) return false;

    std::vector<std::string> value;
    if (!HtmlParser::instance().parseXml(itor->second.data, itor->second.size, "//*[local-name()='rootfile']/@full-path", value)) return false;

    if (value.empty()) return false;

    m_epub->opf = std::move(value.front());
    return true;
}

bool BookEpub::parseOpf(Files& files)
{
    if (m_abort) return false;

    Files::const_iterator itor = files.find(m_epub->opf);
    if (files.end() == itor) return false;

#if 0
    { // for mobi test, dump opf file
        FILE* fp = fopen("package.opf", "wb");
        fwrite(itor->second.data, 1, itor->second.size, fp);
        fclose(fp);
    }
#endif

    // parse manifest
    auto itemNodeParser = [this](const xmlNode *node) {
        if (!node) return;
        xmlChar* id = xmlGetProp(node, BAD_CAST "id");
        xmlChar* href = xmlGetProp(node, BAD_CAST "href");
        xmlChar* mediaType = xmlGetProp(node, BAD_CAST "media-type");
        xmlChar* properties = xmlGetProp(node, BAD_CAST "properties");
        if (id && href && mediaType)
        {
            Manifest manifest;
            manifest.id = (const char*)id;
            manifest.href = formatHref(m_epub->opf, (char*)href);
            manifest.mediaType = (const char*)mediaType;
            if (properties) // EPUB 3
            {
                if (EpubContent::token((const char*)properties, "nav"))
                {
                    if (m_epub->nav.empty()) m_epub->nav = manifest.href;
                }
                if (EpubContent::token((const char*)properties, "cover-image"))
                {
                    if (m_epub->cover.imageHref.empty() && xmlStrcasestr(BAD_CAST mediaType, BAD_CAST "image/"))
                        m_epub->cover.imageHref = manifest.href;
                }
            }
            m_epub->manifests.insert(std::make_pair(manifest.id, std::move(manifest)));
        }
        if (id)         xmlFree(id);
        if (href)       xmlFree(href);
        if (mediaType)  xmlFree(mediaType);
        if (properties) xmlFree(properties);
    };

    ParserContext ctx = {0};
    std::vector<std::string> valueIdref;
    std::vector<std::string> valueToc;
    std::vector<std::string> valueMetaCover;
    std::vector<std::string> valueGuideCover;
    std::vector<std::string> valueTitle;
    if (!HtmlParser::instance().parseXmlBegin(itor->second.data, itor->second.size, ctx)) return false;
    HtmlParser::instance().parseXmlNext(ctx, "//*[local-name()='item']", itemNodeParser);
    HtmlParser::instance().parseXmlNext(ctx, "//*[local-name()='itemref'][not(@linear='no')]/@idref", valueIdref);
    HtmlParser::instance().parseXmlNext(ctx, "//*[local-name()='spine']/@toc", valueToc);
    HtmlParser::instance().parseXmlNext(ctx, "//*[local-name()='meta'][@name='cover' or @name='Cover']/@content", valueMetaCover);
    HtmlParser::instance().parseXmlNext(ctx, "//*[local-name()='guide']/*[local-name()='reference'][@type='cover' or contains(@type, 'Cover')]/@href", valueGuideCover);
    HtmlParser::instance().parseXmlNext(ctx, "//*[local-name()='title' and namespace-uri()='http://purl.org/dc/elements/1.1/']", valueTitle);
    HtmlParser::instance().parseXmlEnd(ctx);

    if (m_epub->manifests.empty()) return false;

    // parse spine
    if (m_abort) return false;
    if (valueIdref.empty()) return false;
    for (auto& i : valueIdref)
    {
        auto it = m_epub->manifests.find(i);
        if (m_epub->manifests.end() == it) return false;
        if (0 != xmlStrcasecmp(BAD_CAST it->second.mediaType.c_str(), BAD_CAST "application/xhtml+xml")) continue; // skip
        m_epub->spines.push_back(&(it->second));
    }
    if (m_epub->spines.empty()) return false;

    // EPUB 3: nav.xhtml. parse by manifest > properties
    if (m_epub->nav.empty())
    {
    }

    // EPUB 2：toc.ncx. parse ncx, ncx is not a required file for EPUB 3
    if (m_abort) return false;
    for (auto& i : valueToc)
    {
        auto it = m_epub->manifests.find(i);
        if (m_epub->manifests.end() != it &&
            0 == xmlStrcasecmp(BAD_CAST it->second.mediaType.c_str(), BAD_CAST "application/x-dtbncx+xml"))
        {
            m_epub->ncx = it->second.href;
            break;
        }
    }

    if (m_epub->ncx.empty())
    {
        for (const auto& item : m_epub->manifests)
            if (item.second.mediaType == "application/x-dtbncx+xml") { m_epub->ncx = item.second.href; break; }
    }

    // parse cover from <meta>
    if (m_epub->cover.imageHref.empty() || m_epub->cover.xhtmlHref.empty())
    {
        if (m_abort) return false;
        for (auto& i : valueMetaCover)
        {
            auto it = m_epub->manifests.find(i);
            if (m_epub->manifests.end() != it)
            {
                if (m_epub->cover.imageHref.empty() && xmlStrcasestr(BAD_CAST it->second.mediaType.c_str(), BAD_CAST "image/"))
                    m_epub->cover.imageHref = it->second.href;
                else if (m_epub->cover.xhtmlHref.empty() && 0 == xmlStrcasecmp(BAD_CAST it->second.mediaType.c_str(), BAD_CAST "application/xhtml+xml"))
                    m_epub->cover.xhtmlHref = it->second.href;
            }
            if (!m_epub->cover.imageHref.empty() && !m_epub->cover.xhtmlHref.empty()) break;
        }
    }

    // parse cover from <guide>
    if (m_epub->cover.imageHref.empty() || m_epub->cover.xhtmlHref.empty())
    {
        if (m_abort) return false;
        bool found = false;
        for (auto& i : valueGuideCover)
        {
            std::string href = formatHref(m_epub->opf, i);
            for (auto& j : m_epub->manifests)
            {
                if (0 == xmlStrcasecmp(BAD_CAST j.second.href.c_str(), BAD_CAST href.c_str()))
                {
                    if (m_epub->cover.imageHref.empty() && xmlStrcasestr(BAD_CAST j.second.mediaType.c_str(), BAD_CAST "image/"))
                        m_epub->cover.imageHref = j.second.href;
                    else if (m_epub->cover.xhtmlHref.empty() && 0 == xmlStrcasecmp(BAD_CAST j.second.mediaType.c_str(), BAD_CAST "application/xhtml+xml"))
                        m_epub->cover.xhtmlHref = j.second.href;
                    break;
                }
            }
            if (!m_epub->cover.imageHref.empty() && !m_epub->cover.xhtmlHref.empty()) break;
        }
    }

    if (m_epub->cover.imageHref.empty() || m_epub->cover.xhtmlHref.empty())
    {
        // find cover from <manifest>
        if (m_abort) return false;
        for (auto& i : m_epub->manifests)
        {
            if (xmlStrcasestr(BAD_CAST i.second.id.c_str(), BAD_CAST "cover") ||
                xmlStrcasestr(BAD_CAST i.second.href.c_str(), BAD_CAST "cover"))
            {
                if (m_epub->cover.imageHref.empty() && xmlStrcasestr(BAD_CAST i.second.mediaType.c_str(), BAD_CAST "image/"))
                    m_epub->cover.imageHref = i.second.href;
                else if (m_epub->cover.xhtmlHref.empty() && 0 == xmlStrcasecmp(BAD_CAST i.second.mediaType.c_str(), BAD_CAST "application/xhtml+xml"))
                    m_epub->cover.xhtmlHref = i.second.href;
            }
            if (!m_epub->cover.imageHref.empty() && !m_epub->cover.xhtmlHref.empty()) break;
        }
    }

    if (!valueTitle.empty())
    {
        wchar_t* bookName = NULL;
        conv_to_utf16(valueTitle.front().c_str(), (int)valueTitle.front().size(), &bookName);
        if (bookName)
        {
            m_bookName = bookName;
            conv_free(bookName);
        }
    }

    return true;
}

bool BookEpub::parseNav(Files& files)
{
    if (m_abort) return false;
    if (m_epub->nav.empty()) return true; // nav is not required
    if (!m_epub->navpoints.empty()) return true; // already parsed

    Files::const_iterator itor = files.find(m_epub->nav);
    if (files.end() == itor) return true;

    auto itemNodeParser = [this](const xmlNode *node) {
        if (!node) return;
        xmlChar* href = xmlGetProp(node, BAD_CAST "href");
        xmlChar* text = xmlNodeGetContent(node);
        if (href && text)
        {
            Navpoint navpoint;
            navpoint.id = "";
            navpoint.href = formatHref(m_epub->nav, (char*)href);
            if (type() == BookType::EPUB)
            {
                std::string target = (char*)href;
                size_t fragment = target.find('#');
                if (fragment != std::string::npos) navpoint.href += "#" + decodeHref(target.substr(fragment + 1));
            }
            navpoint.text = (char*)text;
            navpoint.order = -1;
            m_epub->navpoints.insert(std::make_pair(navpoint.href, std::move(navpoint)));
        }
        if (href) xmlFree(href);
        if (text) xmlFree(text);
    };

    ParserContext ctx = {0};
    if (!HtmlParser::instance().parseXmlBegin(itor->second.data, itor->second.size, ctx)) return true;
    HtmlParser::instance().parseXmlNext(ctx, "//*[local-name()='nav'][contains(concat(' ', normalize-space(@*[local-name()='type']), ' '), ' toc ') or @id='toc' or @role='doc-toc']//*[local-name()='a']", itemNodeParser);
    HtmlParser::instance().parseXmlEnd(ctx);
    return true;
}

bool BookEpub::parseNcx(Files& files)
{
    if (m_abort) return false;
    if (m_epub->ncx.empty()) return true; // ncx is not required
    if (!m_epub->navpoints.empty()) return true; // already parsed

    Files::const_iterator itor = files.find(m_epub->ncx);
    if (files.end() == itor) return true;

#if 0
    { // for mobi test, dump ncx file
        FILE* fp = fopen("toc.ncx", "wb");
        fwrite(itor->second.data, 1, itor->second.size, fp);
        fclose(fp);
    }
#endif

    auto itemNodeParser = [this](const xmlNode *node) {
        if (!node) return;

        xmlChar* id = xmlGetProp(node, BAD_CAST "id");
        xmlChar* order = xmlGetProp(node, BAD_CAST "playOrder");
        xmlChar* text = nullptr;
        xmlChar* src = nullptr;

        node = node->children;
        while (node)
        {
            if (!src && xmlStrcasecmp(node->name, BAD_CAST "content") == 0)
            {
                src = xmlGetProp(node, BAD_CAST "src");
            }
            if (!text && xmlStrcasecmp(node->name, BAD_CAST "navLabel") == 0)
            {
                text = xmlNodeGetContent(node->children);
            }
            if (src && text) break;
            node = node->next;
        }
        if (src && text)
        {
            Navpoint navpoint;
            if (id) navpoint.id = (const char*)id;
            navpoint.href = formatHref(m_epub->ncx, (char*)src);
            if (type() == BookType::EPUB)
            {
                std::string target = (char*)src;
                size_t fragment = target.find('#');
                if (fragment != std::string::npos) navpoint.href += "#" + decodeHref(target.substr(fragment + 1));
            }
            navpoint.text = (char*)text;
            navpoint.order = order ? atoi((const char *)order) : -1;
            m_epub->navpoints.insert(std::make_pair(navpoint.href, std::move(navpoint)));
        }
        if (id)    xmlFree(id);
        if (order) xmlFree(order);
        if (text)  xmlFree(text);
        if (src)   xmlFree(src);
    };

    ParserContext ctx = {0};
    if (!HtmlParser::instance().parseXmlBegin(itor->second.data, itor->second.size, ctx)) return false;
    HtmlParser::instance().parseXmlNext(ctx, "//*[local-name()='navPoint']", itemNodeParser);
    HtmlParser::instance().parseXmlEnd(ctx);
    return true;
}

bool BookEpub::parseCover(Files& files)
{
    if (m_abort) return false;
    if (m_epub->cover.xhtmlHref.empty() && m_epub->cover.imageHref.empty()) return true;
    freeCover();

    // find image from cover.xhtml
    if (m_epub->cover.imageHref.empty() && !m_epub->cover.xhtmlHref.empty())
    {
        ParserContext ctx = {0};
        std::vector<std::string> value;
        auto itor = files.find(m_epub->cover.xhtmlHref);
        if (files.end() == itor) return true; // not found

        auto itemNodeParser = [this](const xmlNode *node) {
            if (!node) return;
            if (!m_epub->cover.imageHref.empty()) return;
            xmlChar* src = xmlGetProp(node, BAD_CAST "src");
            if (src)
            {
                m_epub->cover.imageHref = formatHref(m_epub->cover.xhtmlHref ,(char*)src);
                xmlFree(src);
                return;
            }
            xmlChar* href = xmlGetProp(node, BAD_CAST "href");
            if (!href) href = xmlGetNsProp(node, BAD_CAST "href", BAD_CAST "http://www.w3.org/1999/xlink");
            if (href)
            {
                m_epub->cover.imageHref = formatHref(m_epub->cover.xhtmlHref ,(char*)href);;
                xmlFree(href);
                return;
            }
        };

        if (!HtmlParser::instance().parseXmlBegin(itor->second.data, itor->second.size, ctx)) return true;
        HtmlParser::instance().parseXmlNext(ctx, "//*[local-name()='img' or local-name()='image']", itemNodeParser);
        HtmlParser::instance().parseXmlEnd(ctx);
        if (m_abort) return false;
    }

    if (!m_epub->cover.imageHref.empty())
    {
        auto itor = files.find(m_epub->cover.imageHref);
        if (files.end() == itor) return true; // not found

        if (!itor->second.data || itor->second.size <= 0) return true;
        m_cover = (Cover*)calloc(1, sizeof(Cover));
        if (!m_cover) return false;
        m_cover->data = (char*)malloc(itor->second.size);
        if (!m_cover->data) { freeCover(); return false; }
        memcpy(m_cover->data, itor->second.data, itor->second.size);
        m_cover->size = itor->second.size;
    }
    return true;
}

bool BookEpub::isTitleLine(int index) const
{
    if (type() != BookType::EPUB) return BookBase::isTitleLine(index);
    for (const auto& heading : m_headingRanges)
    {
        if (index < heading.first) break;
        if (index < heading.second) return true;
    }
    return false;
}

bool BookEpub::isCover(int index) const
{
    return BookBase::isCover(index) ||
        (Settings::instance().data().layout.epubImages && m_pictures.find(index) != m_pictures.end());
}

const Cover* BookEpub::coverAt(int index) const
{
    auto picture = m_pictures.find(index);
    if (picture != m_pictures.end()) return &picture->second;
    return BookBase::isCover(index) ? m_cover : nullptr;
}

bool BookEpub::parseChapters(Files& files)
{
    if (type() != BookType::EPUB) return parseLegacyChapters(files);
    m_pictureData.clear();
    m_pictures.clear();
    m_headingRanges.clear();
    m_chapters.clear();
    std::u16string text(m_cover ? 1 : 0, u'\n');
    if (m_cover)
    {
        Chapter coverChapter = {};
        coverChapter.index = 0;
        coverChapter.title = L"封面";
        m_chapters.push_back(coverChapter);
    }
    for (const auto* spine : m_epub->spines)
    {
        if (m_abort) return false;
        auto file = files.find(spine->href);
        if (file == files.end()) return false; // do not silently lose reading-order content
        EpubContent::Document document;
        auto acceptImage = [&](const std::string& href) {
            auto image = files.find(href);
            return !(m_cover && href == m_epub->cover.imageHref) && image != files.end()
                && image->second.data && image->second.size > 0;
        };
        if (!EpubContent::parse(file->second.data, file->second.size, spine->href, document, acceptImage)) return false;
        if (document.text.empty()) continue;
        if (text.size() + document.text.size() >= (size_t)(std::numeric_limits<int>::max)()) return false;
        const int offset = (int)text.size();
        std::vector<Chapter> chapters;
        for (const auto& nav : m_epub->navpoints)
        {
            size_t hash = nav.first.find('#');
            if (nav.first.substr(0, hash) != spine->href) continue;
            size_t position = 0;
            if (hash != std::string::npos)
            {
                auto anchor = document.anchors.find(nav.first.substr(hash + 1));
                if (anchor == document.anchors.end()) continue;
                position = anchor->second;
            }
            if (position >= document.text.size()) continue;
            Chapter chapter = {};
            chapter.index = offset + (int)position;
            auto title = EpubContent::utf16(nav.second.text);
            chapter.title.assign(title.begin(), title.end());
            if (!chapter.title.empty()) chapters.push_back(chapter);
        }
        if (chapters.empty())
        {
            Chapter chapter = {};
            chapter.index = offset;
            chapter.title.assign(document.title.begin(), document.title.end());
            if (chapter.title.empty()) chapter.title = L"章节 " + std::to_wstring(m_chapters.size() + 1);
            chapters.push_back(chapter);
        }
        std::stable_sort(chapters.begin(), chapters.end(), [](const Chapter& a, const Chapter& b) { return a.index < b.index; });
        int last = -1;
        for (auto& chapter : chapters)
        {
            if (chapter.index == last) continue;
            last = chapter.index;
            // Only style actual heading text, never an arbitrary TOC-label-length prefix.
            size_t start = chapter.index - offset;
            size_t end = document.text.find(u'\n', start);
            if (end != std::u16string::npos && std::wstring(document.text.begin() + start, document.text.begin() + end) == chapter.title)
                chapter.titleLineLen = (int)(end - start);
            m_chapters.push_back(chapter);
        }
        for (const auto& heading : document.headings)
            m_headingRanges.push_back({offset + (int)heading.first, offset + (int)heading.second});
        for (const auto& picture : document.pictures)
        {
            const auto& image = files.find(picture.href)->second;
            int index = offset + (int)picture.offset;
            auto& bytes = m_pictureData[index];
            bytes.assign(image.data, image.data + image.size);
            m_pictures[index] = {bytes.data(), (int)bytes.size()};
        }
        text += document.text;
    }
    if (text.empty()) return false;
    free(m_text);
    m_text = (wchar_t*)malloc((text.size() + 1) * sizeof(wchar_t));
    if (!m_text) return false;
    std::copy(text.begin(), text.end(), m_text);
    m_length = (int)text.size();
    m_text[m_length] = 0;
    return true;
}

bool BookEpub::parseLegacyChapters(Files& files)
{
    struct Buffer
    {
        wchar_t *text;
        int len;
    };

    if (m_abort) return false;

    std::vector<Buffer> buffers;
    buffers.reserve(m_epub->spines.size());
    m_chapters.reserve(m_epub->spines.size());
    m_length = m_cover ? 1 : 0; // add one character for cover image
    bool useSpineTitle = m_epub->navpoints.empty();

    for (const auto& spine : m_epub->spines)
    {
        const auto file = files.find(spine->href);
        if (files.end() == file) continue;

        wchar_t *text = NULL;
        wchar_t *title = NULL;
        int len = 0;
        int tlen = 0;
        if (parseOps(file->second, &text, &len, &title, &tlen, useSpineTitle))
        {
            if (len > 0 && text)
            {
                Buffer buffer;
                buffer.text = text; text = nullptr;
                buffer.len = len;
                buffers.push_back(std::move(buffer));
            }
            Chapter chapter;
            chapter.index = m_length;
            if (m_cover && (spine->href == m_epub->cover.xhtmlHref || spine->href == m_epub->cover.imageHref
                || (m_epub->cover.xhtmlHref.empty() && m_length == 1 && len == 0)/*first empty spine*/))
            {
                chapter.index = 0;
            }
            if (useSpineTitle)
            {
                if (tlen > 0 && title)
                {
                    chapter.title = title;
                    chapter.titleLineLen = tlen;
                }
            }
            else
            {
                if (title) { conv_free(title); title = nullptr; }
                auto navpoint = m_epub->navpoints.find(spine->href);
                if (m_epub->navpoints.end() != navpoint &&
                    decodeText(navpoint->second.text.c_str(), (int)navpoint->second.text.size(), &title, &tlen))
                {
                    chapter.title = title;
                    chapter.titleLineLen = tlen;
                }
            }
            if (!chapter.title.empty()) m_chapters.push_back(std::move(chapter));
            m_length += len;
        }
        if (title) conv_free(title);
        if (text) conv_free(text);

        if (m_abort)
        {
            // free buffer
            for (auto& i : buffers) if (i.text) conv_free(i.text);
            return false;
        }
    }

    // rebuild m_text
    m_text = (wchar_t *)malloc(sizeof(wchar_t) * (m_length + 1));
    m_text[m_length] = 0;
    int offset = 0;
    if (m_cover)
    {
        m_text[0] = 0x000A; // '\n' replacement character for cover image
        offset = 1;
    }
    for (auto& i : buffers)
    {
        if (i.text)
        {
            if (i.len > 0)
            {
                memcpy(m_text + offset, i.text, sizeof(wchar_t) * i.len);
                offset += i.len;
            }
            conv_free(i.text);
        }
    }
    return true;
}

bool BookEpub::parseOps(FileData& file, wchar_t **text, int *len, wchar_t **title, int *tlen, bool needTitle)
{
    if (m_abort) return false;
    if (!file.data || file.size <= 0 || !text || !len) return false;
    if (needTitle && (!title || !tlen)) return false;

#if 1
    std::string formated;
    if (!HtmlParser::instance().formatHtml(file.data, file.size, formated)) return false;

    ParserContext ctx = {0};
    std::vector<std::string> value;
    if (!HtmlParser::instance().parseHtmlBegin(formated.c_str(), (int)formated.size(), ctx)) return false;

    // parse title
    if (needTitle)
    {
        HtmlParser::instance().parseHtmlNext(ctx, "//*[local-name()='title']", value);
        if (!value.empty())
        {
            const auto& front = value.front();
            if (!decodeText(front.c_str(), (int)front.size(), title, tlen))
            {
                HtmlParser::instance().parseHtmlEnd(ctx);
                return false;
            }
        }
    }

    // parse body
    HtmlParser::instance().parseHtmlNext(ctx, "//*[local-name()='body']", value);
    HtmlParser::instance().parseHtmlEnd(ctx);

    if (value.empty()) return false;
    const auto& front = value.front();
    if (!decodeText(front.c_str(), (int)front.size(), text, len)) return false;

    return true;
#else // Not use format, to fast parse epub file
    xmlDocPtr doc = htmlReadMemory(file.data, file.size, nullptr, nullptr, HTML_PARSE_RECOVER | HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET);
    if (!doc) return false;

    xmlXPathContextPtr xpathCtx = xmlXPathNewContext(doc);
    if (!xpathCtx)
    {
        xmlFreeDoc(doc);
        return false;
    }

    // parse title
    if (needTitle)
    {
        xmlXPathObjectPtr xpathObj = xmlXPathEvalExpression(BAD_CAST "//*[local-name()='title']", xpathCtx);
        if (xpathObj)
        {
            if (!xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
            {
                xmlChar* content = xmlNodeGetContent(xpathObj->nodesetval->nodeTab[0]);
                if (content)
                {
                    decodeText((const char*)content, (int)xmlStrlen(content), title, tlen);
                    xmlFree(content);
                }
            }
            xmlXPathFreeObject(xpathObj);
        }
    }

    // parse body
    bool ret = false;
    xmlXPathObjectPtr xpathObj = xmlXPathEvalExpression(BAD_CAST "//*[local-name()='body']", xpathCtx);
    if (xpathObj)
    {
        if (!xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
        {
            xmlChar* content = xmlNodeGetContent(xpathObj->nodesetval->nodeTab[0]);
            if (content)
            {
                ret = decodeText((const char*)content, (int)xmlStrlen(content), text, len);
                xmlFree(content);
            }
        }
        xmlXPathFreeObject(xpathObj);
    }

    xmlXPathFreeContext(xpathCtx);
    xmlFreeDoc(doc);

    return ret;
#endif
}

bool BookEpub::splitHtmlByNavpoints(Files& files)
{
    // not use for epub
    return true;
}

std::string BookEpub::formatHref(const std::string& base, const std::string& href)
{
    return EpubContent::resolve(base, href);
}

std::string BookEpub::decodeHref(const std::string& href)
{
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    std::string out;
    out.reserve(href.size());
    for (size_t i = 0; i < href.size(); ++i)
    {
        if (href[i] == '%' && i + 2 < href.size())
        {
            int h = hex(href[i + 1]), l = hex(href[i + 2]);
            if (h >= 0 && l >= 0)
            {
                out += char((h << 4) | l);
                i += 2;
                continue;
            }
        }
        out += href[i];
    }
    return out;
}

std::string BookEpub::resolveHref(const std::string& base, const std::string& href)
{
    std::string path = base;
    size_t pos = path.find_last_of('/');
    path = pos == std::string::npos ? "" : path.substr(0, pos + 1);
    path += href;

    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size())
    {
        size_t end = path.find('/', start);
        std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..")
        {
            if (!parts.empty()) parts.pop_back();
        }
        else if (!part.empty() && part != ".")
            parts.push_back(part);
        if (end == std::string::npos) break;
        start = end + 1;
    }

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i) result += '/';
        result += parts[i];
    }
    return result;
}
