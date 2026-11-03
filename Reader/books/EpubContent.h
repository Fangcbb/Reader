#pragma once
// Platform-independent XHTML extraction. Offsets are UTF-16 code units, as used
// by the Windows reader; anchors and images share exactly the same text stream.
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>
#include "libxml/HTMLparser.h"
#include "libxml/uri.h"

namespace EpubContent
{
inline bool token(const std::string& values, const std::string& wanted)
{
    size_t begin = 0;
    while (begin < values.size())
    {
        begin = values.find_first_not_of(" \t\r\n", begin);
        if (begin == std::string::npos) break;
        size_t end = values.find_first_of(" \t\r\n", begin);
        if (values.substr(begin, end - begin) == wanted) return true;
        begin = end;
    }
    return false;
}
inline std::string decode(const std::string& value)
{
    char* result = xmlURIUnescapeString(value.c_str(), (int)value.size(), nullptr);
    if (!result) return value;
    std::string out(result);
    xmlFree(result);
    return out;
}
inline std::string resolve(const std::string& base, const std::string& href)
{
    // Archive-relative paths only. Never fetch external resources.
    if (href.find(':') != std::string::npos || href.compare(0, 2, "//") == 0) return "";
    std::string path = href.substr(0, href.find_first_of("?#"));
    path = decode(path);
    if (path.empty()) return base.substr(0, base.find('#'));
    if (path.front() != '/')
    {
        size_t slash = base.find_last_of('/');
        if (slash != std::string::npos) path = base.substr(0, slash + 1) + path;
    }
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size())
    {
        size_t end = path.find('/', start);
        std::string part = path.substr(start, end - start);
        if (part == "..") { if (parts.empty()) return ""; parts.pop_back(); }
        else if (!part.empty() && part != ".") parts.push_back(part);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    std::string result;
    for (const auto& part : parts) { if (!result.empty()) result += '/'; result += part; }
    return result;
}
inline std::u16string utf16(const std::string& text)
{
    std::u16string out;
    const unsigned char* p = (const unsigned char*)text.data();
    size_t i = 0;
    while (i < text.size())
    {
        uint32_t c = p[i++];
        if (c >= 0x80)
        {
            int n = c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
            c &= n == 1 ? 0x1F : n == 2 ? 0x0F : 0x07;
            while (n-- && i < text.size()) c = (c << 6) | (p[i++] & 0x3F);
        }
        if (c > 0xFFFF) { c -= 0x10000; out += char16_t(0xD800 + (c >> 10)); out += char16_t(0xDC00 + (c & 1023)); }
        else out += char16_t(c);
    }
    return out;
}
inline std::string prop(const xmlNode* node, const char* name)
{
    xmlChar* value = xmlGetProp(node, BAD_CAST name);
    if (!value) return "";
    std::string out((const char*)value);
    xmlFree(value);
    return out;
}
struct Picture { size_t offset; std::string href; };
struct Document
{
    std::u16string text;
    std::u16string title;
    std::map<std::string, size_t> anchors;
    std::vector<Picture> pictures;
    std::vector<std::pair<size_t, size_t>> headings;
};
inline void newline(std::u16string& text)
{
    while (!text.empty() && text.back() == u' ') text.pop_back();
    if (!text.empty() && text.back() != u'\n') text += u'\n';
}
inline bool block(const std::string& name)
{
    return token("p div section article h1 h2 h3 h4 h5 h6 li blockquote pre tr dt dd figure figcaption", name);
}
inline void walk(const xmlNode* node, Document& out, const std::string& base,
    const std::function<bool(const std::string&)>& acceptImage, bool pre = false)
{
    for (; node; node = node->next)
    {
        if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE)
        {
            auto text = utf16(node->content ? (const char*)node->content : "");
            for (size_t i = 0; i < text.size(); ++i)
            {
                char16_t ch = text[i];
                if (pre && ch == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n') continue;
                if (!pre && (ch == u' ' || ch == u'\t' || ch == u'\r' || ch == u'\n'))
                {
                    if (!out.text.empty() && out.text.back() != u' ' && out.text.back() != u'\n') out.text += u' ';
                }
                else out.text += ch == u'\r' ? u'\n' : ch;
            }
            continue;
        }
        if (node->type != XML_ELEMENT_NODE) continue;
        std::string name((const char*)node->name);
        if (token("script style head nav noscript", name)) continue;
        if (name == "br") out.text += u'\n';
        else if (block(name) || name == "hr") newline(out.text);
        std::string id = prop(node, "id");
        if (id.empty() && name == "a") id = prop(node, "name");
        if (!id.empty()) out.anchors.emplace(id, out.text.size());
        if (name == "img" || name == "image")
        {
            std::string src = prop(node, "src");
            if (src.empty()) src = prop(node, "href");
            if (src.empty())
            {
                xmlChar* value = xmlGetNsProp(node, BAD_CAST "href", BAD_CAST "http://www.w3.org/1999/xlink");
                if (value) { src = (const char*)value; xmlFree(value); }
            }
            std::string href = resolve(base, src);
            if (!src.empty() && !href.empty() && acceptImage(href))
            {
                newline(out.text);
                out.pictures.push_back({out.text.size(), href});
                out.text += u'\n'; // isolated page marker consumed by LayoutText
            }
            else
            {
                out.text += utf16(prop(node, "alt"));
            }
        }
        else
        {
            size_t begin = out.text.size();
            walk(node->children, out, base, acceptImage, pre || name == "pre");
            if (token("h1 h2 h3 h4 h5 h6", name) && out.text.size() > begin)
                out.headings.push_back({begin, out.text.size()});
        }
        if (block(name)) newline(out.text);
    }
}
inline const xmlNode* find(const xmlNode* node, const char* name)
{
    for (; node; node = node->next)
    {
        if (node->type == XML_ELEMENT_NODE && xmlStrEqual(node->name, BAD_CAST name)) return node;
        if (const auto* result = find(node->children, name)) return result;
    }
    return nullptr;
}
inline bool parse(const char* data, int size, const std::string& base, Document& out,
    const std::function<bool(const std::string&)>& acceptImage)
{
    out = Document();
    if (!data || size <= 0) return false;
    xmlDocPtr doc = xmlReadMemory(data, size, nullptr, nullptr, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) doc = htmlReadMemory(data, size, nullptr, "UTF-8", HTML_PARSE_NONET | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_RECOVER);
    if (!doc) return false;
    const xmlNode* root = xmlDocGetRootElement(doc);
    const xmlNode* body = find(root, "body");
    const xmlNode* title = body ? find(body->children, "h1") : nullptr;
    if (!title && body) title = find(body->children, "h2");
    if (!title) title = find(root, "title");
    if (title)
    {
        xmlChar* text = xmlNodeGetContent(title);
        if (text) { out.title = utf16((const char*)text); xmlFree(text); }
    }
    if (body)
    {
        std::string id = prop(body, "id");
        if (!id.empty()) out.anchors.emplace(id, 0);
        walk(body->children, out, base, acceptImage);
    }
    newline(out.text);
    xmlFreeDoc(doc);
    return body != nullptr;
}
}
