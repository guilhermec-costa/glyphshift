"""GPU automatic-exposure oracle: metering, the exposure response and adaptation."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--report', action='store_true', help='Print measurements only')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
W, H, CW, CH = 192, 120, 6, 10


def frame(scale):
    """A diagonal gradient with an outlined box, every level multiplied by
    `scale`: 0.5 is the same scene at half the brightness."""
    data = bytearray()
    for y in range(H):
        for x in range(W):
            value = 60 + 160 * (x + y) / (W + H - 2)
            if 70 <= x < 130 and 30 <= y < 90:
                edge = min(x - 70, 129 - x, y - 30, 89 - y)
                value = 30 if edge < 3 else 200
            data += bytes([round(value * scale)] * 3)
    return f'P6\n{W} {H}\n255\n'.encode() + bytes(data)


def changed_cells(a, b):
    """Fraction of cells whose rendered glyph visibly changed."""
    a, b = a.split(b'\n', 3)[3], b.split(b'\n', 3)[3]
    count = total = 0
    for cy in range(0, H, CH):
        for cx in range(0, W, CW):
            total += 1
            for y in range(cy, min(cy + CH, H)):
                i, j = (y * W + cx) * 3, (y * W + min(cx + CW, W)) * 3
                if any(abs(p - q) > 24 for p, q in zip(a[i:j], b[i:j])):
                    count += 1
                    break
    return count / total


def render(name, auto, inputs):
    config = args.output / f'{name}.conf'
    config.write_text(
        'render_style=illustrated\ncolor_mode=mono\nglyph_strength=1\n'
        f'cell_width={CW}\ncell_height={CH}\ncharacter_set=custom\n'
        'characters=" .:-=+*#%@"\nedge_strength=0\n' +
        (f'auto_exposure={auto}\n' if auto is not None else ''), encoding='utf-8')
    outputs = [args.output / f'{name}-{k}.ppm' for k in range(len(inputs))]
    command = [str(args.build / 'glyphshift-preview'), str(inputs[0]), str(config), str(outputs[0])]
    for source, output in zip(inputs[1:], outputs[1:]):
        command += [str(source), str(output)]
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    return [output.read_bytes() for output in outputs]


bright, dark = args.output / 'bright.ppm', args.output / 'dark.ppm'
bright.write_bytes(frame(1))
dark.write_bytes(frame(.5))
plain = render('plain', None, [bright])[0]
off = render('off', 0, [bright])[0]
bright_fixed, dark_fixed = render('bright-fixed', 0, [bright])[0], render('dark-fixed', 0, [dark])[0]
bright_auto, dark_auto = render('bright-auto', 1, [bright])[0], render('dark-auto', 1, [dark])[0]
# Two seconds at 60 fps after a cut from the dark scene: the exposure eases
# toward the new scene instead of jumping (time constant 0.35 s).
cut = render('cut', 1, [dark] * 30 + [bright] * 121)
measured = {
    'dark vs bright, fixed exposure': changed_cells(dark_fixed, bright_fixed),
    'dark vs bright, auto exposure': changed_cells(dark_auto, bright_auto),
    'first frame after the cut vs adapted': changed_cells(cut[30], bright_auto),
    'two seconds after the cut vs adapted': changed_cells(cut[-1], bright_auto),
}
if args.report:
    for key, value in measured.items():
        print(f'{key}: {value:.3f}')
    raise SystemExit
assert off == plain, 'auto_exposure=0 changed the image'
assert cut[0] == dark_auto, 'the first metered frame did not start from its own measurement'
assert cut[29] == dark_auto, 'a static scene drifted under automatic exposure'
assert measured['dark vs bright, fixed exposure'] > .3, measured
assert measured['dark vs bright, auto exposure'] <= .05, measured
assert measured['first frame after the cut vs adapted'] > .1, measured
assert measured['two seconds after the cut vs adapted'] <= .02, measured
print('Automatic exposure: half-brightness scene matches the original ({:.3f} of cells differ, '
      '{:.3f} with fixed exposure); after a cut {:.3f} -> {:.3f} within two seconds'.format(
          measured['dark vs bright, auto exposure'], measured['dark vs bright, fixed exposure'],
          measured['first frame after the cut vs adapted'],
          measured['two seconds after the cut vs adapted']))
