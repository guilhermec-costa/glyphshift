"""Windows graphics smoke (M7): the Win32 Vulkan fixture, then the capture overlay.

Opt-in, needs an interactive desktop and a Vulkan GPU; no network or
installation. It never takes the desktop keyboard, pointer or focus: windows
open without activation and input is posted to their message queues. Third-party
implicit Vulkan layers (overlays and capture hooks) are disabled for the tested
processes, so their drawing cannot change the images.
"""
import argparse
import contextlib
import os
import pathlib
import subprocess
import sys
import time

import win32

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=pathlib.Path, required=True)
parser.add_argument('--output', type=pathlib.Path)
parser.add_argument('--only', help='Comma-separated phases: fixture, overlay, tracking, editor, '
                    'daemon, oracles')
args = parser.parse_args()
build = args.build.resolve()
if sys.platform != 'win32':
    print('SKIP: Windows only')
    sys.exit(77)
output = (args.output or build / 'smoke-evidence' / 'windows').resolve()
output.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
for name in ['GLYPHSHIFT_CONFIG', 'GLYPHSHIFT_PROFILE', 'GLYPHSHIFT_PRESET', 'GLYPHSHIFT_LOG',
             'GLYPHSHIFT_FIXTURE_MULTI', 'GLYPHSHIFT_FIXTURE_SRGB', 'GLYPHSHIFT_FIXTURE_SCROLL']:
    env.pop(name, None)
env.update(APPDATA=str(output / 'config'), GLYPHSHIFT_FIXTURE_NO_FOCUS='1',
           GLYPHSHIFT_FIXTURE_INPUT_LOG='1', VK_LAYER_VALIDATE_SYNC='1',
           VK_LOADER_LAYERS_DISABLE='VK_LAYER_OBS_HOOK,VK_LAYER_RTSS,'
                                    'VK_LAYER_VALVE_steam_overlay,VK_LAYER_VALVE_steam_fossilize')
sequence = 0


@contextlib.contextmanager
def fixture(label, extra=None, frames=9000):
    """Starts the Vulkan fixture with validation at a fixed time; yields (process, window, log)."""
    global sequence
    sequence += 1
    suffix = f' {os.getpid()}-{sequence}'
    local = dict(env, GLYPHSHIFT_FIXTURE_TITLE_SUFFIX=suffix, **(extra or {}))
    log = output / f'{label}.log'
    with log.open('w') as stream:
        process = subprocess.Popen([str(build / 'glyphshift-vulkan-fixture'), '--validation',
                                    '--fixed-time', '1.25', '--frames', str(frames)],
                                   env=local, stdout=stream, stderr=subprocess.STDOUT)
    try:
        window = win32.wait_window(f'GlyphShift Vulkan fixture{suffix}', process, log)
        yield process, window, log
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)


settle = float(os.environ.get('GLYPHSHIFT_SMOKE_SETTLE', '0.25'))


def image(window, label):
    time.sleep(max(.15, settle))
    picture = win32.image(window)
    win32.save(output / f'{label}.ppm', picture)
    return picture


def finish(process, log):
    # The fixture exits nonzero on any validation or loader error; the loader's
    # notes on the disabled layers are expected.
    assert process.wait(timeout=15) == 0, log.read_text(errors='replace')
    text = log.read_text(errors='replace')
    assert 'Vulkan validation:' not in text, text
    return text


def pixel(picture, x, y):
    width, _, pixels = picture
    at = (y * width + x) * 3
    return tuple(pixels[at:at + 3])


