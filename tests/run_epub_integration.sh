#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
output=$(mktemp -d /tmp/reader-epub-integration.XXXXXX)
trap 'rm -rf "$output"' EXIT
python3 tests/prepare_epub_integration.py "$output"
sanitizers=()
if [[ "${SANITIZE:-0}" == 1 ]]; then
    sanitizers=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
incs=(-I"$output" -I3rdparty/miniz -I3rdparty/libxml2/inc -I3rdparty/cjson/inc)
# Compile the actual bundled ZIP/JSON libraries as C, as in the application.
for source in 3rdparty/miniz/miniz.c 3rdparty/cjson/src/cJSON.c 3rdparty/cjson/src/cJSON_Path.c 3rdparty/cjson/src/cJSON_XPath.c; do
    gcc -w "${sanitizers[@]}" -D_strdup=strdup "${incs[@]}" -c "$source" -o "$output/$(basename "$source").o"
done
"${CXX:-g++}" "${sanitizers[@]}" -std=c++14 -g -UNDEBUG -DENABLE_TESTCASE=1 "${incs[@]}" \
    -c "$output/LayoutText.cpp" -o "$output/LayoutText.o"
"${CXX:-g++}" "${sanitizers[@]}" -std=c++14 -g -UNDEBUG -Wno-write-strings "${incs[@]}" \
    tests/epub_integration_test.cpp "$output/BookEpub.cpp" \
    "$output/HtmlParser.cpp" "$output/LayoutBase.cpp" "$output/Utils.cpp" \
    "$output"/*.o -l:libxml2.so.2 -o "$output/test"
# LeakSanitizer cannot inspect this ptraced cloud runner. Address/UB checks
# still run when SANITIZE=1; leak checking is explicitly excluded.
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$output/test" "$output"
python3 - "$output" <<'PY'
from pathlib import Path
from PIL import Image
import sys
files = list(Path(sys.argv[1]).glob('*-*.png'))
assert files
for file in files:
    with Image.open(file) as image:
        image.load()
        assert image.size in ((120, 180), (240, 120))
print(f'{len(files)} extracted cover/poster image payloads decoded successfully (Pillow, not Win32 rendering)')
PY
