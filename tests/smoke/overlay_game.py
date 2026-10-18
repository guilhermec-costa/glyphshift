#!/usr/bin/env python3
"""Focused overlay check on a real game: attach with the daemon's global hotkey.

Starts SuperTux (native, or the Windows build through Proton) on its own, on the
introduction of a level, and the overlay daemon with a monochrome live config,
then uses real XTest keyboard input on the focused game:

- Ctrl+Alt+G attaches: the screen over the game shows only glyph colors;
- holding Right reaches the game: Tux walks and the camera scrolls in its own
  image, far beyond its idle animation, while the overlay stays on;
- Insert/Escape open and close the editor over the game;
- a resize is followed by the overlay window;
- Ctrl+Alt+G detaches (the original image returns) and attaches again;
- closing the game ends its overlay; the daemon keeps running until SIGTERM.

The Proton variant must run without network access:
    python3 tests/smoke/test_env.py run --offline -- python3 tests/smoke/overlay_game.py ...
"""
import argparse
import ctypes as c
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

from real_input import RealInput
import test_env
from x11 import X11

XK_G, XK_CONTROL_L, XK_ALT_L = 0x67, 0xffe3, 0xffe9
XK_RIGHT, XK_INSERT, XK_ESCAPE = 0xff53, 0xff63, 0xff1b

