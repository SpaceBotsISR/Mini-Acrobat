#!/usr/bin/env python3
"""Regenerates the MAgcs icon from resources/magcs_icon_source.png (square art,
full-bleed background): masks it to the macOS rounded-square shape with the
standard margin, then writes resources/magcs.png (window icon, 512 px) and
resources/MAgcs.icns (bundle icon).

  python3 gcs/tools/icon/make_icon.py        # needs Pillow (pip install pillow)
"""
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.normpath(os.path.join(HERE, '..', '..', 'resources'))
SRC = os.path.join(RES, 'magcs_icon_source.png')

S = 1024            # canvas
INSET = 100         # macOS icon grid: 824 px shape on a 1024 canvas
RADIUS = 185

art = Image.open(SRC).convert('RGBA')
side = min(art.size)
art = art.crop(((art.width - side) // 2, (art.height - side) // 2,
                (art.width + side) // 2, (art.height + side) // 2))
inner = S - 2 * INSET
art = art.resize((inner, inner), Image.LANCZOS)

mask = Image.new('L', (S * 4, S * 4), 0)
ImageDraw.Draw(mask).rounded_rectangle(
    [INSET * 4, INSET * 4, (S - INSET) * 4, (S - INSET) * 4], radius=RADIUS * 4, fill=255)
mask = mask.resize((S, S), Image.LANCZOS)

layer = Image.new('RGBA', (S, S), (0, 0, 0, 0))
layer.paste(art, (INSET, INSET))
icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
icon.paste(layer, (0, 0), mask)

icon.resize((512, 512), Image.LANCZOS).save(os.path.join(RES, 'magcs.png'))
icon.save(os.path.join(RES, 'MAgcs.icns'),
          sizes=[(16, 16), (32, 32), (64, 64), (128, 128), (256, 256), (512, 512), (1024, 1024)])
print('wrote resources/magcs.png and resources/MAgcs.icns')