def fixture_phase():
    """The Win32 fixture alone: deterministic image, gradient, resizes, input, exits."""
    sizes = [(320, 240), (1024, 768), (640, 480), (1000, 300), (300, 800), (800, 600)] * 2
    with fixture('fixture') as (process, window, log):
        win32.move(window, 40, 40)
        assert win32.resize(window, 800, 600), win32.client_size(window)
        original = image(window, 'fixture-original')
        # Red grows to the right, green downwards (the shared gradient).
        assert pixel(original, 760, 300)[0] > pixel(original, 40, 300)[0] + 100, \
            (pixel(original, 40, 300), pixel(original, 760, 300))
        assert pixel(original, 400, 560)[1] > pixel(original, 400, 40)[1] + 100, \
            (pixel(original, 400, 40), pixel(original, 400, 560))
        assert len(set(original[2][i:i + 3] for i in range(0, len(original[2]), 3 * 97))) > 50
        for index, (width, height) in enumerate(sizes):
            assert win32.resize(window, width, height), \
                f'resize {width}x{height} gave {win32.client_size(window)}'
            picture = image(window, f'fixture-resize-{index}')
            assert picture[:2] == (width, height), picture[:2]
        assert image(window, 'fixture-again')[2] == original[2], \
            'fixed-time image changed after the resizes'
        win32.key(window, ord('A'), 'a')
        win32.click(window, 100, 100)
        time.sleep(.3)
        win32.close(window)
        text = finish(process, log)
        events = [line for line in text.splitlines() if line.startswith('Fixture input:')]
        assert events == ['Fixture input: key down 65', 'Fixture input: key up 65',
                          'Fixture input: button down 1', 'Fixture input: button up 1'], events
    with fixture('fixture-escape') as (process, window, log):
        win32.key(window, win32.VK_ESCAPE)
        finish(process, log)
    with fixture('fixture-multi', {'GLYPHSHIFT_FIXTURE_MULTI': '1'}, frames=60) as \
            (process, _, log):
        assert 'two swapchains / one device' in finish(process, log)
    print('Fixture: deterministic gradient, 12 confirmed resizes, posted input, '
          'close/Escape/multi-swapchain clean exits, no validation message or loader error')


def overlay_phase():
    """The capture overlay on a fixture started on its own: its pixels equal the
    preview of the captured frame, the screen shows them, input passes through,
    and closing the target ends the overlay."""
    with fixture('overlay-fixture') as (process, window, log):
        win32.move(window, 60, 60)
        assert win32.resize(window, 640, 400), win32.client_size(window)
        original = image(window, 'overlay-original')
        source = win32.save(output / 'overlay-source.ppm', original)
        expected_path = output / 'overlay-expected.ppm'
        subprocess.run([str(build / 'glyphshift-preview'), str(source),
                        str(pathlib.Path(__file__).resolve().parents[2] / 'presets' /
                            'art-newsprint.conf'), str(expected_path)],
                       env=env, check=True, stdout=subprocess.DEVNULL)
        expected = expected_path.read_bytes().split(b'\n', 3)[3]
        overlay_log = output / 'overlay.log'
        with overlay_log.open('w') as stream:
            overlay = subprocess.Popen([str(build / 'glyphshift-overlay'), '--window',
                                        str(window), '--preset', 'art-newsprint', '--log'],
                                       env=dict(env, GLYPHSHIFT_VULKAN_VALIDATION='1'),
                                       stdout=stream, stderr=subprocess.STDOUT)
        try:
            layer = win32.wait_window('GlyphShift overlay', overlay, overlay_log)
            x, y = win32.client_origin(window)
            # The first frame takes a moment (device, pipeline, capture).
            deadline = time.monotonic() + 30
            while True:
                time.sleep(.25)
                screen = win32.screen(x, y, 640, 400)
                worst, close = win32.difference(screen[2], expected)
                if (close >= .999 and worst <= 8) or time.monotonic() > deadline:
                    break
            win32.save(output / 'overlay-screen.ppm', screen)
            assert close >= .999 and worst <= 8, ('screen over the target', worst, close)
            screen_match = (worst, close)
            own = win32.image(layer)
            win32.save(output / 'overlay-own.ppm', own)
            assert own[:2] == (640, 400), own[:2]
            worst, close = win32.difference(own[2], expected)
            assert close >= .999 and worst <= 8, ('overlay pixels', worst, close)
            own_match = (worst, close)
            _, still_original = win32.difference(screen[2], original[2])
            assert still_original < .9, still_original
            left, top, width, height = win32.window_rect(layer)
            assert (left, top, width, height) == (x, y, 640, 400), (left, top, width, height)
            # Input passes through: layered and transparent, and the hit test
            # over the overlay finds the target.
            style = win32.extended_style(layer)
            assert style & win32.WS_EX_LAYERED and style & win32.WS_EX_TRANSPARENT, hex(style)
            assert win32.window_at(x + 320, y + 200) == window, 'overlay takes the pointer'
            win32.close(window)
            finish(process, log)
            assert overlay.wait(timeout=15) == 0, overlay_log.read_text(errors='replace')
            text = overlay_log.read_text(errors='replace')
            assert 'Vulkan validation:' not in text, text
        finally:
            if overlay.poll() is None:
                overlay.terminate()
                overlay.wait(timeout=10)
    print('Capture overlay: independent fixture transformed on screen, output equals the '
          'preview, input passes through, clean exit with the target, no validation message '
          f'(overlay vs preview: max difference {own_match[0]}, {100 * own_match[1]:.2f}% '
          f'identical pixels; screen: {screen_match[0]}, {100 * screen_match[1]:.2f}%; '
          f'screen still original in {100 * still_original:.2f}%)')


