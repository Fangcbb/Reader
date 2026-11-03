from pathlib import Path
import json,sys
from PIL import Image, ImageChops
root=Path(sys.argv[1])
reference=Image.open(root/'ui-theme-light.png').convert('RGB')
x,y,w,h=json.loads((root/'ui-theme-reading-crop.json').read_text(encoding='utf-8-sig'))
assert w>0 and h>0
reading=reference.crop((x,y,x+w,y+h))
for name in ['ui-theme-dark','ui-theme-dark-custom','ui-theme-light-custom','ui-theme-dark-restarted']:
    image=Image.open(root/f'{name}.png').convert('RGB')
    assert image.size == reference.size
    assert not ImageChops.difference(reading,image.crop((x,y,x+w,y+h))).getbbox(), f'{name}: reading colors/pixels changed'
    # Sample the left outer gutter, not paper or custom window-title rendering.
    before=reference.getpixel((12,image.height//2))
    after=image.getpixel((12,image.height//2))
    if name!='ui-theme-light-custom': assert sum(after)<sum(before)/2, f'{name}: shell did not become dark'
    else: assert after!=before, 'Custom light background was not applied'
print('UI theme pixels: shell changed while the reading area remained identical')
