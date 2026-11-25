#!/usr/bin/env python3
"""SvR06 recomp icon (original design): blue-vs-red split with light burst, torn seam, riveted steel plate, chrome "06".
Outputs: svr06_icon_1024.png, svr06_icon_512.png, svr06_icon_256.png, svr06.ico, svr06_banner.png (Quiver/GitHub)."""
import math, random, os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

S = 1024
OUT = sys.argv[1] if len(sys.argv) > 1 else '.'
FONT_BLACK = '/usr/share/fonts/opentype/inter/InterDisplay-Black.otf'
FONT_BOLD = '/usr/share/fonts/opentype/inter/Inter-ExtraBold.otf'
random.seed(6)

def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))

def radial(size, center, inner, outer, radius):
    im = Image.new('RGB', size)
    px = im.load(); cx, cy = center
    for y in range(size[1]):
        for x in range(size[0]):
            t = min(1.0, math.hypot(x - cx, y - cy) / radius)
            px[x, y] = lerp(inner, outer, t ** 0.8)
    return im

def vgrad(w, h, stops):
    im = Image.new('RGBA', (w, h)); d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / max(1, h - 1)
        for i in range(len(stops) - 1):
            if stops[i][0] <= t <= stops[i + 1][0]:
                u = (t - stops[i][0]) / max(1e-6, stops[i + 1][0] - stops[i][0]); c = lerp(stops[i][1], stops[i + 1][1], u); break
        d.line([(0, y), (w, y)], fill=c + (255,))
    return im

# ---- background: blue left, red right, split by a jagged seam -------------------------------------------------
blue = radial((S, S), (S * 0.46, S * 0.40), (70, 150, 255), (4, 10, 40), S * 0.75)
red = radial((S, S), (S * 0.54, S * 0.40), (255, 70, 70), (40, 3, 6), S * 0.75)
seam = []
x = S * 0.5; y = -20
while y < S + 40:
    seam.append((x + random.uniform(-26, 26), y)); y += random.uniform(28, 60)
maskR = Image.new('L', (S, S), 0); ImageDraw.Draw(maskR).polygon(seam + [(S + 10, S + 40), (S + 10, -20)], fill=255)
bg = Image.composite(red, blue, maskR).convert('RGBA')

# light burst (rays from the upper centre)
rays = Image.new('L', (S, S), 0); dr = ImageDraw.Draw(rays); cx, cy = S * 0.5, S * 0.42
for i in range(44):
    a = i / 44 * 2 * math.pi + random.uniform(-0.03, 0.03); w = random.uniform(0.012, 0.035); L = S * random.uniform(0.6, 0.95)
    dr.polygon([(cx, cy), (cx + L * math.cos(a - w), cy + L * math.sin(a - w)), (cx + L * math.cos(a + w), cy + L * math.sin(a + w))], fill=int(random.uniform(40, 110)))
rays = rays.filter(ImageFilter.GaussianBlur(6))
glow = Image.new('L', (S, S), 0); ImageDraw.Draw(glow).ellipse([cx - 230, cy - 230, cx + 230, cy + 230], fill=200)
glow = glow.filter(ImageFilter.GaussianBlur(90))
light = ImageChops.add(rays, glow)
bg = Image.composite(Image.new('RGBA', (S, S), (255, 255, 255, 255)), bg, light.point(lambda v: int(v * 0.55)))

# torn seam: white glowing jagged line
seamL = Image.new('L', (S, S), 0); ImageDraw.Draw(seamL).line(seam, fill=255, width=7, joint='curve')
seamGlow = seamL.filter(ImageFilter.GaussianBlur(14))
bg = Image.composite(Image.new('RGBA', (S, S), (235, 240, 255, 255)), bg, ImageChops.add(seamL, seamGlow.point(lambda v: int(v * 0.9))))

# vignette
vig = Image.new('L', (S, S), 0); ImageDraw.Draw(vig).ellipse([-S * 0.15, -S * 0.15, S * 1.15, S * 1.15], fill=255)
vig = vig.filter(ImageFilter.GaussianBlur(120))
bg = Image.composite(bg, Image.new('RGBA', (S, S), (0, 0, 0, 255)), vig.point(lambda v: int(70 + v * 185 / 255)))

# ---- riveted steel plate (slanted bar across the middle) -------------------------------------------------------
def plate_poly(cx, cy, w, h, slant):
    return [(cx - w / 2 + slant, cy - h / 2), (cx + w / 2 + slant, cy - h / 2), (cx + w / 2 - slant, cy + h / 2), (cx - w / 2 - slant, cy + h / 2)]