def eventually(condition, message, seconds=10):
    deadline = time.monotonic() + seconds
    while not condition():
        if time.monotonic() > deadline:
            raise AssertionError(message)
        time.sleep(.05)


def tracking_phase():
    """Follow, stay directly above the target under later windows, rise with it,
    hide and return with it (minimize, hide), end with it."""
    with fixture('tracking-target') as (process, target, log), \
            fixture('tracking-occluder') as (occluder_process, occluder, occluder_log):
        win32.move(occluder, 1000, 500)
        win32.move(target, 60, 80)
        assert win32.resize(target, 320, 200)
        overlay_log = output / 'tracking-overlay.log'
        with overlay_log.open('w') as stream:
            overlay = subprocess.Popen([str(build / 'glyphshift-overlay'), '--window',
                                        str(target), '--preset', 'art-newsprint'],
                                       env=env, stdout=stream, stderr=subprocess.STDOUT)
        try:
            layer = win32.wait_window('GlyphShift overlay', overlay, overlay_log)

            def follows():
                x, y = win32.client_origin(target)
                width, height = win32.client_size(target)
                return win32.visible(layer) and win32.window_rect(layer) == (x, y, width,
                                                                               height)

            def directly_above(window):
                return win32.visible_above(window) == layer

            def order(window):
                return win32.z_order().index(window)

            eventually(lambda: follows() and directly_above(target),
                       'overlay did not cover the target')
            win32.move(target, 140, 120)
            assert win32.resize(target, 360, 240)
            eventually(follows, 'overlay did not follow move/resize')
            # A window raised above the target stays above the overlay too.
            win32.move(occluder, 100, 100)
            win32.raise_window(occluder)
            win32.move(target, 150, 130)
            eventually(lambda: follows() and directly_above(target),
                       'overlay left its place directly above the target')
            assert order(occluder) < order(layer), 'overlay covered a window above'
            # Raising the target brings the overlay with it.
            win32.raise_window(target)
            eventually(lambda: directly_above(target) and order(layer) < order(occluder),
                       'overlay did not follow the raised target')
            # Hidden while minimized or hidden, back when restored or shown.
            for hide, restore, name in ((win32.SW_SHOWMINNOACTIVE, win32.SW_SHOWNOACTIVATE,
                                         'minimized'), (win32.SW_HIDE, win32.SW_SHOWNA, 'hidden')):
                win32.show(target, hide)
                eventually(lambda: not win32.visible(layer), f'overlay stayed over a {name} target')
                win32.show(target, restore)
                eventually(lambda: follows() and directly_above(target),
                           f'overlay did not return with the {name} target')
            win32.close(target)
            finish(process, log)
            assert overlay.wait(timeout=10) == 0, overlay_log.read_text(errors='replace')
            win32.close(occluder)
            finish(occluder_process, occluder_log)
        finally:
            if overlay.poll() is None:
                overlay.terminate()
                overlay.wait(timeout=10)
    print('Overlay tracking: follows move/resize, stays directly above the target under '
          'other windows, rises with it, hides and returns with it (minimized, hidden), '
          'exits with the target')


