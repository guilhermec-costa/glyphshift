"""GPU world-anchored grid oracle: glyphs must travel with a scrolling scene."""
import argparse
import math
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--report', action='store_true', help='Print measurements only')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
W, H, CW, CH = 240, 160, 6, 10
# Camera path in pixels per frame (content moves the opposite way): steady
# pans, a diagonal, a reversal and a fast move, all within the 16-pixel search.
STEPS = [(2, 0)] * 6 + [(3, 1)] * 6 + [(-2, 0)] * 4 + [(0, -2)] * 4 + [(7, 0)] * 4


def world(x, y):
    """A scrolling-game stand-in: sky gradient, blocks with outlines and a
    textured ground, aperiodic enough for an unambiguous motion search."""
    if y > 110 + 12 * math.sin(x / 37):
        v = 70 + 40 * math.sin(x / 5.3) * math.sin(y / 3.1) + 30 * math.sin((x + 2 * y) / 23)
        return (v * .8, v * .6, v * .3)
    cell_x, cell_y = x // 40, y // 30
    if (cell_x * 7 + cell_y * 3) % 5 == 0 and y < 100:
        edge = min(x % 40, 39 - x % 40, y % 30, 29 - y % 30)
        return (20, 20, 40) if edge < 2 else (180 + 30 * math.sin(cell_x), 140, 90)
    t = y / 160 + 0.15 * math.sin(x / 61)
    return (90 + 100 * t, 140 + 80 * t, 230)


def frame(camera):
    cx, cy = camera
    data = bytearray()
    for y in range(H):
        for x in range(W):
            data += bytes(max(0, min(255, round(c))) for c in world(x + cx, y + cy))
    return f'P6\n{W} {H}\n255\n'.encode() + bytes(data)


def pixels(image):
    return image.split(b'\n', 3)[3]


def cell_changes(current, previous, shift, margin):
    """Fraction of interior cells (of a 6x10 screen tiling) whose pixels
    differ from the previous frame's at the same scene position."""
    a, b = pixels(current), pixels(previous)
    dx, dy = shift
    changed = total = 0
    for cy in range(margin, H - CH - margin, CH):
        for cx in range(margin, W - CW - margin, CW):
            total += 1
            different = False
            for y in range(cy, cy + CH):
                for x in range(cx, cx + CW):
                    i = (y * W + x) * 3
                    j = ((y - dy) * W + (x - dx)) * 3
                    if a[i:i + 3] != b[j:j + 3]:
                        different = True
                        break
                if different:
                    break
            changed += different
    return changed / total


def render(name, settings, inputs):
    config = args.output / f'{name}.conf'
    config.write_text(
        'render_style=illustrated\ncolor_mode=source\nglyph_strength=1\n'
        f'cell_width={CW}\ncell_height={CH}\ncharacter_set=custom\n'
        'characters=" .:-=+*#%@"\nedge_detector=dog\n' + settings)
    outputs = [args.output / f'{name}-{k}.ppm' for k in range(len(inputs))]
    command = [str(args.build / 'glyphshift-preview'), str(inputs[0]), str(config), str(outputs[0])]
    for source, output in zip(inputs[1:], outputs[1:]):
        command += [str(source), str(output)]
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    return [output.read_bytes() for output in outputs]


camera = [0, 0]
cameras = [tuple(camera)]
for step in STEPS:
    camera = [camera[0] + step[0], camera[1] + step[1]]
    cameras.append(tuple(camera))
inputs = []
for k, position in enumerate(cameras):
    path = args.output / f'scene-{k}.ppm'
    path.write_bytes(frame((position[0] + 200, position[1] + 10)))
    inputs.append(path)
