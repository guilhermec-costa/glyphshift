"""GPU coverage oracle: analytic Unicode blocks must retain area at every scale."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
for glyph in ['▌', '▘']:
    for cw, ch in [(2, 2), (3, 5), (4, 4), (8, 12), (13, 19), (64, 96), (128, 128)]:
        w, h = cw * 2, ch * 2
        name = f'{ord(glyph):x}-{cw}-{ch}'
        source = args.output / (name + '-source.ppm')
        source.write_bytes(f'P6\n{w} {h}\n255\n'.encode() + bytes([128]) * w * h * 3)
        config = args.output / (name + '.conf')
        mode = 'source' if glyph == '▌' else 'mono'
        palette = '' if glyph == '▌' else 'foreground=0.2,0.4,0.8\nbackground=0.05,0.1,0.15\n'
        config.write_text(f'{palette}render_style=illustrated\ncolor_mode={mode}\nglyph_strength=1\ncell_width={cw}\ncell_height={ch}\ncharacter_set=custom\ncharacters="{glyph}"\nedge_strength=0\n')
        output = args.output / (name + '.ppm')
        subprocess.run([str(args.build / 'glyphshift-preview'), str(source), str(config), str(output)], check=True, stdout=subprocess.DEVNULL)
        rgb = output.read_bytes().split(b'\n', 3)[3]
        worst = 0
        for y in range(h):
            for x in range(w):
                # Integrate a half-width rectangle (and half-height for ▘)
                # in normalized cell space, independently of atlas/shader code.
                x0, x1 = (x % cw) / cw, (x % cw + 1) / cw
                y0, y1 = (y % ch) / ch, (y % ch + 1) / ch
                area_x = max(0, min(x1, .5) - x0) / (x1 - x0)
                area_y = 1 if glyph == '▌' else max(0, min(y1, .5) - y0) / (y1 - y0)
                mask = area_x * area_y
                ink, paper = ((1, 1, 1), (0, 0, 0)) if glyph == '▌' else ((.2, .4, .8), (.05, .1, .15))
                expected = [round((bg + (fg - bg) * mask) * 255) for fg, bg in zip(ink, paper)]
                pixel = rgb[(y * w + x) * 3:(y * w + x + 1) * 3]
                worst = max(worst, *(abs(c - value) for c, value in zip(pixel, expected)))
        assert worst <= 1, (glyph, cw, ch, worst)
print('14 GPU area-filter cases passed (2x2 through 128x128; max error <= 1/255)')
# With a solid fill, a detected contour must change only its stroke footprint,
# not erase the whole cell. Test both light and dark palettes independently.
for ink in [0, 1]:
    w, h, cw, ch = 80, 24, 8, 12
    name = f'contour-overlay-{ink}'
    source = args.output / (name + '-source.ppm')
    source.write_bytes(f'P6\n{w} {h}\n255\n'.encode() + b''.join(bytes([0 if x < 40 else 255]) * 3 for y in range(h) for x in range(w)))
    config = args.output / (name + '.conf')
    config.write_text(f'render_style=illustrated\ncolor_mode=mono\nglyph_strength=1\ncell_width={cw}\ncell_height={ch}\ncharacter_set=custom\ncharacters="█"\nforeground={ink},{ink},{ink}\nbackground={1-ink},{1-ink},{1-ink}\n')
    output = args.output / (name + '.ppm')
    subprocess.run([str(args.build / 'glyphshift-preview'), str(source), str(config), str(output)], check=True, stdout=subprocess.DEVNULL)
    rgb = output.read_bytes().split(b'\n', 3)[3]
    for y in range(h):
        for x in range(w):
            # The cell before the boundary owns the Sobel contour. A two-texel
            # atlas bar integrates to half coverage in its two center pixels.
            expected = 128 if x in [35, 36] else ink * 255
            assert all(abs(c - expected) <= 1 for c in rgb[(y*w+x)*3:(y*w+x+1)*3]), (name, x, y)
print('2 adaptive contour-over-fill GPU cases passed without cell-sized holes')
