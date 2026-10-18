"""Capture overlay smoke: an independently started window is transformed on top."""
import argparse
import ctypes as c
import ctypes.util as util
import os
import pathlib
import signal
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


class WindowAttributes(c.Structure):
    _fields_ = [('x', c.c_int), ('y', c.c_int), ('width', c.c_int), ('height', c.c_int),
                ('border_width', c.c_int), ('depth', c.c_int), ('visual', c.c_void_p),
                ('root', c.c_ulong), ('class_', c.c_int), ('bit_gravity', c.c_int),
                ('win_gravity', c.c_int), ('backing_store', c.c_int),
                ('backing_planes', c.c_ulong), ('backing_pixel', c.c_ulong),
                ('save_under', c.c_int), ('colormap', c.c_ulong), ('map_installed', c.c_int),
                ('map_state', c.c_int), ('all_event_masks', c.c_long),
                ('your_event_mask', c.c_long), ('do_not_propagate_mask', c.c_long),
                ('override_redirect', c.c_int), ('screen', c.c_void_p)]


class SetAttributes(c.Structure):
    _fields_ = [('background_pixmap', c.c_ulong), ('background_pixel', c.c_ulong),
                ('border_pixmap', c.c_ulong), ('border_pixel', c.c_ulong),
                ('bit_gravity', c.c_int), ('win_gravity', c.c_int), ('backing_store', c.c_int),
                ('backing_planes', c.c_ulong), ('backing_pixel', c.c_ulong),
                ('save_under', c.c_int), ('event_mask', c.c_long),
                ('do_not_propagate_mask', c.c_long), ('override_redirect', c.c_int),
                ('colormap', c.c_ulong), ('cursor', c.c_ulong)]


def call(name, result, argtypes, *values):
    function = getattr(x.lib, name)
    function.restype = result
    function.argtypes = argtypes
    return function(*values)


def plain_window(x0, y0, w, h, color):
    """An unmanaged window: no window manager reacts to moves, maps or stacking."""
    attributes = SetAttributes(background_pixel=color, override_redirect=1)
    window = call('XCreateWindow', c.c_ulong,
                  [c.c_void_p, c.c_ulong, c.c_int, c.c_int, c.c_uint, c.c_uint, c.c_uint, c.c_int,
                   c.c_uint, c.c_void_p, c.c_ulong, c.POINTER(SetAttributes)],
                  x.display, x.root, x0, y0, w, h, 0, 0, 1, None, (1 << 1) | (1 << 9),
                  c.byref(attributes))
    call('XStoreName', c.c_int, [c.c_void_p, c.c_ulong, c.c_char_p], x.display, window,
         f'GlyphShift overlay target{suffix}'.encode())
    call('XMapRaised', c.c_int, [c.c_void_p, c.c_ulong], x.display, window)
    x.lib.XSync(x.display, False)
    return window


def attributes(window):
    info = WindowAttributes()
    ok = call('XGetWindowAttributes', c.c_int,
              [c.c_void_p, c.c_ulong, c.POINTER(WindowAttributes)], x.display, window,
              c.byref(info))
    return info if ok else None


def stacking():
    root, parent = c.c_ulong(), c.c_ulong()
    children, count = c.POINTER(c.c_ulong)(), c.c_uint()
    x.lib.XQueryTree(x.display, x.root, c.byref(root), c.byref(parent), c.byref(children),
                     c.byref(count))
    order = [children[i] for i in range(count.value)]
    if children:
        x.lib.XFree(children)
    return order


def eventually(condition, message):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        x.lib.XSync(x.display, False)
        if condition():
            return
        time.sleep(.05)
    raise AssertionError(message)