INSERT_HOTKEY, F10_HOTKEY = 1, 2
VK_CONTROL = 0x11


@contextlib.contextmanager
def overlay_on(window, label, extra=None):
    """Attaches the overlay to `window` in the no-focus test mode; yields (process, layer, log)."""
    log = output / f'{label}.log'
    with log.open('w') as stream:
        process = subprocess.Popen([str(build / 'glyphshift-overlay'), '--window', str(window)],
                                   env=dict(env, GLYPHSHIFT_VULKAN_VALIDATION='1',
                                            GLYPHSHIFT_OVERLAY_NO_FOCUS='1', **(extra or {})),
                                   stdout=stream, stderr=subprocess.STDOUT)
    try:
        yield process, win32.wait_window('GlyphShift overlay', process, log), log
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)


def screen_over(window, label):
    """What the user sees over the target's client area."""
    time.sleep(max(.3, settle))
    x, y = win32.client_origin(window)
    width, height = win32.client_size(window)
    picture = win32.screen(x, y, width, height)
    win32.save(output / f'{label}.ppm', picture)
    return picture


def hotkey(layer, identifier):
    win32.user32.PostMessageW(layer, 0x0312, identifier, 0)  # WM_HOTKEY


def type_text(layer, text):
    for character in text:
        virtual = win32.user32.VkKeyScanW(character) & 0xff
        win32.key(layer, virtual, character)


def with_control(layer, action):
    win32.user32.PostMessageW(layer, win32.WM_KEYDOWN, VK_CONTROL, win32._key_parameter(VK_CONTROL, False))
    time.sleep(.06)
    action()
    time.sleep(.06)
    win32.user32.PostMessageW(layer, win32.WM_KEYUP, VK_CONTROL, win32._key_parameter(VK_CONTROL, True))
    time.sleep(.12)


