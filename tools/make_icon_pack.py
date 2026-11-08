#!/usr/bin/env python3
"""Build a custom button-icon pack from your own 128x256 PNG (same layout as mods/icons/<set>/glyph_sheet.png).
Usage: python tools/make_icon_pack.py my_sheet.png mypack
Creates mods/icons/mypack/{glyph_idx.bin,glyph_clut.bin,glyph_sheet.png}; then set SVR06_ICONSET=mypack in scripts/probe_env.cmd."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np
from PIL import Image
from make_icon_sheets import quant, clut_storage
src, name = sys.argv[1], sys.argv[2]
im = Image.open(src).convert('RGBA')
assert im.size == (128, 256), 'sheet must be exactly 128x256'
idx, clut = quant(im)
out = os.path.join(os.path.dirname(__file__), '..', 'mods', 'icons', name)
os.makedirs(out, exist_ok=True)
open(os.path.join(out, 'glyph_idx.bin'), 'wb').write(idx.astype(np.uint8).tobytes())
open(os.path.join(out, 'glyph_clut.bin'), 'wb').write(clut_storage(clut).tobytes())
im.save(os.path.join(out, 'glyph_sheet.png'))
print('pack written to', out)
