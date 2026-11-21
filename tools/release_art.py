"""Makes a kit's page images from its portrait and skill icons, in the look Vector's pages use.

    python tools/release_art.py Vector portrait.png s1.png s2.png s3.png passive.png --out ../Releases/nexus
    python tools/release_art.py Vector portrait.png s1.png s2.png s3.png --accent 255,138,36 --tagline "Girls' Frontline 2 kit for HellForge"

Writes <name>-thumb.png (1280x720: the page thumbnail and feature image) and <name>-art.jpg (900x1200: a gallery
image). The portrait is a PNG with a transparent ground; the icons are the HUD's (any size). Needs Pillow.
Use official art instead when Yui has it (Koleda's, Ascalon's and Exusiai's thumbnails are official art).
"""
import argparse, os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

BOLD = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
REGULAR = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'

def font(path, size):
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        return ImageFont.load_default()

def glow(size, box, colour, blur, amount, ground):
    g = Image.new('RGB', size, (0, 0, 0))
    ImageDraw.Draw(g).ellipse(box, fill=colour)
    g = g.filter(ImageFilter.GaussianBlur(blur))
    return Image.blend(ground, Image.composite(g, ground, g.convert('L')), amount)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('name')
    ap.add_argument('portrait')
    ap.add_argument('icons', nargs='*')
    ap.add_argument('--out', default='.')
    ap.add_argument('--accent', default='255,138,36', help='r,g,b of the bar, the subtitle and the icon frames')
    ap.add_argument('--tagline', default="Girls' Frontline 2 kit for HellForge")
    a = ap.parse_args()
    accent = tuple(int(v) for v in a.accent.split(','))
    dim = tuple(v * 3 // 5 for v in accent)
    slug = a.name.lower().replace(' ', '-')

    W, H = 1280, 720
    bg = Image.new('RGB', (W, H), (18, 18, 22))
    d = ImageDraw.Draw(bg)
    for y in range(H):
        t = y / H
        d.line([(0, y), (W, y)], fill=(int(18 + 22 * t), int(18 + 8 * t), int(22 + 2 * t)))
    bg = glow((W, H), [640, 60, 1240, 760], dim, 120, 0.55, bg)
    d = ImageDraw.Draw(bg)
    d.rectangle([0, 0, 10, H], fill=accent)
    p = Image.open(a.portrait).convert('RGBA')
    s = 700 / p.height
    p = p.resize((int(p.width * s), 700), Image.LANCZOS)
    bg.paste(p, (700 + max(0, (520 - p.width) // 2), 20), p)
    title = a.name.upper()
    size = 132
    while size > 60 and d.textlength(title, font=font(BOLD, size)) > 600:
        size -= 6
    d.text((70, 150), title, font=font(BOLD, size), fill=(245, 245, 245))
    d.rectangle([74, 312, 574, 318], fill=accent)
    d.text((74, 338), 'Playable skillset', font=font(BOLD, 44), fill=accent)
    d.text((74, 400), a.tagline, font=font(REGULAR, 30), fill=(200, 200, 205))
    x = 74
    for path in a.icons[:4]:
        ic = Image.open(path).convert('RGBA').resize((112, 112), Image.LANCZOS)
        d.rectangle([x, 490, x + 124, 614], fill=(30, 30, 34), outline=accent, width=2)
        bg.paste(ic, (x + 6, 496), ic)
        x += 142
    os.makedirs(a.out, exist_ok=True)
    bg.save(os.path.join(a.out, f'{slug}-thumb.png'))

    art = glow((900, 1200), [100, 200, 800, 1100], dim, 140, 0.5, Image.new('RGB', (900, 1200), (20, 18, 20)))
    p2 = Image.open(a.portrait).convert('RGBA')
    s2 = min(860 / p2.width, 1140 / p2.height)
    p2 = p2.resize((int(p2.width * s2), int(p2.height * s2)), Image.LANCZOS)
    art.paste(p2, ((900 - p2.width) // 2, 1200 - p2.height - 30), p2)
    art.save(os.path.join(a.out, f'{slug}-art.jpg'), quality=92)
    print(f'{slug}-thumb.png, {slug}-art.jpg in {os.path.abspath(a.out)}')

if __name__ == '__main__':
    main()
