"""Check actual Windows UI captures, not a mock drawing of the layout."""
from pathlib import Path
import sys
from PIL import Image

root = Path(sys.argv[1])
images = {}
for name in ('welcome', 'epub-cover', 'epub-text', 'appearance', 'behavior', 'compact', 'compact-contents', 'focus', 'restored'):
    image = Image.open(root / f'{name}.png').convert('RGB')
    assert image.width > 300 and image.height > 300, f'{name}: capture is too small'
    assert len(image.getcolors(image.width * image.height) or []) > 3, f'{name}: capture is blank'
    images[name] = image
assert images['compact'].size == images['restored'].size, 'Focus mode did not restore window dimensions'
assert images['focus'].width < images['compact'].width, 'Focus mode did not remove the shell'

# The fixture has 24/66 consumed UTF-16 positions at the first text page. Check
# the real progress-bar pixels so percent/fraction errors are caught visually.
image = images['epub-text']
pixels = image.load()
rows = []
for y in range(max(0, image.height - 80), image.height):
    accent = sum(pixels[x, y] == (101, 86, 213) for x in range(image.width))
    track = sum(pixels[x, y] == (223, 225, 235) for x in range(image.width))
    rows.append((accent + track, accent))
length, filled = max(rows)
assert length > 100, 'Progress bar was not captured'
assert 0.30 < filled / length < 0.40, f'Progress bar does not match 36.36%: {filled / length:.2%}'
print('Nine Windows captures, focus geometry and rendered progress-bar proportion passed')