still = [inputs[0]] * 3
fixed = render('fixed', '', inputs)
anchored = render('anchored', 'anchor_grid=true\n', inputs)
# With hysteresis, each world cell's previous decision must be found where
# the grid moved it.
stable = render('anchored-stable', 'anchor_grid=true\nstability=1\n', inputs)
static_fixed = render('static-fixed', '', still)
static_anchored = render('static-anchored', 'anchor_grid=true\n', still)
# Content moves opposite to the camera. Margins exclude cells entering at an
# edge (the largest step) and the neighbor reach of paper wash and glow.
margin = 2 * CW + 7
moves = [(-sx, -sy) for sx, sy in STEPS]
fixed_churn = [cell_changes(fixed[k + 1], fixed[k], moves[k], margin) for k in range(len(STEPS))]
anchored_churn = [cell_changes(anchored[k + 1], anchored[k], moves[k], margin)
                  for k in range(len(STEPS))]
stable_churn = [cell_changes(stable[k + 1], stable[k], moves[k], margin)
                for k in range(len(STEPS))]
measured = {
    'fixed grid, mean changed cells per frame': sum(fixed_churn) / len(fixed_churn),
    'anchored grid, mean changed cells per frame': sum(anchored_churn) / len(anchored_churn),
    'anchored grid, worst frame (first two excluded)': max(anchored_churn[2:]),
    'anchored grid with stability 1, worst frame (first two excluded)': max(stable_churn[2:]),
}
# Lattice aliasing: 4-pixel blocks of hashed gray (the fixture's scroll mode)
# 544 pixels wide, where an unjittered 32-point lattice would be spaced 16
# pixels apart, a multiple of the texture's period. Scrolling one pixel per
# frame with 7-pixel cells, the anchored grid must be off the screen grid
# after 1..6 pixels and back on it after 7.
BW, BH = 544, 64


def blocks(shift):
    data = bytearray()
    for y in range(BH):
        for x in range(BW):
            h = (((x + shift) // 4 * 73856093) & 0xffffffff) ^ ((y // 4 * 19349663) & 0xffffffff)
            h = ((h ^ (h >> 13)) * 1274126177) & 0xffffffff
            data += bytes([round((.15 + .7 * ((h >> 8) & 255) / 255) * 255)] * 3)
    return f'P6\n{BW} {BH}\n255\n'.encode() + bytes(data)


lattice = []
for k in range(8):
    path = args.output / f'blocks-{k}.ppm'
    path.write_bytes(blocks(k))
    lattice.append(path)
block_text = 'cell_width=7\nanchor_grid=true\n'
block_anchored = render('blocks-anchored', block_text, lattice)
block_screen = [render(f'blocks-screen-{k}', 'cell_width=7\n', [lattice[k]])[0] for k in (1, 6, 7)]
measured['block lattice, anchored grid aligned after 1, 6, 7 pixels'] = [
    block_anchored[k] == screen for k, screen in zip((1, 6, 7), block_screen)]
if args.report:
    for key, value in measured.items():
        print(f'{key}: {value}')
    print('per frame, anchored:', ' '.join(f'{v:.3f}' for v in anchored_churn))
    raise SystemExit
assert static_anchored == static_fixed, 'anchoring changed a static scene'
assert anchored[0] == fixed[0], 'the first anchored frame did not start at the screen grid'
assert measured['fixed grid, mean changed cells per frame'] > .2, measured
# The first motion search runs on the second frame; from then on every
# interior cell must reproduce the previous frame at the scene's new place.
assert measured['anchored grid, worst frame (first two excluded)'] == 0, (measured, anchored_churn)
assert measured['anchored grid with stability 1, worst frame (first two excluded)'] == 0, (
    measured, stable_churn)
assert measured['block lattice, anchored grid aligned after 1, 6, 7 pixels'] == [
    False, False, True], measured
print('Anchored grid: {:.3f} of interior cells change per frame on a fixed grid, {:.3f} anchored '
      '(pans, diagonal, reversal and a 7-pixel step), also with stability 1 and on a '
      'block texture that aliases a regular sample lattice'.format(
          measured['fixed grid, mean changed cells per frame'],
          measured['anchored grid, mean changed cells per frame']))
