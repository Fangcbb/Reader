#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
binary=$(mktemp /tmp/reader-epub-tests.XXXXXX)
trap 'rm -f "$binary"' EXIT
# Use the same bundled libxml headers as the application. On Debian, the
# runtime library alone is sufficient; no MSVC or system development headers.
if pkg-config --exists libxml-2.0 2>/dev/null; then
    read -r -a xml_flags <<< "$(pkg-config --libs libxml-2.0)"
else
    xml_flags=(-l:libxml2.so.2)
fi
"${CXX:-g++}" -std=c++14 -Wall -Wextra -pedantic -UNDEBUG \
    -IReader/books -I3rdparty/libxml2/inc \
    tests/epub_content_test.cpp "${xml_flags[@]}" -o "$binary"
"$binary"
"${CXX:-g++}" -std=c++14 -Wall -Wextra -pedantic -UNDEBUG \
    -IReader/renderer tests/image_fit_test.cpp -o "$binary"
"$binary"
"${CXX:-g++}" -std=c++14 -Wall -Wextra -pedantic -UNDEBUG \
    -IReader/common tests/ui_palette_test.cpp -o "$binary"
"$binary"
