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
        config.write_text(f'{palette}render_style=illustrated\ncolor_mode={mode}\nglyph_strength=1\ncell_width={cw}\ncell_height={ch}\ncharacter_set=custom\ncharacters="{glyph}"\nedge_strength=0\n', encoding='utf-8')
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
    config.write_text(f'render_style=illustrated\ncolor_mode=mono\nglyph_strength=1\ncell_width={cw}\ncell_height={ch}\ncharacter_set=custom\ncharacters="█"\nforeground={ink},{ink},{ink}\nbackground={1-ink},{1-ink},{1-ink}\n', encoding='utf-8')
    output = args.output / (name + '.ppm')
    subprocess.run([str(args.build / 'glyphshift-preview'), str(source), str(config), str(output)], check=True, stdout=subprocess.DEVNULL)
    rgb = output.read_bytes().split(b'\n', 3)[3]
    for y in range(h):
        for x in range(w):
            # The cell before the boundary owns the contour. The four-texel
            # atlas bar covers texels 6..9 of 16: pixels 3 and 4 of the 8-pixel
            # cell, fully, in the contrasting paper color.
            expected = (1 - ink) * 255 if x in [35, 36] else ink * 255
            assert all(abs(c - expected) <= 1 for c in rgb[(y*w+x)*3:(y*w+x+1)*3]), (name, x, y)
print('2 adaptive contour-over-fill GPU cases passed without cell-sized holes')
# Crisp glyphs: every pixel must be exactly ink or paper, decided by the
# analytic rectangle coverage against the 40% threshold.
for cw, ch in [(3, 5), (8, 12), (13, 19), (64, 96)]:
    w, h = cw * 2, ch * 2
    name = f'crisp-{cw}-{ch}'
    source = args.output / (name + '-source.ppm')
    source.write_bytes(f'P6\n{w} {h}\n255\n'.encode() + bytes([128]) * w * h * 3)
    config = args.output / (name + '.conf')
    config.write_text('render_style=illustrated\ncolor_mode=mono\nglyph_strength=1\n'
        f'cell_width={cw}\ncell_height={ch}\ncharacter_set=custom\ncharacters="▘"\n'
        'edge_strength=0\ncrisp_glyphs=true\nforeground=1,1,1\nbackground=0,0,0\n', encoding='utf-8')
    output = args.output / (name + '.ppm')
    subprocess.run([str(args.build / 'glyphshift-preview'), str(source), str(config), str(output)], check=True, stdout=subprocess.DEVNULL)
    rgb = output.read_bytes().split(b'\n', 3)[3]
    for y in range(h):
        for x in range(w):
            x0, x1 = (x % cw) / cw, (x % cw + 1) / cw
            y0, y1 = (y % ch) / ch, (y % ch + 1) / ch
            area = max(0, min(x1, .5) - x0) / (x1 - x0) * max(0, min(y1, .5) - y0) / (y1 - y0)
            pixel = rgb[(y * w + x) * 3:(y * w + x + 1) * 3]
            assert pixel in (b'\xff\xff\xff', b'\x00\x00\x00'), (name, x, y, pixel)
            if abs(area - .4) > .01:
                assert pixel == (b'\xff\xff\xff' if area >= .4 else b'\x00\x00\x00'), (name, x, y, area)
print('4 crisp-glyph GPU cases passed: binary pixels matching analytic coverage')
# Difference-of-Gaussians contours: render contours only (no fill), white on
# black, so every lit pixel is a contour stroke.
def dog_case(name, pixel):
    w, h, cw, ch = 64, 48, 8, 12
    source = args.output / (name + '-source.ppm')
    source.write_bytes(f'P6\n{w} {h}\n255\n'.encode() +
                       b''.join(bytes([pixel(x, y)]) * 3 for y in range(h) for x in range(w)))
    config = args.output / (name + '.conf')
    config.write_text('render_style=illustrated\ncolor_mode=mono\nglyph_strength=1\n'
        f'cell_width={cw}\ncell_height={ch}\nfill_enabled=false\ncontour_overlay=false\n'
        'edge_detector=dog\nforeground=1,1,1\nbackground=0,0,0\n', encoding='utf-8')
    output = args.output / (name + '.ppm')
    subprocess.run([str(args.build / 'glyphshift-preview'), str(source), str(config), str(output)], check=True, stdout=subprocess.DEVNULL)
    rgb = output.read_bytes().split(b'\n', 3)[3]
    return lambda x, y: rgb[(y * w + x) * 3]
