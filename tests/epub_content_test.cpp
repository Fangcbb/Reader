#include "EpubContent.h"
#include <cassert>
#include <iostream>
using namespace EpubContent;

int main()
{
    assert(token("nav scripted", "nav"));
    assert(token(" scripted\tcover-image\n", "cover-image"));
    assert(!token("navigation", "nav"));
    assert(resolve("OPS/Text/chapter.xhtml", "../Images/cover%20one.jpg#view") == "OPS/Images/cover one.jpg");
    assert(resolve("OPS/Text/chapter.xhtml", "#part") == "OPS/Text/chapter.xhtml");
    assert(resolve("OPS/a.xhtml", "../../escape.png").empty());
    assert(resolve("OPS/a.xhtml", "https://example.com/image.png").empty());
    assert(resolve("OPS/a.xhtml", "//example.com/image.png").empty());
    assert(utf16("中文📖").size() == 4);
    const std::string xhtml = R"(<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Book title</title><style>hidden</style></head><body id="start">
      <section id="first"><h1>第一章</h1><p>Hello <em>world</em> &amp; friends.<br/>Next line.</p>
      <img src="../Images/poster.png"/><p>After poster.</p></section>
      <section id="second"><h2>第二章</h2><p>Another chapter 📖.</p><script>hidden</script></section>
      <img src="missing.jpg" alt="missing picture"/><img src="https://example.com/tracker.png" alt="external"/>
      </body></html>)";
    Document doc;
    assert(parse(xhtml.data(), (int)xhtml.size(), "OPS/Text/book.xhtml", doc,
        [](const std::string& path) { return path == "OPS/Images/poster.png"; }));
    assert(doc.anchors.at("start") == 0);
    assert(doc.title == u"第一章");
    assert(doc.headings.size() == 2);
    assert(doc.text.substr(doc.headings[0].first, doc.headings[0].second - doc.headings[0].first) == u"第一章");
    assert(doc.headings[1].first == doc.anchors.at("second"));
    assert(doc.text.find(u"Hello world & friends.\nNext line.\n") != std::u16string::npos);
    assert(doc.text.find(u"hidden") == std::u16string::npos);
    assert(doc.text.substr(doc.anchors.at("first"), 3) == u"第一章");
    assert(doc.text.substr(doc.anchors.at("second"), 3) == u"第二章");
    assert(doc.pictures.size() == 1);
    assert(doc.pictures[0].href == "OPS/Images/poster.png");
    assert(doc.text[doc.pictures[0].offset] == u'\n');
    assert(doc.text.substr(doc.pictures[0].offset + 1, 13) == u"After poster.");
    assert(doc.text.find(u"missing picture") != std::u16string::npos);
    // SVG wrappers in EPUB often use namespaced image hrefs.
    std::string svg = R"(<html xmlns="http://www.w3.org/1999/xhtml" xmlns:xlink="http://www.w3.org/1999/xlink"><body><svg xmlns="http://www.w3.org/2000/svg"><image xlink:href="../Images/cover.jpg"/></svg></body></html>)";
    assert(parse(svg.data(), (int)svg.size(), "OPS/Text/cover.xhtml", doc, [](const std::string&) { return true; }));
    assert(doc.pictures.size() == 1 && doc.pictures[0].href == "OPS/Images/cover.jpg");
    // Image-only chapters must survive extraction and remain navigable.
    assert(doc.text.size() == 1 && doc.text[0] == u'\n');
    // Broken XHTML falls back to a non-networked HTML parser.
    std::string malformed = "<html><body><h1>Title</h1><p>One<p>Two<br>Three</body>";
    assert(parse(malformed.data(), (int)malformed.size(), "book.html", doc, [](const std::string&) { return false; }));
    assert(doc.text.find(u"One\nTwo\nThree") != std::u16string::npos);
    std::string breaks = "<html><body><p>First<br/><br/>Second</p><pre>A\r\nB</pre></body></html>";
    assert(parse(breaks.data(), (int)breaks.size(), "book.xhtml", doc, [](const std::string&) { return false; }));
    assert(doc.text.find(u"First\n\nSecond") != std::u16string::npos);
    assert(doc.text.find(u"A\nB") != std::u16string::npos);
    // Never expand external entities from an ebook.
    std::string entity = "<!DOCTYPE html [<!ENTITY secret SYSTEM 'file:///etc/passwd'>]><html><body><p>&secret;</p></body></html>";
    assert(parse(entity.data(), (int)entity.size(), "book.xhtml", doc, [](const std::string&) { return true; }));
    assert(doc.text.find(u"root:") == std::u16string::npos);
    assert(!parse(nullptr, 0, "book.xhtml", doc, [](const std::string&) { return true; }));
    std::cout << "EPUB content regression checks passed\n";
}