parser = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build', type=Path, default=Path('build/release'))
parser.add_argument('--application', choices=['supertux', 'supertux-proton'], required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
build = args.build.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
if not os.getenv('DISPLAY'):
    print('SKIP: DISPLAY is not set')
    sys.exit(77)
x = X11()
env = os.environ.copy()
for name in ['LD_PRELOAD', 'GLYPHSHIFT_PROFILE', 'GLYPHSHIFT_PRESET']:
    env.pop(name, None)
config = output / 'mono.conf'
config.write_text('color_mode=mono\nglyph_strength=1\n')
env.update(XDG_CONFIG_HOME=str(output / 'config'), GLYPHSHIFT_CONFIG=str(config),
           GLYPHSHIFT_GPU_TIMING='1')
if args.application == 'supertux':
    game_command = test_env.supertux_native('--geometry', '800x600', str(test_env.NATIVE_LEVEL))
else:
    test_env.proton_env(env)  # Refuses to continue with network access.
    game_command = test_env.supertux_proton('--geometry', '800x600', test_env.LEVEL)


def eventually(condition, message, seconds=15):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(.1)
    raise AssertionError(message + '\n' + daemon_log.read_text())


def children():
    path = Path(f'/proc/{daemon.pid}/task/{daemon.pid}/children')
    return [int(pid) for pid in path.read_text().split()] if path.exists() else []


def tinted(picture):
    # Share of pixels that are neither black nor white. Monochrome glyphs
    # leave only compositor corner pixels; the game's own image has far more.
    _, _, rgb = picture
    pixels = range(0, len(rgb), 3 * 3)
    return sum(1 for i in pixels if 8 < max(rgb[i:i + 3]) and min(rgb[i:i + 3]) < 247) / len(pixels)


def lit(picture):
    # Share of pixels that are not near black.
    _, _, rgb = picture
    pixels = range(0, len(rgb), 3 * 5)
    return sum(1 for i in pixels if max(rgb[i:i + 3]) > 48) / len(pixels)


def changed(a, b):
    # Share of pixels that differ clearly between two captures of one size.
    _, _, first = a
    _, _, second = b
    pixels = range(0, len(first), 3 * 5)
    return sum(1 for i in pixels
               if max(abs(first[i + k] - second[i + k]) for k in range(3)) > 40) / len(pixels)


def hold(symbol, seconds):
    control.guard()
    code = x.lib.XKeysymToKeycode(x.display, symbol)
    control.test.XTestFakeKeyEvent(x.display, code, True, 0)
    x.lib.XFlush(x.display)
    try:
        time.sleep(seconds)
    finally:
        control.test.XTestFakeKeyEvent(x.display, code, False, 0)
        x.lib.XFlush(x.display)


def save(label, picture):
    w, h, rgb = picture
    (output / f'{label}.ppm').write_bytes(f'P6\n{w} {h}\n255\n'.encode() + rgb)


def glyphs_only():
    return tinted(x.screen(window)) < .001


def hotkey():
    control.key(XK_G, (XK_CONTROL_L, XK_ALT_L))


def root_geometry(target):
    fn = x.lib.XTranslateCoordinates
    fn.argtypes = [c.c_void_p, c.c_ulong, c.c_ulong, c.c_int, c.c_int, c.POINTER(c.c_int),
                   c.POINTER(c.c_int), c.POINTER(c.c_ulong)]
    px, py, child = c.c_int(), c.c_int(), c.c_ulong()
    fn(x.display, target, x.root, 0, 0, c.byref(px), c.byref(py), c.byref(child))
    return (px.value, py.value, *x.geometry(target))


def overlay_follows():
    overlays = x.windows('GlyphShift overlay')
    return len(overlays) == 1 and root_geometry(overlays[0]) == root_geometry(window)


daemon_log = output / 'daemon.log'
game_log = output / 'game.log'
previous = RealInput.read_focus(x)
daemon = game = control = None
try:
    with daemon_log.open('w') as stream:
        daemon = subprocess.Popen([str(build / 'glyphshift-overlay'), '--log'], env=env,
                                  stdout=stream, stderr=subprocess.STDOUT)
    eventually(lambda: 'running;' in daemon_log.read_text(), 'daemon did not start')
    with game_log.open('w') as stream:
        game = subprocess.Popen(game_command, env=env, stdout=stream, stderr=subprocess.STDOUT)
    eventually(lambda: x.windows(test_env.SUPERTUX_TITLE), 'game window did not appear', 90)
    # Wine may name more than one window; the game's client is the largest.
    window = max(x.windows(test_env.SUPERTUX_TITLE), key=lambda w: x.geometry(w)[0] * x.geometry(w)[1])
    if shutil.which('i3-msg'):
        subprocess.run(['i3-msg', f'[id={window}] floating enable'], stdout=subprocess.DEVNULL,
                       check=True)
    x.resize(window, 800, 600)
    time.sleep(4)
    control = RealInput(x, window, previous)
    # Focus through i3 as a user's click would, after RealInput maps its
    # sentinel: i3 restores the window it believes focused when the hotkey's
    # grab ends (see overlay_daemon.py).
    if shutil.which('i3-msg'):
        subprocess.run(['i3-msg', f'[id={window}] focus'], stdout=subprocess.DEVNULL, check=True)
    time.sleep(1)
    control.guard()
    original = x.screen(window)
    save('1-unattached-screen', original)
    print(f'unattached: {tinted(original):.2%} of the screen over the game is neither black nor white')
    assert tinted(original) > .003, 'the game image is already monochrome'

    hotkey()
    eventually(lambda: len(children()) == 1 and 'attached' in daemon_log.read_text(),
               'hotkey did not attach an overlay')
    eventually(glyphs_only, 'the attached overlay does not show glyphs over the game')
    save('2-attached-screen', x.screen(window))
    save('2-attached-game', x.image(window))
    print(f'attached: overlay pid {children()[0]}; {tinted(x.screen(window)):.3%} of the screen '
          'is neither black nor white')

    # The level introduction ends on its own (or with a key in some builds).
    eventually(lambda: lit(x.image(window)) > .5, 'the level did not start', 30)
    time.sleep(1)
    before = x.image(window)
    time.sleep(1)
    idle = x.image(window)
    hold(XK_RIGHT, 2.5)
    moved = x.image(window)
    save('3-level-before', idle)
    save('3-level-after-right', moved)
    save('3-level-screen', x.screen(window))
    still, walked = changed(before, idle), changed(idle, moved)
    print(f'input: holding Right changed {walked:.0%} of the game\'s own image '
          f'(idle: {still:.0%}); overlay still on')
    assert walked > max(.2, 3 * still), 'the game did not react to Right'
    eventually(glyphs_only, 'the overlay stopped covering the running level')

    control.key(XK_INSERT)
    eventually(lambda: not glyphs_only(), 'Insert did not open the editor over the game')
    save('4-editor-screen', x.screen(window))
    control.key(XK_ESCAPE)
    eventually(glyphs_only, 'Escape did not close the editor')
    print('editor: Insert opened and Escape closed it over the game')

    x.resize(window, 640, 480)
    eventually(overlay_follows, 'the overlay did not follow the resized game')
    eventually(glyphs_only, 'the overlay did not render the resized game')
    print(f'follow: overlay at {root_geometry(x.windows("GlyphShift overlay")[0])}, same as the game')

    control.guard()
    hotkey()
    eventually(lambda: not children(), 'hotkey did not detach the overlay')
    eventually(lambda: not glyphs_only(), 'the original image did not return')
    save('5-detached-screen', x.screen(window))
    print('detached: original image on screen')

    hotkey()
    eventually(lambda: len(children()) == 1, 'hotkey did not attach again')
    eventually(glyphs_only, 'the second attachment shows no glyphs')
    x.close(window)
    game.wait(timeout=60)
    eventually(lambda: not children(), 'overlay did not end with its game')
    assert daemon.poll() is None, daemon_log.read_text()
    print(f'game closed (status {game.returncode}): its overlay ended; daemon still running')

    daemon.send_signal(signal.SIGTERM)
    assert daemon.wait(timeout=10) == 0, daemon_log.read_text()
    print('daemon stopped cleanly')
    for line in daemon_log.read_text().splitlines():
        if line.startswith('glyphshift: GPU'):
            print('  ' + line)
finally:
    if control:
        control.close()
    for process in (game, daemon):
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=30)
print(f'PASS: overlay hotkey on {args.application}')