def editor_phase():
    """The editor through the overlay, as run.py drives it on Linux: F10, Insert,
    Escape, strength 0/1/0.37 by numeric entry, the binary set, a named preset
    surviving a restart, and the editor across six resizes."""
    profile = f'smoke-windows-{os.getpid()}'
    extra = {'GLYPHSHIFT_PROFILE': profile}
    with fixture('editor-fixture') as (process, window, log):
        win32.move(window, 40, 40)
        assert win32.resize(window, 800, 600)
        original = image(window, 'editor-original')
        with overlay_on(window, 'editor-overlay', extra) as (overlay, layer, overlay_log):
            eventually(lambda: win32.visible(layer), 'overlay not shown', 20)
            time.sleep(1)
            glyph = screen_over(window, 'editor-glyph')
            assert glyph[2] != original[2], 'effect did not transform the image'
            hotkey(layer, F10_HOTKEY)
            assert screen_over(window, 'editor-off')[2] == original[2], 'F10 did not show the original'
            hotkey(layer, INSERT_HOTKEY)
            assert screen_over(window, 'editor-open')[2] != original[2], 'editor not drawn'
            # The open editor takes the pointer; closed, input passes through again.
            x, y = win32.client_origin(window)
            assert not win32.extended_style(layer) & win32.WS_EX_TRANSPARENT and \
                win32.window_at(x + 100, y + 200) == layer, 'open editor does not take input'
            win32.key(layer, win32.VK_ESCAPE)
            assert screen_over(window, 'editor-closed')[2] == original[2], \
                'Escape did not close only the editor'
            hotkey(layer, F10_HOTKEY)
            eventually(lambda: win32.extended_style(layer) & win32.WS_EX_TRANSPARENT and
                       win32.window_at(x + 100, y + 200) == window,
                       'closed editor still takes input')
            hotkey(layer, F10_HOTKEY)
            hotkey(layer, F10_HOTKEY)

            def strength(value):
                hotkey(layer, INSERT_HOTKEY)
                time.sleep(.2)
                with_control(layer, lambda: win32.click(layer, 100, 301))
                with_control(layer, lambda: win32.key(layer, ord('A'), 'a'))
                type_text(layer, value)
                win32.key(layer, win32.VK_RETURN)
                win32.key(layer, win32.VK_ESCAPE)

            strength('0')
            assert screen_over(window, 'editor-strength-zero')[2] == original[2], \
                'zero strength did not retain source pixels'
            strength('1')
            pure = screen_over(window, 'editor-strength-one')
            assert pure[2] != original[2]
            strength('0.37')
            glyph = screen_over(window, 'editor-strength-fraction')
            assert glyph[2] not in (original[2], pure[2]), 'fractional strength did not change'
            # Binary through the real editor, applied.
            hotkey(layer, INSERT_HOTKEY)
            time.sleep(.2)
            for y in (330, 359, 456, 524, 330):
                win32.click(layer, 100, y)
                time.sleep(.15)
            win32.key(layer, win32.VK_ESCAPE)
            selected = screen_over(window, 'editor-binary')
            assert selected[2] != glyph[2], 'character selector did not change the output'
            # Save the disabled effect as "night" through the UI.
            hotkey(layer, F10_HOTKEY)
            hotkey(layer, INSERT_HOTKEY)
            time.sleep(.2)
            win32.click(layer, 90, 122)
            with_control(layer, lambda: win32.key(layer, ord('A'), 'a'))
            type_text(layer, 'night')
            win32.key(layer, win32.VK_RETURN)
            time.sleep(.1)
            win32.click(layer, 288, 122)
            screen_over(window, 'editor-saved')
            directory = output / 'config' / 'glyphshift' / 'profiles' / profile
            saved = directory / 'night.conf'
            eventually(saved.is_file, 'Save preset did not create the file')
            text = saved.read_text(encoding='utf-8')
            assert 'enabled=false' in text and 'glyph_strength=0.37\n' in text and \
                'character_set=binary\n' in text, text
            assert (directory / 'active').read_text(encoding='utf-8') == 'night\n'
            win32.key(layer, win32.VK_ESCAPE)
            time.sleep(.3)
            win32.close(window)
            finish(process, log)
            assert overlay.wait(timeout=15) == 0, overlay_log.read_text(errors='replace')
            assert 'Vulkan validation:' not in overlay_log.read_text(errors='replace')
    # A restarted overlay restores the saved selection and settings.
    with fixture('editor-restart') as (process, window, log):
        win32.move(window, 40, 40)
        assert win32.resize(window, 800, 600)
        with overlay_on(window, 'editor-restart-overlay', extra) as (overlay, layer, overlay_log):
            time.sleep(2)
            assert screen_over(window, 'editor-restarted')[2] == original[2], \
                'preset not retained across restart'
            hotkey(layer, F10_HOTKEY)
            eventually(lambda: win32.visible(layer), 'F10 did not show the effect', 10)
            assert screen_over(window, 'editor-re-enabled')[2] == selected[2], \
                'restored settings render differently'
            for index, (w, h) in enumerate([(320, 240), (1024, 768), (640, 480), (1000, 300),
                                            (300, 800), (800, 600)]):
                assert win32.resize(window, w, h)
                eventually(lambda: win32.window_rect(layer)[2:] == (w, h),
                           f'overlay did not follow {w}x{h}')
                before = screen_over(window, f'editor-resize-{index}-before')
                hotkey(layer, INSERT_HOTKEY)
                assert screen_over(window, f'editor-resize-{index}-open')[2] != before[2]
                win32.key(layer, win32.VK_ESCAPE)
                assert screen_over(window, f'editor-resize-{index}-after')[2] == before[2], \
                    f'closing the editor changed the image at {w}x{h}'
            # A stop request (the daemon's detach) ends the overlay cleanly,
            # and the game keeps running with its own image.
            assert win32.request_stop(overlay.pid), 'overlay does not listen for stop requests'
            assert overlay.wait(timeout=15) == 0, overlay_log.read_text(errors='replace')
            assert process.poll() is None and not win32.exists(layer)
            assert screen_over(window, 'editor-detached')[2] == image(window, 'editor-own')[2], \
                'the screen does not show the game after detaching'
            assert 'Vulkan validation:' not in overlay_log.read_text(errors='replace')
            win32.close(window)
            finish(process, log)
    print('Editor through the overlay: F10/Insert/Escape, strength 0/1/fraction, binary set, '
          'named preset across a restart, editor over 6 resizes, detach by stop request, '
          'no validation message')