def tracking():
    """Follow, stack directly above, hide and show with an unmanaged target."""
    target = plain_window(60, 80, 320, 200, 0x3366cc)
    occluder = None
    log = output / 'tracking.log'
    with log.open('w') as stream:
        process = subprocess.Popen([str(build / 'glyphshift-overlay'), '--window', str(target),
                                    '--preset', 'art-newsprint'], env=env, stdout=stream,
                                   stderr=subprocess.STDOUT)
    try:
        layer = wait_window('GlyphShift overlay', process, log)

        def follows(x0, y0, w, h):
            info = attributes(layer)
            return info and (info.x, info.y, info.width, info.height, info.map_state) == \
                (x0, y0, w, h, 2)

        def directly_above(window):
            order = stacking()
            return layer in order and window in order and \
                order.index(layer) == order.index(window) + 1

        eventually(lambda: follows(60, 80, 320, 200) and directly_above(target),
                   'overlay did not cover the target')
        call('XMoveResizeWindow', c.c_int,
             [c.c_void_p, c.c_ulong, c.c_int, c.c_int, c.c_uint, c.c_uint],
             x.display, target, 140, 120, 360, 240)
        eventually(lambda: follows(140, 120, 360, 240), 'overlay did not follow move/resize')
        # A window raised above the target must stay above the overlay too.
        occluder = plain_window(100, 100, 200, 150, 0xcc3333)
        call('XMoveWindow', c.c_int, [c.c_void_p, c.c_ulong, c.c_int, c.c_int],
             x.display, target, 150, 130)
        eventually(lambda: follows(150, 130, 360, 240) and directly_above(target),
                   'overlay left its place directly above the target')
        order = stacking()
        assert order.index(occluder) > order.index(layer), 'overlay covered a window above'
        # Raising the target brings the overlay with it.
        call('XRaiseWindow', c.c_int, [c.c_void_p, c.c_ulong], x.display, target)
        eventually(lambda: directly_above(target) and
                   stacking().index(layer) > stacking().index(occluder),
                   'overlay did not follow the raised target')
        # Hidden like a switched workspace or minimized window, then shown.
        call('XUnmapWindow', c.c_int, [c.c_void_p, c.c_ulong], x.display, target)
        eventually(lambda: attributes(layer).map_state == 0, 'overlay stayed over a hidden target')
        call('XMapWindow', c.c_int, [c.c_void_p, c.c_ulong], x.display, target)
        eventually(lambda: follows(150, 130, 360, 240) and directly_above(target),
                   'overlay did not return with the target')
        call('XDestroyWindow', c.c_int, [c.c_void_p, c.c_ulong], x.display, target)
        x.lib.XSync(x.display, False)
        target = None
        assert process.wait(timeout=10) == 0, log.read_text()
        print('Overlay tracking: follows move/resize, stays directly above the target under '
              'other windows, rises with it, hides and returns with it, exits on destroy')
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        for window in [target, occluder]:
            if window:
                call('XDestroyWindow', c.c_int, [c.c_void_p, c.c_ulong], x.display, window)
        x.lib.XSync(x.display, False)


fixture_log = output / 'fixture.log'
overlay_log = output / 'overlay.log'
# The fixture is started directly: no launcher, no preload, no Vulkan layer.
with fixture_log.open('w') as stream:
    fixture = subprocess.Popen([str(build / 'glyphshift-glx-fixture'), '--fixed-time', '1.25',
                                '--frames', '9000'], env=env, stdout=stream,
                               stderr=subprocess.STDOUT)
def single_instance():
    # One daemon per display; no keyboard input. An unusual hotkey avoids
    # other applications' grabs, and notify-send is kept off PATH.
    local = dict(env, PATH=str(output / 'no-programs'))

    def start(name, hotkey):
        log = output / f'daemon-{name}.log'
        with log.open('w') as stream:
            process = subprocess.Popen([str(build / 'glyphshift-overlay'), '--hotkey', hotkey],
                                       env=local, stdout=stream, stderr=subprocess.STDOUT)
        return process, log

    def running(log):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            text = log.read_text()
            if 'running;' in text or 'already running' in text:
                return 'running;' in text
            time.sleep(.05)
        raise RuntimeError('daemon did not report: ' + log.read_text())

    first, first_log = start('first', 'ctrl+alt+shift+F12')
    if not running(first_log):
        first.wait(timeout=10)
        print('Single instance: SKIPPED, another GlyphShift daemon runs on this display')
        return
    second, second_log = start('second', 'ctrl+alt+g')
    assert second.wait(timeout=10) == 0, second_log.read_text()
    assert 'already running' in second_log.read_text() and \
        'Ctrl+Alt+Shift+F12' in second_log.read_text(), second_log.read_text()
    first.send_signal(signal.SIGTERM)
    assert first.wait(timeout=10) == 0, first_log.read_text()
    # Started together, exactly one daemon stays; the other leaves cleanly.
    pair = [start('pair-a', 'ctrl+alt+shift+F11'), start('pair-b', 'ctrl+alt+shift+F12')]
    states = [running(log) for _, log in pair]
    assert sorted(states) == [False, True], [log.read_text() for _, log in pair]
    for (process, log), state in zip(pair, states):
        if state:
            time.sleep(.5)
            assert process.poll() is None, log.read_text()
            process.send_signal(signal.SIGTERM)
        assert process.wait(timeout=10) == 0, log.read_text()
    # The selection is released when a daemon exits.
    again, again_log = start('again', 'ctrl+alt+shift+F12')
    assert running(again_log), again_log.read_text()
    again.send_signal(signal.SIGTERM)
    assert again.wait(timeout=10) == 0, again_log.read_text()
    print('Single instance: a second daemon reports the running one\'s hotkey and exits 0; '
          'of two started together one stays; a new daemon runs after the first exits')


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
    tracking()
    single_instance()
finally:
    for process in [overlay, fixture]:
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
    x.shutdown()
