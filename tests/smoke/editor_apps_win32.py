"""Focused editor check on Windows with real input, the counterpart of
`editor_apps.py --application overlay-fixture`: the capture overlay attached to
the Win32 Vulkan fixture, its editor driven with SendInput at the panel
coordinates the Linux script uses (Insert, a Ctrl-click numeric edit, the
character set combo, a named preset saved, Escape, F10, resizes with the editor
open, closing on focus loss, the panel's close button), then a key and a click
that must reach the fixture as its only input, and a clean exit.
`--illustrated` and `--presets` add the same optional sections as on Linux.

It takes the desktop keyboard and pointer for about half a minute; run it
only when nobody uses them. The foreground and the pointer are given back at
the end.
"""
import argparse
import os
import pathlib
import subprocess
import sys
import time

import win32
from real_input_win32 import VK_LCONTROL, VK_LMENU, RealInput, foreground

VK_INSERT, VK_F10, VK_ESCAPE, VK_RETURN = 0x2D, 0x79, 0x1B, 0x0D

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=pathlib.Path, required=True)
parser.add_argument('--output', type=pathlib.Path)
parser.add_argument('--illustrated', action='store_true',
                    help='Enable illustrated rendering through the panel')
parser.add_argument('--presets', action='store_true',
                    help='Select every included style and save personal copies')
args = parser.parse_args()
build = args.build.resolve()
if sys.platform != 'win32':
    print('SKIP: Windows only')
    sys.exit(77)
out = (args.output or build / 'smoke-evidence' / 'windows-editor').resolve()
out.mkdir(parents=True, exist_ok=True)
for stale in out.glob('config/glyphshift/profiles/editor-overlay-fixture/*'):
    stale.unlink()
config = out / 'live.conf'
config.write_text('color_mode=mono\nglyph_strength=1\n', encoding='utf-8')
env = os.environ.copy()
for name in ['GLYPHSHIFT_PROFILE', 'GLYPHSHIFT_PRESET', 'GLYPHSHIFT_FIXTURE_NO_FOCUS',
             'GLYPHSHIFT_OVERLAY_NO_FOCUS', 'GLYPHSHIFT_FIXTURE_MULTI',
             'GLYPHSHIFT_FIXTURE_SCROLL']:
    env.pop(name, None)
suffix = f' editor-{os.getpid()}'
env.update(GLYPHSHIFT_CONFIG=str(config), APPDATA=str(out / 'config'),
           GLYPHSHIFT_FIXTURE_TITLE_SUFFIX=suffix, GLYPHSHIFT_FIXTURE_INPUT_LOG='1',
           VK_LAYER_VALIDATE_SYNC='1',
           VK_LOADER_LAYERS_DISABLE='VK_LAYER_OBS_HOOK,VK_LAYER_RTSS,'
                                    'VK_LAYER_VALVE_steam_overlay,VK_LAYER_VALVE_steam_fossilize')
app = 'overlay-fixture'
fixture_log = out / 'application.log'
overlay_log = out / 'overlay.log'
fixture = overlay = control = None


