"""Focused overlay check on a real game on Windows, the counterpart of
overlay_game.py: SuperTux 0.7.0 for Windows, run natively from the test
environment (test_env.py setup) on the introduction of a level, and the
overlay daemon with a monochrome live config, driven with SendInput on the
focused game:

- Ctrl+Alt+G attaches: the screen over the game shows only glyph colors;
- holding Right reaches the game: Tux walks and the camera scrolls in its own
  image, far beyond its idle animation, while the overlay stays on;
- Insert/Escape open and close the editor over the game;
- a resize is followed by the overlay window;
- Ctrl+Alt+G detaches (the original image returns) and attaches again;
- closing the game ends its overlay; the daemon keeps running until stopped.

The test profile turns the game's networking, release check and Discord off.
It takes the keyboard and pointer for about half a minute.
"""
import argparse
import os
import pathlib
import subprocess
import sys
import time

import test_env
import win32
from real_input_win32 import VK_LCONTROL, VK_LMENU, RealInput, foreground

VK_RIGHT, VK_INSERT, VK_ESCAPE = 0x27, 0x2D, 0x1B

parser = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build', type=pathlib.Path, required=True)
parser.add_argument('--output', type=pathlib.Path)
args = parser.parse_args()
build = args.build.resolve()
if sys.platform != 'win32':
    print('SKIP: Windows only')
    sys.exit(77)
if not test_env.marked(test_env.SUPERTUX_EXE.parents[1]):
    raise SystemExit('the test environment is incomplete; run: python tests/smoke/test_env.py setup')
output = (args.output or build / 'smoke-evidence' / 'windows-game').resolve()
output.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
for name in ['GLYPHSHIFT_PROFILE', 'GLYPHSHIFT_PRESET', 'GLYPHSHIFT_OVERLAY_NO_FOCUS']:
    env.pop(name, None)
config = output / 'mono.conf'
config.write_text('color_mode=mono\nglyph_strength=1\n', encoding='utf-8')
env.update(APPDATA=str(output / 'config'), GLYPHSHIFT_CONFIG=str(config),
           GLYPHSHIFT_NO_NOTIFICATIONS='1',
           VK_LOADER_LAYERS_DISABLE='VK_LAYER_OBS_HOOK,VK_LAYER_RTSS,'
                                    'VK_LAYER_VALVE_steam_overlay,VK_LAYER_VALVE_steam_fossilize')
daemon_log = output / 'daemon.log'
game_log = output / 'game.log'


def eventually(condition, message, seconds=15):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(.1)
    raise AssertionError(message + '\n' + daemon_log.read_text(errors='replace'))


def overlays():
    return win32.children(daemon.pid)


def screen():
    """What the user sees over the game's client area."""
    x, y = win32.client_origin(window)
    width, height = win32.client_size(window)
    return win32.screen(x, y, width, height)


def tinted(picture):
    # Share of pixels that are neither black nor white. Monochrome glyphs
    # leave almost none; the game's own image has far more.
    _, _, rgb = picture
    pixels = range(0, len(rgb), 3 * 3)
    return sum(1 for i in pixels if 8 < max(rgb[i:i + 3]) and min(rgb[i:i + 3]) < 247) / \
        len(pixels)


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


def save(label, picture):
    win32.save(output / f'{label}.ppm', picture)


def glyphs_only():
    return tinted(screen()) < .001


def hotkey():
    # The daemon ignores presses within 400 ms of the previous one.
    time.sleep(.5)
    control.key(ord('G'), (VK_LCONTROL, VK_LMENU))


def overlay_follows():
    layers = [layer for pid in overlays() if (layer := win32.window_of('GlyphShiftOverlay', pid))]
    if len(layers) != 1:
        return False
    x, y = win32.client_origin(window)
    return win32.window_rect(layers[0]) == (x, y, *win32.client_size(window))