# A smooth ramp is not an outline.
lit = dog_case('dog-gradient', lambda x, y: 40 + 3 * x)
assert not any(lit(x, y) for y in range(48) for x in range(64)), 'DoG drew a contour on a gradient'
# A one-pixel dark line through column 28 (cell 3, pixel 4) on a bright field
# becomes a vertical stroke in that cell, through every row.
lit = dog_case('dog-thin-line', lambda x, y: 30 if x == 28 else 220)
assert all(lit(28, y) for y in range(12, 36)), 'DoG missed a one-pixel outline'
assert not any(lit(x, y) for y in range(48) for x in list(range(0, 16)) + list(range(40, 64))), \
    'DoG drew contours far from the line'
# Fine checkerboard texture has no coherent direction.
lit = dog_case('dog-checker', lambda x, y: 220 if (x // 2 + y // 2) % 2 else 30)
assert not any(lit(x, y) for y in range(12, 36) for x in range(8, 56)), 'DoG outlined texture'
print('3 DoG contour cases passed: gradient ignored, thin outline found, texture rejected')
# Structural matching must recover every mixed quadrant silhouette, including
# dark objects on bright paper, with both source colors and reversed mono ink.
for mode in ['source', 'mono']:
    cw, ch, w, h = 8, 12, 128, 24
    name = f'shape-matching-{mode}'
    source = args.output / (name + '-source.ppm')
    low, high = (24, 48, 72), (216, 192, 168)
    pixels = bytearray()
    for y in range(h):
        for x in range(w):
            pattern = x // cw
            q = (2 if y % ch >= ch // 2 else 0) + (1 if x % cw >= cw // 2 else 0)
            pixels.extend(high if pattern & (1 << q) else low)
    source.write_bytes(f'P6\n{w} {h}\n255\n'.encode() + pixels)
    config = args.output / (name + '.conf')
    config.write_text(f'render_style=illustrated\ncolor_mode={mode}\nglyph_strength=1\n'
        'cell_width=8\ncell_height=12\ncharacter_set=custom\n'
        'characters=" █▘▝▀▖▌▞▛▗▚▐▜▄▙▟"\nshape_matching=true\n'
        'shape_threshold=0.2\nscene_paper=1\n'
        'foreground=0,0,0\nbackground=1,1,1\nedge_threshold=0.01\n', encoding='utf-8')
    output = args.output / (name + '.ppm')
    subprocess.run([str(args.build / 'glyphshift-preview'), str(source), str(config), str(output)], check=True, stdout=subprocess.DEVNULL)
    rgb = output.read_bytes().split(b'\n', 3)[3]
    for y in range(h):
        for x in range(cw, 15*cw):
            i = (y*w+x)*3
            q = (2 if y % ch >= ch // 2 else 0) + (1 if x % cw >= cw // 2 else 0)
            lit = (x // cw) & (1 << q)
            expected = pixels[i:i+3] if mode == 'source' else bytes([255 if lit else 0])*3
            assert all(abs(a-b) <= 1 for a,b in zip(rgb[i:i+3], expected)), (mode,x,y)
    # Strength zero must still bypass the entire structural renderer exactly.
    config.write_text(config.read_text(encoding='utf-8').replace('glyph_strength=1','glyph_strength=0').replace('color_mode=mono','color_mode=source'), encoding='utf-8')
    subprocess.run([str(args.build / 'glyphshift-preview'), str(source), str(config), str(output)], check=True, stdout=subprocess.DEVNULL)
    assert output.read_bytes().split(b'\n',3)[3] == pixels
print('28 GPU silhouette cases passed (14 masks x source/reversed mono), plus exact strength-zero bypass')
