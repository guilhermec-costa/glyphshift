"""Capture overlay smoke: an independently started window is transformed on top."""
import argparse
import ctypes as c
import ctypes.util as util
import os
import pathlib
import subprocess
import sys
import time
from x11 import X11

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=pathlib.Path, required=True)
parser.add_argument('--output', type=pathlib.Path, required=True)
args = parser.parse_args()
build = args.build.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
if not os.getenv('DISPLAY'):
    print('SKIP: DISPLAY is not set')
    sys.exit(77)
x = X11()
env = os.environ.copy()
for name in ['LD_PRELOAD', 'GLYPHSHIFT_CONFIG', 'GLYPHSHIFT_LOG']:
    env.pop(name, None)
suffix = f' overlay-{os.getpid()}'
env.update(GLYPHSHIFT_FIXTURE_TITLE_SUFFIX=suffix, GLYPHSHIFT_FIXTURE_NO_FOCUS='1',
           GLYPHSHIFT_FIXTURE_FLOATING='1')
xext = c.CDLL(util.find_library('Xext'))
xext.XShapeGetRectangles.restype = c.c_void_p
xext.XShapeGetRectangles.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.POINTER(c.c_int),
                                     c.POINTER(c.c_int)]


def wait_window(title, process, log):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        found = x.windows(title)
        if found:
            return found[0]
        if process.poll() is not None:
            raise RuntimeError(log.read_text())
        time.sleep(.05)
    raise RuntimeError('window did not appear: ' + title)


def save(label, picture):
    w, h, rgb = picture
    path = output / f'{label}.ppm'
    path.write_bytes(f'P6\n{w} {h}\n255\n'.encode() + rgb)
    return path


def difference(a, b):
    worst = max(abs(p - q) for p, q in zip(a, b))
    close = sum(1 for p, q in zip(a, b) if abs(p - q) <= 2) / len(a)
    return worst, close


fixture_log = output / 'fixture.log'
overlay_log = output / 'overlay.log'
# The fixture is started directly: no launcher, no preload, no Vulkan layer.
with fixture_log.open('w') as stream:
    fixture = subprocess.Popen([str(build / 'glyphshift-glx-fixture'), '--fixed-time', '1.25',
                                '--frames', '9000'], env=env, stdout=stream,
                               stderr=subprocess.STDOUT)
overlay = None
try:
    window = wait_window(f'GlyphShift GLX fixture{suffix}', fixture, fixture_log)
    x.resize(window, 640, 400)
    original = x.image(window)
    source = save('original', original)
    expected_path = output / 'expected.ppm'
    subprocess.run([str(build / 'glyphshift-preview'), str(source),
                    str(pathlib.Path(__file__).resolve().parents[2] / 'presets' /
                        'art-newsprint.conf'), str(expected_path)],
                   check=True, stdout=subprocess.DEVNULL)
    expected = expected_path.read_bytes().split(b'\n', 3)[3]
    with overlay_log.open('w') as stream:
        overlay = subprocess.Popen([str(build / 'glyphshift-overlay'), '--window', str(window),
                                    '--preset', 'art-newsprint'], env=env, stdout=stream,
                                   stderr=subprocess.STDOUT)
    layer = wait_window('GlyphShift overlay', overlay, overlay_log)
    time.sleep(1)
    # The overlay's own pixels equal the preview of the same source frame.
    w, h, rendered = x.image(layer)
    save('overlay', (w, h, rendered))
    assert (w, h) == original[:2], ((w, h), original[:2])
    worst, close = difference(rendered, expected)
    assert close >= .999 and worst <= 8, (worst, close)
    # On screen, the fixture's region shows the overlay, not the original.
    root = x.root
    x.lib.XTranslateCoordinates.argtypes = [c.c_void_p, c.c_ulong, c.c_ulong, c.c_int, c.c_int,
                                           c.POINTER(c.c_int), c.POINTER(c.c_int),
                                           c.POINTER(c.c_ulong)]
    rx, ry, child = c.c_int(), c.c_int(), c.c_ulong()
    x.lib.XTranslateCoordinates(x.display, window, root, 0, 0, c.byref(rx), c.byref(ry),
                                c.byref(child))
    from x11 import Image
    image = x.lib.XGetImage(x.display, root, rx.value, ry.value, w, h, c.c_ulong(-1).value, 2)
    assert image, 'cannot read the screen'
    info = image.contents
    raw = c.string_at(info.data, info.bytes_per_line * h)
    screen = bytearray(w * h * 3)
    for row in range(h):
        pixels = raw[row * info.bytes_per_line:row * info.bytes_per_line + w * 4]
        at = row * w * 3
        screen[at:at + w * 3:3] = pixels[2::4]
        screen[at + 1:at + w * 3:3] = pixels[1::4]
        screen[at + 2:at + w * 3:3] = pixels[0::4]
    x.lib.XDestroyImage(image)
    save('screen', (w, h, bytes(screen)))
    _, on_screen = difference(bytes(screen), rendered)
    _, still_original = difference(bytes(screen), original[2])
    assert on_screen >= .99 and still_original < .9, (on_screen, still_original)
    # Input passes through: the overlay's input shape has no rectangles.
    count, ordering = c.c_int(-1), c.c_int()
    rectangles = xext.XShapeGetRectangles(x.display, layer, 2, c.byref(count), c.byref(ordering))
    if rectangles:
        x.lib.XFree(c.c_void_p(rectangles))
    assert count.value == 0, count.value
    # Closing the target ends the overlay cleanly.
    x.close(window)
    assert fixture.wait(timeout=15) == 0, fixture_log.read_text()
    assert overlay.wait(timeout=15) == 0, overlay_log.read_text()
    print('Capture overlay: independent fixture transformed on screen, output equals the '
          'preview, input shape empty, clean exit with the target')
finally:
    for process in [overlay, fixture]:
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
    x.shutdown()
