"""Focused overlay check on a real game on Windows, the counterpart of
overlay_game.py. By default it starts SuperTux 0.7.0 for Windows natively from
the test environment (test_env.py setup) on the introduction of a level; with
`--running EXE` it attaches to a game the user started on their own (windowed
or borderless) and left in front, which it neither resizes nor closes. The
overlay daemon runs with a monochrome live config, driven with SendInput on the
focused game:

- Ctrl+Alt+G attaches: the screen over the game shows only glyph colors;
- holding a key reaches the game (SuperTux: Right, Tux walks and the camera
  scrolls; `--hold-key`, for example a camera key, for other games): the
  game's own image changes far beyond its idle animation while the overlay
  stays on;
- Insert/Escape open and close the editor over the game;
- SuperTux only: a resize is followed by the overlay window;
- Ctrl+Alt+G detaches (the original image returns) and attaches again;
- SuperTux: closing the game ends its overlay; a running game is detached
  instead and keeps running. The daemon stops when asked.

SuperTux's test profile turns its networking, release check and Discord off.
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
parser.add_argument('--running', metavar='EXE',
                    help='Attach to the running game with this executable, e.g. eldenring.exe')
parser.add_argument('--hold-key', default=None,
                    help='Input that must reach the game: a letter or Right held, or Mouse '
                         '(the pointer moved right, turning a 3D camera) (default: Right for '
                         'SuperTux; required with --running)')
args = parser.parse_args()
build = args.build.resolve()
if sys.platform != 'win32':
    print('SKIP: Windows only')
    sys.exit(77)
if args.running and not args.hold_key:
    parser.error('--running needs --hold-key')
action = (args.hold_key or 'right').lower()
held = VK_RIGHT if action == 'right' else None if action == 'mouse' else ord(action.upper())
if not args.running and not test_env.marked(test_env.SUPERTUX_EXE.parents[1]):
    raise SystemExit('the test environment is incomplete; run: python tests/smoke/test_env.py setup')
name = pathlib.Path(args.running).stem.lower() if args.running else 'supertux'
output = (args.output or build / 'smoke-evidence' / f'windows-game-{name}').resolve()
output.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
for variable in ['GLYPHSHIFT_PROFILE', 'GLYPHSHIFT_PRESET', 'GLYPHSHIFT_OVERLAY_NO_FOCUS']:
    env.pop(variable, None)
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


def blocks(picture, columns=32, rows=18):
    # Mean brightness of a 32x18 grid: small moving details (particles, fire,
    # an idle animation) fade, while a scrolling or turning camera changes
    # most cells, in dark scenes too.
    width, height, rgb = picture
    means = []
    for row in range(rows):
        for column in range(columns):
            total = count = 0
            for y in range(row * height // rows, (row + 1) * height // rows, 4):
                for x in range(column * width // columns, (column + 1) * width // columns, 4):
                    i = (y * width + x) * 3
                    total += rgb[i] + rgb[i + 1] + rgb[i + 2]
                    count += 3
            means.append(total / count)
    return means


def changed(a, b):
    # Share of grid cells whose brightness differs between two captures.
    return sum(1 for x, y in zip(blocks(a), blocks(b)) if abs(x - y) > 6) / (32 * 18)


def save(label, picture):
    win32.save(output / f'{label}.ppm', picture)


def glyphs_only():
    return tinted(screen()) < .001


def explain(label):
    """On a failure: the screen and the foreground window, for the record."""
    picture = screen()
    save(label, picture)
    current = foreground()
    print(f'{label}: {tinted(picture):.3%} of the screen neither black nor white; foreground '
          f'{win32.class_name(current)!r} {win32.window_title(current)!r} (pid '
          f'{win32.process_of(current)})', flush=True)
    for pid in overlays():
        layer = win32.window_of('GlyphShiftOverlay', pid)
        print(f'  overlay pid {pid}: window {layer} visible {layer and win32.visible(layer)} '
              f'style {layer and hex(win32.extended_style(layer))} rect '
              f'{layer and win32.window_rect(layer)}', flush=True)
    print(f'  game style {hex(win32.extended_style(window))}; z-order from the top:', flush=True)
    for above in win32.z_order()[:6]:
        print(f'    {win32.class_name(above)!r} {win32.window_title(above)[:40]!r} '
              f'{hex(win32.extended_style(above))}', flush=True)


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
    if args.running:
        pids = win32.processes(args.running)
        if not pids:
            raise SystemExit(f'{args.running} is not running: start the game first')
        window = next(filter(None, map(win32.main_window, pids)), None)
        if not window:
            raise SystemExit(f'{args.running} has no visible window')
    with daemon_log.open('w') as stream:
        daemon = subprocess.Popen([str(build / 'glyphshift-overlay'), '--log'], env=env,
                                  stdout=stream, stderr=subprocess.STDOUT)
    eventually(lambda: 'running;' in daemon_log.read_text() or
               'already running' in daemon_log.read_text(), 'daemon did not start')
    if 'already running' in daemon_log.read_text():
        raise SystemExit('stop your own GlyphShift daemon first: only one runs per session')
    if not args.running:
        with game_log.open('w') as stream:
            game = subprocess.Popen(test_env.supertux_windows(test_env.LEVEL), env=env,
                                    stdout=stream, stderr=subprocess.STDOUT,
                                    cwd=test_env.SUPERTUX_EXE.parent)
        eventually(lambda: [w for w in win32.windows(test_env.SUPERTUX_TITLE)
                            if win32.process_of(w) == game.pid], 'game window did not appear',
                   60)
        window = next(w for w in win32.windows(test_env.SUPERTUX_TITLE)
                      if win32.process_of(w) == game.pid)
        win32.move(window, 40, 40)
        assert win32.resize(window, 800, 600), win32.client_size(window)
        time.sleep(3)
    print(f'game window: {win32.window_title(window)!r}, client area '
          f'{win32.client_size(window)} at {win32.client_origin(window)}')
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

    if not args.running:
        # The level introduction ends on its own.
        eventually(lambda: lit(win32.image(window)) > .5, 'the level did not start', 30)
    time.sleep(1)
    before = win32.image(window)
    time.sleep(1)
    idle = win32.image(window)
    if held is None:
        control.turn(25, 2.5)
    else:
        control.hold(held, 2.5)
    moved = win32.image(window)
    save('3-game-before', idle)
    save('3-game-after-key', moved)
    save('3-game-screen', screen())
    still, reacted = changed(before, idle), changed(idle, moved)
    print(f"input: {args.hold_key or 'Right'} changed {reacted:.0%} of the game's own image "
          f'(32x18 brightness grid; idle: {still:.0%}); overlay still on')
    assert lit(moved) > .05, "the game's own image cannot be read (PrintWindow returned black)"
    assert reacted > max(.3, 3 * still), 'the game did not react to the held key'
    eventually(glyphs_only, 'the overlay stopped covering the running game')

    control.key(VK_INSERT)
    eventually(lambda: not glyphs_only(), 'Insert did not open the editor over the game')
    control.allow(foreground())  # The open editor's overlay window.
    explain('4-editor-screen')
    control.key(VK_ESCAPE)
    try:
        eventually(glyphs_only, 'Escape did not close the editor')
    except AssertionError:
        explain('4-after-escape')
        raise
    eventually(lambda: foreground() == window, 'closing the editor did not return the game')
    print('editor: Insert opened and Escape closed it over the game')

    if not args.running:
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
    if args.running:
        hotkey()
        eventually(lambda: not overlays(), 'hotkey did not detach the second overlay')
        eventually(lambda: not glyphs_only(), 'the original image did not return')
        control.close()
        control = None
        assert win32.exists(window), 'the game window is gone'
        print('attached again and detached; the game keeps running')
    else:
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
print(f'PASS: overlay hotkey on {args.running or "SuperTux for Windows"}')
