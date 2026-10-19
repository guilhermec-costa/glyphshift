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


def oracles():
    """The GPU image oracles through the Vulkan preview, with validation."""
    oracle_env = dict(env, GLYPHSHIFT_PREVIEW_VALIDATION='1')
    here = pathlib.Path(__file__).resolve().parent
    for name in ['atlas_filter', 'temporal', 'exposure', 'anchor']:
        subprocess.run([sys.executable, str(here / f'{name}.py'), '--build', str(build),
                        '--output', str(output / name)], env=oracle_env, check=True)


fixture_phase()
overlay_phase()
oracles()