def single_instance_phase():
    """One daemon per session, without keyboard input (overlay.py's single
    instance): a second daemon names the first one's hotkey and exits 0; of two
    started together one stays; a new daemon runs after the first exits. Uncommon
    hotkeys avoid other applications' registrations."""
    def start(name, hotkey):
        log = output / f'daemon-{name}.log'
        with log.open('w') as stream:
            process = subprocess.Popen([str(build / 'glyphshift-overlay'), '--hotkey', hotkey],
                                       env=env, stdout=stream, stderr=subprocess.STDOUT)
        return process, log

    def running(log):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            text = log.read_text(errors='replace')
            if 'running;' in text or 'already running' in text:
                return 'running;' in text
            time.sleep(.05)
        raise RuntimeError('daemon did not report: ' + log.read_text(errors='replace'))

    def stop(process, log):
        deadline = time.monotonic() + 10
        while not win32.request_stop(process.pid):
            assert time.monotonic() < deadline, 'daemon does not listen: ' + log.read_text()
            time.sleep(.05)
        assert process.wait(timeout=10) == 0, log.read_text(errors='replace')

    first, first_log = start('first', 'ctrl+alt+shift+F12')
    if not running(first_log):
        first.wait(timeout=10)
        print('Single instance: SKIPPED, another GlyphShift daemon runs in this session')
        return
    second, second_log = start('second', 'ctrl+alt+g')
    assert second.wait(timeout=10) == 0, second_log.read_text(errors='replace')
    text = second_log.read_text(errors='replace')
    assert 'already running' in text and 'Ctrl+Alt+Shift+F12' in text, text
    stop(first, first_log)
    # Started together, exactly one daemon stays; the other leaves cleanly.
    pair = [start('pair-a', 'ctrl+alt+shift+F11'), start('pair-b', 'ctrl+alt+shift+F12')]
    states = [running(log) for _, log in pair]
    assert sorted(states) == [False, True], [log.read_text() for _, log in pair]
    for (process, log), state in zip(pair, states):
        if state:
            time.sleep(.5)
            assert process.poll() is None, log.read_text(errors='replace')
            stop(process, log)
        assert process.wait(timeout=10) == 0, log.read_text(errors='replace')
    # The claim is released when a daemon exits.
    again, again_log = start('again', 'ctrl+alt+shift+F12')
    assert running(again_log), again_log.read_text(errors='replace')
    stop(again, again_log)
    print("Single instance: a second daemon reports the running one's hotkey and exits 0; "
          'of two started together one stays; a new daemon runs after the first exits')


def oracles():
    """The GPU image oracles through the Vulkan preview, with validation."""
    oracle_env = dict(env, GLYPHSHIFT_PREVIEW_VALIDATION='1')
    here = pathlib.Path(__file__).resolve().parent
    for name in ['atlas_filter', 'temporal', 'exposure', 'anchor']:
        subprocess.run([sys.executable, str(here / f'{name}.py'), '--build', str(build),
                        '--output', str(output / name)], env=oracle_env, check=True)


phases = {'fixture': fixture_phase, 'overlay': overlay_phase, 'tracking': tracking_phase,
          'editor': editor_phase, 'daemon': single_instance_phase, 'oracles': oracles}
for name in (args.only.split(',') if args.only else phases):
    phases[name]()
