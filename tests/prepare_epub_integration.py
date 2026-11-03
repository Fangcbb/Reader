"""Build a temporary Linux mirror: production EPUB/XML/layout sources unchanged,
with narrowly scoped platform adapters. No replacements for parser or pagination.
Generate EPUB archives with real PNG bytes, not stand-ins for an ebook package.
"""
from pathlib import Path
import sys, shutil, zipfile
from PIL import Image

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
for p in (root / 'tests/portable').glob('*.h'):
    shutil.copyfile(p, out / p.name)
for folder, names in [('books', ['BookEpub.cpp', 'BookEpub.h', 'EpubContent.h']),
                      ('utils', ['HtmlParser.cpp', 'HtmlParser.h']),
                      ('layout', ['LayoutText.cpp', 'LayoutText.h', 'LayoutBase.cpp', 'LayoutBase.h', 'ILayout.h'])]:
    for name in names:
        shutil.copyfile(root / 'Reader' / folder / name, out / name)
text = (root / 'Reader/common/types.h').read_text()
# Copy production structures that do not require Win32 types.
(out / 'types.h').write_text('#pragma once\n#include "common.h"\n#include <string>\n#include <vector>\n'
    + 'struct TextMetrics { int width; int height; int count; };\n'
    + text[text.index('enum Alignment'):text.index('// book')]
    + text[text.index('enum class BookType'):text.index('struct ChapterRule')])
source = (root / 'Reader/books/IBook.h').read_text()
(out / 'IBook.h').write_text('#pragma once\n#include "types.h"\n'
    + source[source.index('class ISource'):source.index('class IBook')])
(out / 'common.h').write_text('#pragma once\n#include <cassert>\n#include <algorithm>\n'
    '#define ENABLE_TAG 0\n#ifndef ENABLE_TESTCASE\n#define ENABLE_TESTCASE 0\n#endif\n#define ASSERT(x) assert(x)\n'
    'using std::min; using std::max;\n')
source = (root / 'Reader/utils/Utils.cpp').read_text()
(out / 'Utils.cpp').write_text('#include "Utils.h"\n'
    + source[source.index('bool Utils::isCr('):source.index('void Utils::formatText(')])
for name, color, size in [('cover', '#406a8c', (120, 180)), ('poster', '#c68145', (240, 120))]:
    Image.new('RGB', size, color).save(out / f'{name}.png')