daemon = game = control = None
try:
    with daemon_log.open('w') as stream:
        daemon = subprocess.Popen([str(build / 'glyphshift-overlay'), '--log'], env=env,
                                  stdout=stream, stderr=subprocess.STDOUT)
    eventually(lambda: 'running;' in daemon_log.read_text() or
               'already running' in daemon_log.read_text(), 'daemon did not start')
    if 'already running' in daemon_log.read_text():
        raise SystemExit('stop your own GlyphShift daemon first: only one runs per session')
    with game_log.open('w') as stream:
        game = subprocess.Popen(test_env.supertux_windows(test_env.LEVEL), env=env,
                                stdout=stream, stderr=subprocess.STDOUT,
                                cwd=test_env.SUPERTUX_EXE.parent)
    eventually(lambda: [w for w in win32.windows(test_env.SUPERTUX_TITLE)
                        if win32.process_of(w) == game.pid], 'game window did not appear', 60)
    window = next(w for w in win32.windows(test_env.SUPERTUX_TITLE)
                  if win32.process_of(w) == game.pid)
    win32.move(window, 40, 40)
    assert win32.resize(window, 800, 600), win32.client_size(window)
    time.sleep(3)
    control = RealInput(window)
    time.sleep(1)
    control.guard()
    original = screen()
    save('1-unattached-screen', original)
    print(f'unattached: {tinted(original):.2%} of the screen over the game is neither black '
          'nor white')
    assert tinted(original) > .003, 'the game image is already monochrome'

    hotkey()
    eventually(lambda: len(overlays()) == 1 and 'attached' in daemon_log.read_text(),
               'hotkey did not attach an overlay')
    eventually(glyphs_only, 'the attached overlay does not show glyphs over the game')
    save('2-attached-screen', screen())
    save('2-attached-game', win32.image(window))
    print(f'attached: overlay pid {overlays()[0]}; {tinted(screen()):.3%} of the screen is '
          'neither black nor white')

    # The level introduction ends on its own.
    eventually(lambda: lit(win32.image(window)) > .5, 'the level did not start', 30)
    time.sleep(1)
    before = win32.image(window)
    time.sleep(1)
    idle = win32.image(window)
    control.hold(VK_RIGHT, 2.5)
    moved = win32.image(window)
    save('3-level-before', idle)
    save('3-level-after-right', moved)
    save('3-level-screen', screen())
    still, walked = changed(before, idle), changed(idle, moved)
    print(f"input: holding Right changed {walked:.0%} of the game's own image "
          f'(idle: {still:.0%}); overlay still on')
    assert walked > max(.2, 3 * still), 'the game did not react to Right'
    eventually(glyphs_only, 'the overlay stopped covering the running level')

    control.key(VK_INSERT)
    eventually(lambda: not glyphs_only(), 'Insert did not open the editor over the game')
    control.allow(foreground())  # The open editor's overlay window.
    save('4-editor-screen', screen())
    control.key(VK_ESCAPE)
    eventually(glyphs_only, 'Escape did not close the editor')
    eventually(lambda: foreground() == window, 'closing the editor did not return the game')
    print('editor: Insert opened and Escape closed it over the game')

    assert win32.resize(window, 640, 480), win32.client_size(window)
    eventually(overlay_follows, 'the overlay did not follow the resized game')
    eventually(glyphs_only, 'the overlay did not render the resized game')
    print(f'follow: overlay at {win32.client_origin(window)} {win32.client_size(window)}, '
          "over the game's client area")

    hotkey()
    eventually(lambda: not overlays(), 'hotkey did not detach the overlay')
    eventually(lambda: not glyphs_only(), 'the original image did not return')
    save('5-detached-screen', screen())
    print('detached: original image on screen')

    hotkey()
    eventually(lambda: len(overlays()) == 1, 'hotkey did not attach again')
    eventually(glyphs_only, 'the second attachment shows no glyphs')
    control.close()
    control = None
    win32.close(window)
    game.wait(timeout=60)
    eventually(lambda: not overlays(), 'overlay did not end with its game')
    assert daemon.poll() is None, daemon_log.read_text()
    print(f'game closed (status {game.returncode}): its overlay ended; daemon still running')

    assert win32.request_stop(daemon.pid)
    assert daemon.wait(timeout=10) == 0, daemon_log.read_text()
    print('daemon stopped cleanly')
finally:
    if control:
        control.close()
    for process in (game, daemon):
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=30)
print('PASS: overlay hotkey on SuperTux for Windows')