PY = 0.57
P = plate_poly(S / 2, S * PY, S * 0.86, S * 0.31, 32)
shadow = Image.new('L', (S, S), 0); ImageDraw.Draw(shadow).polygon([(x + 10, y + 22) for x, y in P], fill=210)
bg = Image.composite(Image.new('RGBA', (S, S), (0, 0, 0, 255)), bg, shadow.filter(ImageFilter.GaussianBlur(22)))
steel = vgrad(S, S, [(0.0, (60, 64, 72)), (0.36, (150, 156, 166)), (0.50, (225, 229, 236)), (0.56, (120, 126, 136)), (0.66, (70, 74, 82)), (1.0, (40, 42, 48))])
# brushed noise
noise = Image.effect_noise((S, S), 22).filter(ImageFilter.GaussianBlur(0.6)).resize((S, S))
noise = noise.transform((S, S), Image.AFFINE, (1, 0, 0, 0, 0.08, 0), resample=Image.BILINEAR)
steel = Image.blend(steel, Image.merge('RGBA', (noise, noise, noise, Image.new('L', (S, S), 255))), 0.10)
pm = Image.new('L', (S, S), 0); ImageDraw.Draw(pm).polygon(P, fill=255)
bg = Image.composite(steel, bg, pm)
d = ImageDraw.Draw(bg)
d.polygon(P, outline=(18, 18, 22, 255), width=10)
inner = plate_poly(S / 2, S * PY, S * 0.86 - 30, S * 0.31 - 30, 29)
d.line(inner + [inner[0]], fill=(235, 238, 245, 160), width=3)
# coloured edge strips: blue left half, red right half (the split carried onto the plate)
for (x0, col) in ((P[0][0] - 10, (40, 110, 255, 255)), (P[1][0] - 70, (230, 30, 40, 255))):
    pass
for rx, ry in [(P[0][0] + 26, P[0][1] + 24), (P[1][0] - 30, P[1][1] + 24), (P[3][0] + 34, P[3][1] - 26), (P[2][0] - 26, P[2][1] - 26)]:
    d.ellipse([rx - 15, ry - 15, rx + 15, ry + 15], fill=(30, 32, 38, 255))
    d.ellipse([rx - 12, ry - 13, rx + 10, ry + 9], fill=(205, 210, 220, 255))
    d.ellipse([rx - 6, ry - 8, rx + 1, ry - 1], fill=(255, 255, 255, 255))


# ---- chrome "06" -------------------------------------------------------------------------------------------------
def chrome_text(text, font, fill_stops, outline=14, skew=0.18, ocol=(12, 12, 16)):
    f = ImageFont.truetype(font[0], font[1])
    bbox = f.getbbox(text); w = bbox[2] - bbox[0] + outline * 4; h = bbox[3] - bbox[1] + outline * 4
    m = Image.new('L', (w, h), 0); ImageDraw.Draw(m).text((outline * 2 - bbox[0], outline * 2 - bbox[1]), text, font=f, fill=255)
    out = m.filter(ImageFilter.MaxFilter(outline * 2 + 1))
    grad = vgrad(w, h, fill_stops)
    layer = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    layer = Image.composite(Image.new('RGBA', (w, h), ocol + (255,)), layer, out)
    layer = Image.composite(grad, layer, m)
    # bevel highlight: top inner edge
    hi = ImageChops.subtract(m, m.transform((w, h), Image.AFFINE, (1, 0, 0, 0, 1, 5))).filter(ImageFilter.GaussianBlur(1.5))
    layer = Image.composite(Image.new('RGBA', (w, h), (255, 255, 255, 255)), layer, hi.point(lambda v: int(v * 0.8)))
    # italic skew
    W2 = int(w + h * skew)
    layer = layer.transform((W2, h), Image.AFFINE, (1, skew, -h * skew, 0, 1, 0), resample=Image.BICUBIC)
    return layer
chrome = [(0.0, (255, 255, 255)), (0.42, (200, 206, 216)), (0.50, (70, 76, 88)), (0.58, (150, 156, 168)), (0.80, (240, 243, 250)), (1.0, (170, 176, 188))]
t06 = chrome_text('06', (FONT_BLACK, 400), chrome, outline=13)
# shadow under the numerals
sh = t06.split()[3].filter(ImageFilter.GaussianBlur(10))
ox = (S - t06.width) // 2 + 6; oy = int(S * PY - t06.height / 2) - 6
bg.paste(Image.new('RGBA', t06.size, (0, 0, 0, 230)), (ox + 10, oy + 16), sh)
bg.alpha_composite(t06, (ox, oy))