assets = {f'OPS/Images/{name}.png': (out / f'{name}.png').read_bytes() for name in ('cover', 'poster')}
container = '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/package.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'
chapter = '''<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Book</title></head><body>
<section id="one"><h1>第一章</h1><p>Hello EPUB reader.</p><img src="../Images/poster.png"/><p>After the poster.</p></section>
<section id="two"><h2>第二章</h2><p>Chapter two text.</p></section></body></html>'''
nav = '''<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body><nav epub:type="toc landmarks"><ol>
<li><a href="Text/chapter.xhtml#one"><span>第一章</span></a></li>
<li><a href="Text/chapter.xhtml#two">第二章</a></li>
<li><a href="Text/art.xhtml">海报章节</a></li></ol></nav></body></html>'''
ncx = '''<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/"><navMap><navPoint id="one"><navLabel><text>第一章</text></navLabel><content src="Text/chapter.xhtml#one"/></navPoint><navPoint id="two"><navLabel><text>第二章</text></navLabel><content src="Text/chapter.xhtml#two"/></navPoint><navPoint id="art"><navLabel><text>海报章节</text></navLabel><content src="Text/art.xhtml"/></navPoint></navMap></ncx>'''
cover_page = '''<html xmlns="http://www.w3.org/1999/xhtml" xmlns:xlink="http://www.w3.org/1999/xlink"><body><svg xmlns="http://www.w3.org/2000/svg"><image xlink:href="../Images/cover.png"/></svg></body></html>'''
for kind in ['epub3', 'epub2', 'guide', 'no_cover', 'missing_image', 'encoded_path', 'jpeg_cover', 'root_package', 'invalid_spine']:
    v2 = kind == 'epub2'
    metadata = '<meta name="cover" content="cover-img"/>' if v2 else ''
    # Guide-only cover deliberately has an unrelated ID and name; it cannot be
    # accidentally found by the parser's filename/id heuristics.
    props = ' properties="scripted cover-image"' if kind == 'epub3' else ''
    cover_id = 'artwork' if kind == 'guide' else 'cover-img'
    manifest = f'<item id="{cover_id}" href="Images/cover.png" media-type="image/png"{props}/>' if kind != 'no_cover' else ''
    if kind == 'guide':
        manifest += '<item id="front" href="Text/front.xhtml" media-type="application/xhtml+xml"/>'
    toc = '<item id="toc" href="toc.ncx" media-type="application/x-dtbncx+xml"/>' if v2 else '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="scripted nav"/>'
    manifest += toc + '<item id="ch" href="Text/chapter.xhtml" media-type="application/xhtml+xml"/><item id="art" href="Text/art.xhtml" media-type="application/xhtml+xml"/><item id="supplement" href="Text/supplement.xhtml" media-type="application/xhtml+xml"/>'
    guide = '<guide><reference type="cover" href="Text/front.xhtml"/></guide>' if kind == 'guide' else ''
    opf = f'<package xmlns="http://www.idpf.org/2007/opf" version="{2 if v2 else 3}.0"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>EPUB fixture</dc:title>{metadata}</metadata><manifest>{manifest}</manifest><spine><itemref idref="ch"/><itemref idref="art"/><itemref idref="supplement" linear="no"/></spine>{guide}</package>'
    files = dict(assets)
    if kind == 'no_cover': files.pop('OPS/Images/cover.png')
    files.update({'mimetype': 'application/epub+zip', 'META-INF/container.xml': container,
        'OPS/package.opf': opf, 'OPS/nav.xhtml': nav, 'OPS/toc.ncx': ncx,
        'OPS/Text/chapter.xhtml': chapter, 'OPS/Text/front.xhtml': cover_page,
        'OPS/Text/art.xhtml': '<html xmlns="http://www.w3.org/1999/xhtml"><body><img src="../Images/poster.png" alt="Art unavailable"/></body></html>',
        'OPS/Text/supplement.xhtml': '<html><body><p>NONLINEAR SUPPLEMENT</p></body></html>'})
    if kind == 'missing_image': files.pop('OPS/Images/poster.png')
    if kind == 'invalid_spine': files.pop('OPS/Text/chapter.xhtml')
    if kind == 'guide':
        files['OPS/Images/artwork.png'] = files.pop('OPS/Images/cover.png')
        files['OPS/package.opf'] = files['OPS/package.opf'].replace('Images/cover.png', 'Images/artwork.png').replace('<spine>', '<spine><itemref idref="front"/>')
        files['OPS/Text/front.xhtml'] = files['OPS/Text/front.xhtml'].replace('Images/cover.png', 'Images/artwork.png')
    if kind == 'encoded_path':
        files['OPS/Images/cover art.png'] = files.pop('OPS/Images/cover.png')
        files['OPS/package.opf'] = files['OPS/package.opf'].replace('Images/cover.png', 'Images/cover%20art.png')
    if kind == 'jpeg_cover':
        Image.new('RGB', (120, 180), '#406a8c').save(out / 'cover.jpg')
        files['OPS/Images/cover.jpg'] = (out / 'cover.jpg').read_bytes()
        del files['OPS/Images/cover.png']
        files['OPS/package.opf'] = files['OPS/package.opf'].replace('Images/cover.png', 'Images/cover.jpg').replace('media-type="image/png"', 'media-type="image/jpeg"')
    if kind == 'root_package':
        files = {(k[4:] if k.startswith('OPS/') else k): v for k, v in files.items()}
        files['META-INF/container.xml'] = container.replace('OPS/package.opf', 'package.opf')
    with zipfile.ZipFile(out / f'{kind}.epub', 'w') as book:
        for name, data in files.items(): book.writestr(name, data)
