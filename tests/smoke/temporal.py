"""GPU temporal-stability oracle: hysteresis must calm noise without drifting."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--report', action='store_true', help='Print measurements only')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
W, H, CW, CH, FRAMES = 192, 120, 6, 10, 10


def frame(noise_seed, moved=False):
    """A wide gradient keeps many cells near tone boundaries; the outlined
    box gives contours and silhouettes. Noisy frames add +/-6 levels of pixel
    noise and a global brightness flicker of up to +/-8 levels, like animated
    lighting, which moves whole regions across tone boundaries."""
    seed = noise_seed * 2654435761 % 2**31
    left = 100 if moved else 70
    lift = 70 if moved else 0
    flicker = ((noise_seed * 7) % 5 - 2) * 4 if noise_seed else 0
    data = bytearray()
    for y in range(H):
        for x in range(W):
            value = 40 + 140 * x / (W - 1) + lift
            rgb = (value * .9, value, value * .8 + 20)
            if left <= x < left + 60 and 30 <= y < 90:
                edge = min(x - left, left + 59 - x, y - 30, 89 - y)
                rgb = (20, 20, 30) if edge < 3 else (200, 120, 50)
            for channel in rgb:
                seed = (seed * 1103515245 + 12345) % 2**31
                noise = (seed >> 16) % 13 - 6 if noise_seed else 0
                data.append(max(0, min(255, round(channel) + noise + flicker)))
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


def render(name, stability, inputs):
    config = args.output / f'{name}-{stability}.conf'
    config.write_text(
        'render_style=illustrated\ncolor_mode=source\nglyph_strength=1\n'
        f'cell_width={CW}\ncell_height={CH}\ncharacter_set=custom\n'
        'characters=" .:-=+*#%@"\nshape_matching=true\nshape_threshold=0.3\n'
        'shape_characters="/\\\\|_()<>^v"\nedge_threshold=0.3\n'
        f'stability={stability}\n', encoding='utf-8')
    outputs = [args.output / f'{name}-{stability}-{k}.ppm' for k in range(len(inputs))]
    command = [str(args.build / 'glyphshift-preview'), str(inputs[0]), str(config), str(outputs[0])]
    for source, output in zip(inputs[1:], outputs[1:]):
        command += [str(source), str(output)]
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    return [output.read_bytes() for output in outputs]


def churn(images):
    rates = [changed_cells(images[k - 1], images[k]) for k in range(1, len(images))]
    return sum(rates) / len(rates)


clean, noisy = [], []
(args.output / 'moved.ppm').write_bytes(frame(FRAMES + 1, moved=True))
for k in range(FRAMES):
    (args.output / f'clean-{k}.ppm').write_bytes(frame(0))
    (args.output / f'noisy-{k}.ppm').write_bytes(frame(k + 1))
    clean.append(args.output / f'clean-{k}.ppm')
    noisy.append(args.output / f'noisy-{k}.ppm')

static_off, static_on = render('clean', 0, clean), render('clean', 1, clean)
noisy_off, noisy_on = render('noisy', 0, noisy), render('noisy', 1, noisy)
# A real change after a stabilized sequence must not leave stale glyphs.
changed = render('changed', 1, noisy + [args.output / 'moved.ppm'])[-1]
fresh = render('fresh', 0, [args.output / 'moved.ppm'])[0]
measured = {
    'noisy churn, stability 0': churn(noisy_off),
    'noisy churn, stability 1': churn(noisy_on),
    'stale cells after a real change': changed_cells(changed, fresh),
}
if args.report:
    for key, value in measured.items():
        print(f'{key}: {value:.3f}')
    raise SystemExit
# An unchanged frame must reproduce the same decisions: history plumbing may
# not alter a static image, and the first frame never has history.
assert static_on == static_off, 'stability changed a static scene'
assert noisy_on[0] == noisy_off[0], 'the first frame used history'
assert measured['noisy churn, stability 0'] > .05, measured
assert measured['noisy churn, stability 1'] <= .35 * measured['noisy churn, stability 0'], measured
assert measured['stale cells after a real change'] <= .02, measured
print('Temporal stability: static frames unchanged, flicker churn {:.3f} -> {:.3f}, '
      'stale cells after a real change {:.3f}'.format(
          measured['noisy churn, stability 0'], measured['noisy churn, stability 1'],
          measured['stale cells after a real change']))