# ---- "SvR" lettering above the plate ------------------------------------------------------------------------------
def letter(ch, size, stops, outline=10):
    return chrome_text(ch, (FONT_BLACK, size), stops, outline=outline, skew=0.18, ocol=(215, 220, 230))
blueS = [(0.0, (190, 225, 255)), (0.45, (70, 150, 255)), (0.55, (20, 60, 170)), (1.0, (90, 160, 255))]
redS = [(0.0, (255, 200, 200)), (0.45, (255, 70, 70)), (0.55, (150, 10, 20)), (1.0, (255, 90, 90))]
whiteS = [(0.0, (255, 255, 255)), (0.5, (205, 210, 220)), (1.0, (255, 255, 255))]
blackS = [(0.0, (95, 98, 108)), (0.42, (28, 30, 36)), (0.52, (6, 6, 8)), (0.75, (22, 24, 28)), (1.0, (55, 58, 66))]
Ls = [letter('S', 280, blackS), letter('v', 200, blackS, 9), letter('R', 280, blackS)]
gap = -52; total = sum(l.width for l in Ls) + gap * 2
x0 = (S - total) // 2; base = int(S * 0.37)
for l in Ls:
    y = base - l.height
    sh2 = l.split()[3].filter(ImageFilter.GaussianBlur(9))
    bg.paste(Image.new('RGBA', l.size, (0, 0, 0, 200)), (x0 + 8, y + 12), sh2)
    bg.alpha_composite(l, (x0, y)); x0 += l.width + gap


# ---- RECOMP tag below the plate -------------------------------------------------------------------------------
tag_f = ImageFont.truetype(FONT_BOLD, 78)
txt = 'RECOMP'; tb = tag_f.getbbox(txt); tw = tb[2] - tb[0] + 120; th = 116
tag = Image.new('RGBA', (tw, th), (0, 0, 0, 0)); td = ImageDraw.Draw(tag)
td.polygon([(18, 0), (tw, 0), (tw - 18, th), (0, th)], fill=(10, 12, 18, 235), outline=(210, 214, 222, 255), width=5)
td.text(((tw - (tb[2] - tb[0])) / 2 - tb[0], (th - (tb[3] - tb[1])) / 2 - tb[1]), txt, font=tag_f, fill=(240, 242, 248, 255))
if not os.environ.get('NOTAG'): bg.alpha_composite(tag, ((S - tw) // 2, int(S * 0.785)))

# ---- squircle mask + rim ------------------------------------------------------------------------------------------
R = int(S * 0.22)
mask = Image.new('L', (S * 4, S * 4), 0); ImageDraw.Draw(mask).rounded_rectangle([0, 0, S * 4 - 1, S * 4 - 1], radius=R * 4, fill=255)
mask = mask.resize((S, S), Image.LANCZOS)
rim = Image.new('RGBA', (S, S), (0, 0, 0, 0)); ImageDraw.Draw(rim).rounded_rectangle([3, 3, S - 4, S - 4], radius=R - 3, outline=(255, 255, 255, 70), width=6)
bg.alpha_composite(rim)
icon = Image.new('RGBA', (S, S), (0, 0, 0, 0)); icon.paste(bg, (0, 0), mask)

os.makedirs(OUT, exist_ok=True)
if os.environ.get('NOTAG'):
    icon.save(os.path.join(OUT, 'svr06_icon_small_1024.png')); print('ok small'); sys.exit(0)
icon.save(os.path.join(OUT, 'svr06_icon_1024.png'))
for s in (512, 256):
    icon.resize((s, s), Image.LANCZOS).save(os.path.join(OUT, 'svr06_icon_%d.png' % s))
small = Image.open(os.path.join(OUT, 'svr06_icon_small_1024.png'))
frames = [(small if s <= 48 else icon).resize((s, s), Image.LANCZOS) for s in (256, 128, 64, 48, 32, 24, 16)]
frames[0].save(os.path.join(OUT, 'svr06.ico'), format='ICO', sizes=[f.size for f in frames], append_images=frames[1:])
# small-size preview sheet (how it reads in a launcher list)
prev = Image.new('RGB', (760, 300), (28, 28, 32)); x = 20
for s in (256, 128, 64, 48, 32, 16):
    src = small if s <= 48 else icon; t = src.resize((s, s), Image.LANCZOS)
    prev.paste(t, (x, (300 - s) // 2), t); x += s + 24
prev.save(os.path.join(OUT, 'svr06_icon_preview.png'))
print('ok')
