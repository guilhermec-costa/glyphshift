"""GPU local-contrast oracle: a fogged region keeps its structure, a contrasty
one keeps its look, and the default is 0.5."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--report', action='store_true', help='Print measurements only')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
W, H, CW, CH = 240, 144, 6, 6
SPLIT = 120


def frame():
    """Left half: fog, bright vertical bands only 12 levels apart (the
    temple's columns behind haze). Right half: the same bands from black to
    white."""
    data = bytearray()
    for y in range(H):
        for x in range(W):
            band = (x // 12) % 2
            value = 200 + 12 * band if x < SPLIT else 30 + 190 * band
            data += bytes([value] * 3)
    return f'P6\n{W} {H}\n255\n'.encode() + bytes(data)


def cells(image, x0, x1):
    """Mean brightness of each cell column between x0 and x1, top to bottom
    averaged: one value per cell column."""
    pixels = image.split(b'\n', 3)[3]
    columns = []
    for cx in range(x0, x1, CW):
        total = 0
        for y in range(CH, H - CH):
            row = (y * W + cx) * 3
            total += sum(pixels[row:row + CW * 3:3])
        columns.append(total / (CW * (H - 2 * CH)))
    return columns


def render(name, extra):
    config = args.output / f'{name}.conf'
    config.write_text(
        'render_style=illustrated\ncolor_mode=mono\nglyph_strength=1\n'
        f'cell_width={CW}\ncell_height={CH}\ncharacter_set=custom\n'
        'characters=" .:-=+*#%@"\nedge_strength=0\n' + extra, encoding='utf-8')
    output = args.output / f'{name}.ppm'
    subprocess.run([str(args.build / 'glyphshift-preview'), str(source), str(config), str(output)],
                   check=True, stdout=subprocess.DEVNULL)
    return output.read_bytes()


def band_contrast(columns):
    """Mean difference between neighboring band columns (bands are two cells
    wide): how visible the stripes are."""
    pairs = [(columns[i], columns[i + 2]) for i in range(0, len(columns) - 2, 4)]
    return sum(abs(a - b) for a, b in pairs) / len(pairs)


source = args.output / 'bands.ppm'
source.write_bytes(frame())
plain = render('plain', '')
off = render('off', 'local_contrast=0\n')
half = render('half', 'local_contrast=0.5\n')
measured = {
    'fog bands, off': band_contrast(cells(off, 0, SPLIT)),
    'fog bands, 0.5': band_contrast(cells(half, 0, SPLIT)),
    'contrasty bands, off': band_contrast(cells(off, SPLIT, W)),
    'contrasty bands, 0.5': band_contrast(cells(half, SPLIT, W)),
}
if args.report:
    for key, value in measured.items():
        print(f'{key}: {value:.1f}')
    raise SystemExit
assert half == plain, 'the default is not local_contrast=0.5'
assert measured['fog bands, 0.5'] > 4 * max(measured['fog bands, off'], 1), measured
assert measured['contrasty bands, 0.5'] > .6 * measured['contrasty bands, off'], measured
print('Local contrast: fogged bands {:.1f} -> {:.1f} apart in brightness, contrasty bands '
      '{:.1f} -> {:.1f}; the default equals 0.5'.format(
          measured['fog bands, off'], measured['fog bands, 0.5'],
          measured['contrasty bands, off'], measured['contrasty bands, 0.5']))
