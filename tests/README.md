# EPUB and reading UI validation

From the repository root on Linux with g++ and the libxml2 runtime:

```sh
./tests/run_epub_tests.sh
```

The test executes the production `EpubContent.h` extractor using the application's
bundled libxml headers. It covers archive URI resolution, properties token lists,
UTF-16 offsets, chapter anchors and heading ranges, paragraph/line boundaries,
image-only pages, image order, missing/external image fallback, SVG wrappers
referencing raster images, malformed XHTML and external-entity isolation.
The same command also checks the production image-fit helper across 400
size/aspect-ratio combinations, nonzero origins, invalid bounds and native-size
limits. It does not build or exercise the Windows GUI.

For real EPUB ZIP → OPF/TOC/cover → text/image page integration checks:

```sh
./tests/run_epub_integration.sh
SANITIZE=1 ./tests/run_epub_integration.sh
```

These require Python 3 with Pillow, gcc/g++ and `libxml2.so.2` on Linux. The runner
creates temporary EPUB fixtures and compiles unchanged production `BookEpub.cpp`,
`HtmlParser.cpp`, `LayoutBase.cpp` and `LayoutText.cpp`, the actual bundled
miniz/cJSON libraries, and the relevant production line-scanning functions.
`tests/portable` supplies the Windows book-owner, UTF conversion, settings, DPI
and measurement interfaces. The layout source is compiled with its production debug assertions enabled.
Additional checks cover surrogate-pair glyph lengths, per-line centering,
intentional blank lines and heading/body pagination. The page measurer uses fixed glyph dimensions;
these checks do **not** test DirectWrite/GDI+, Windows font metrics, the full
BookBase/BookSession implementation, image painting or the Windows ABI.
Fixture generators and temporary source mirrors are outside the checkout and
removed after each run. Sanitizer mode checks addresses and undefined behavior;
leak checking is disabled because LeakSanitizer cannot inspect this ptraced runner.

See [the validation report](../doc/epub-validation.md) for the tested cases,
results and the pagination issue found and fixed during validation.

## Windows acceptance checks (not run in the Linux cloud environment)

Build `Reader.sln` with Visual Studio 2022 C++ desktop tools, v143 and Windows SDK:

```bat
msbuild Reader.sln /m:2 /p:Configuration=Debug /p:Platform=x64 /t:Build
```

- With no open book, check the welcome text and Ctrl+O action. Open TXT and MOBI
  books to check that their existing reading and chapter navigation still work.
- In display settings, try Paper, Green and Night presets. Check the live preview
  with fonts, character/line/paragraph spacing, margins, wrapping and alignment.
  Cancel must preserve the previous appearance; Save must apply it. Restart and
  check that the chapter font option and EPUB image option persist.
- Check settings and the reading window at 100%, 150% and 200% DPI, including
  resizing and borderless/fullscreen modes. Check long chapter labels/tooltips,
  keyboard navigation and theme colors in the chapter/bookmark lists.
- Open EPUB 2 (NCX) and EPUB 3 (nav) books, including multiple anchors in a single
  XHTML spine file, nested chapter labels and filenames containing escaped spaces.
  Check chapter order, jump targets, heading styling and chapter page boundaries.
- Verify EPUB cover variants: manifest `cover-image` with multiple properties,
  EPUB 2 metadata, guide cover page, and an SVG wrapper referencing JPEG/PNG.
  Cover and chapter art should keep their aspect ratio and honor all four margins.
- Read across text → poster → text and image-only chapters using next/previous
  page, line scrolling and chapter jumps. Toggle EPUB chapter images and confirm
  the cover remains available. A corrupt/unsupported image should show a message
  and allow continued reading. Full SVG vector rendering is not implemented.

The EPUB text extractor changed, so existing raw character-offset bookmarks and
saved reading positions can move within EPUB books. Check them when upgrading;
there is no old-to-new offset migration in this change. Original EPUB CSS, tables
and complex page designs are still reflowed into the reader's plain-text layout.

## Native screen picker and independent UI themes

The Windows workflow also runs `scripts/windows-color-picker-test.ps1` against
an external PowerShell-owned color window, and `scripts/windows-ui-theme-test.ps1`
against the actual Reader process. These check desktop sampling, negative client
coordinates, click/drag confirmation, cancellation/capture cleanup, custom UI
colors, persistence and unchanged reading configuration. Pixel comparisons in
`verify-ui-theme-captures.py` verify that shell colors change while the reading
area stays identical. `ui_palette_test.cpp` checks 250 custom-color/contrast
combinations without requiring Windows. Multi-monitor/DPI combinations still
require suitable physical or virtual displays for coverage.