def eventually(condition, message, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(.1)
    raise AssertionError(message)


def view():
    """What the user sees over the fixture's client area."""
    time.sleep(.15)
    x, y = win32.client_origin(window)
    width, height = win32.client_size(window)
    return win32.screen(x, y, width, height)


def capture(label, editor):
    time.sleep(.3)
    assert fixture.poll() is None, fixture_log.read_text(errors='replace')
    assert overlay.poll() is None, overlay_log.read_text(errors='replace')
    picture = view()
    win32.save(out / (label + '.ppm'), picture)
    width, height, rgb = picture
    colors = len(set(zip(rgb[0::3], rgb[1::3], rgb[2::3])))
    # The monochrome effect is gray; the open panel alone has thousands of
    # colored pixels.
    tinted = sum(r != g or g != b for r, g, b in zip(rgb[0::3], rgb[1::3], rgb[2::3]))
    chromatic = tinted > width * height // 1000
    assert chromatic == editor, (label, colors, tinted)
    print(f'{app}: {label} {width}x{height} colors={colors}', flush=True)
    return picture


def click(x, y, modifier=None):
    """A click at the fixture's client coordinates (the panel's)."""
    origin = win32.client_origin(window)
    if modifier:
        control.key_down(modifier)
    try:
        control.click(origin[0] + x, origin[1] + y)
    finally:
        if modifier:
            control.key_up(modifier)


def ctrl_a():
    control.key(ord('A'), (VK_LCONTROL,))


try:
    with fixture_log.open('w') as stream:
        fixture = subprocess.Popen([str(build / 'glyphshift-vulkan-fixture'), '--validation',
                                    '--fixed-time', '1.25', '--frames', '1000000'], env=env,
                                   stdout=stream, stderr=subprocess.STDOUT)
    window = win32.wait_window('GlyphShift Vulkan fixture' + suffix, fixture, fixture_log)
    win32.move(window, 40, 40)
    assert win32.resize(window, 800, 600), win32.client_size(window)
    with overlay_log.open('w') as stream:
        overlay = subprocess.Popen([str(build / 'glyphshift-overlay'), '--window', str(window)],
                                   env=dict(env, GLYPHSHIFT_PROFILE='editor-' + app,
                                            GLYPHSHIFT_VULKAN_VALIDATION='1'),
                                   stdout=stream, stderr=subprocess.STDOUT)
    layer = win32.wait_window_of('GlyphShiftOverlay', overlay, overlay_log)
    control = RealInput(window)
    control.allow(layer)
    time.sleep(2)

    capture('closed', False)
    control.key(VK_INSERT)
    eventually(lambda: foreground() == layer, 'the open editor did not take the foreground')
    capture('opened', True)
    profile_dir = out / 'config/glyphshift/profiles' / ('editor-' + app)
    if args.presets:
        names = ['art-comic', 'art-sepia', 'art-newsprint', 'art-chalk', 'art-ink-edges',
                 'art-true-color', 'art-midnight', 'art-crt', 'art-digital-rain']

        def choose(position, expected):
            click(140, 95)
            time.sleep(.2)
            if position == 1:
                capture('included-preset-menu', True)
            if position < 0:
                capture('personal-preset-menu', True)
                saved_count = len(list(profile_dir.glob('*.conf')))
                # The "Saved presets" heading follows the last included row.
                first_saved = 151 + (len(names) - 1) * 23 + 51
                click(100, first_saved + (saved_count - 1) * 23)
            else:
                # The popup starts with the "Included styles" heading, then
                # one selectable row per included style.
                click(100, 151 + (position - 1) * 23)
            time.sleep(.35)
            active = (profile_dir / 'active').read_text(encoding='utf-8')
            assert active == expected + '\n', (expected, active)

        seen = []
        for index, name in enumerate(names, 1):
            choose(index, name)
            width, height, rgb = capture(name + '-selected', True)
            # Only the scene right of the panel, so labels alone cannot pass.
            scene = b''.join(rgb[(y * width + 400) * 3:(y * width + width) * 3]
                             for y in range(height))
            assert all(scene != previous for previous in seen), name
            seen.append(scene)
            # Copies of the first and last styles only, so the saved section
            # stays within the popup without scrolling.
            if index not in (1, len(names)):
                continue
            click(290, 124)
            saved_copy = profile_dir / (name + '-copy.conf')
            eventually(saved_copy.exists, 'Included style did not propose a personal copy')
            root = pathlib.Path(__file__).resolve().parents[2]
            expected = dict(line.split('=', 1) for line in
                            (root / 'presets' / (name + '.conf')).read_text(
                                encoding='utf-8').splitlines()
                            if '=' in line and not line.startswith('#'))
            actual = dict(line.split('=', 1) for line in
                          saved_copy.read_text(encoding='utf-8').splitlines() if '=' in line)
            for key, value in expected.items():
                if value != actual[key]:
                    assert all(abs(float(a) - float(b)) < 1e-6 for a, b in
                               zip(value.split(','), actual[key].split(','))), \
                        (name, key, value, actual[key])
        # Included styles and saved copies coexist; the last saved row
        # alphabetically is the digital rain copy.
        choose(-1, 'art-digital-rain-copy')
        capture('personal-copy-selected', True)
        # A changed external config still overrides an explicit selection; an
        # unchanged one must not undo it every frame.
        config.write_text('color_mode=mono\nglyph_strength=1\ncell_width=9\n', encoding='utf-8')
        time.sleep(.35)
        config.write_text('color_mode=mono\nglyph_strength=1\n', encoding='utf-8')
        time.sleep(.35)
        print(f'{app}: {len(names)} included styles, distinct live scene images and saved '
              'copies passed', flush=True)
    if args.illustrated:
        click(100, 388)  # Illustration section, after Reset.
        capture('illustration-controls', True)
        click(30, 416)   # Enable illustrated rendering.
        time.sleep(.5)
        capture('illustration-enabled', True)
        click(30, 505)   # Enable shape matching through its checkbox.
        time.sleep(.5)
        capture('shape-matching-enabled', True)
        click(100, 388)
        capture('illustrated-opened', True)
    # A real Ctrl-click numeric edit, the binary set, then a named preset.
    click(100, 214, VK_LCONTROL)
    ctrl_a()
    control.text('21')
    control.key(VK_RETURN)
    click(100, 330)
    click(100, 359)
    click(100, 456)  # Binary set in the combo popup.
    click(100, 524)  # Apply characters.
    click(100, 330)
    click(100, 124)
    ctrl_a()
    control.text('app-editor')
    click(290, 124)
    saved = profile_dir / 'app-editor.conf'
    eventually(saved.exists, 'Save preset did not create a file')
    text = saved.read_text(encoding='utf-8')
    assert 'cell_width=21\n' in text and 'character_set=binary\n' in text, text
    if args.illustrated:
        assert 'render_style=illustrated\n' in text and 'shape_matching=true\n' in text, text
    capture('edited-saved', True)
    control.key(VK_ESCAPE)
    eventually(lambda: foreground() == window, 'closing did not return the foreground')
    capture('escape-closed', False)
    control.key(VK_F10)
    _, _, original = view()
    assert len(set(zip(original[0::3], original[1::3], original[2::3]))) > 2, 'F10 failed'
    control.key(VK_F10)
    capture('f10-restored', False)
    control.key(VK_INSERT)
    capture('reopened', True)
    for index, size in enumerate([(640, 480), (1000, 300), (800, 600)]):
        assert win32.resize(window, *size)
        eventually(lambda: win32.window_rect(layer)[2:] == size, f'overlay did not follow {size}')
        capture('open-resize-' + str(index), True)
    # The foreground goes to a test-owned window, not the user's application.
    control.activate(control.sentinel())
    control.pump()
    capture('focus-loss-closed', False)
    control.activate(window)
    control.key(VK_INSERT)
    capture('focus-restored-opened', True)
    # Alt+Tab away from the open editor: the editor sees Alt pressed, but its
    # release goes to the window that took the foreground. Back on the game,
    # the closed editor must give the input back (input-transparent, the game
    # under the pointer), not wait forever for that release.
    control.key_down(VK_LMENU)
    control.activate(control.sentinel())
    control.key_up(VK_LMENU)
    control.pump()
    capture('alt-tab-closed', False)
    control.activate(window)
    center = win32.client_origin(window)
    center = (center[0] + 400, center[1] + 300)
    eventually(lambda: win32.extended_style(layer) & win32.WS_EX_TRANSPARENT and
               win32.window_at(*center) == window,
               'after Alt+Tab the closed editor still takes input over the game')
    control.key(VK_INSERT)
    capture('alt-tab-reopened', True)
    click(344, 18)  # The panel's close button.
    capture('title-close', False)
    eventually(lambda: foreground() == window, 'the close button did not return the foreground')
    control.key(ord('A'))
    click(500, 450)
    time.sleep(.3)
    text = fixture_log.read_text(errors='replace')
    events = [line for line in text.splitlines() if line.startswith('Fixture input:')]
    # Nothing typed or clicked in the panel, and no hotkey, reached the game.
    assert events == ['Fixture input: key down 65', 'Fixture input: key up 65',
                      'Fixture input: button down 1', 'Fixture input: button up 1'], events
    control.key(VK_ESCAPE)
    control.close()
    control = None
    assert fixture.wait(timeout=20) == 0, fixture_log.read_text(errors='replace')
    assert overlay.wait(timeout=20) == 0, overlay_log.read_text(errors='replace')
    text = fixture_log.read_text(errors='replace') + overlay_log.read_text(errors='replace')
    for error in ['VUID-', 'Validation Error', 'Vulkan validation:', 'editor input:']:
        assert error not in text, (error, text)
    print(f'{app}: editor mouse/keyboard/save/hotkeys/resize/focus/clean exit passed', flush=True)
finally:
    if control:
        control.close()
    for process in (overlay, fixture):
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
