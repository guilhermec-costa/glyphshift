"""Focused overlay daemon check on Windows, the counterpart of overlay_daemon.py:
the real Ctrl+Alt+G attaches the overlay to the focused Win32 Vulkan fixture,
detaches it and attaches it again; key and click input reaches the fixture
through the attached overlay; the icon's menu closes on a click elsewhere and
"Detach from" detaches; closing the fixture ends its overlay; the daemon stops
with status 0.

It uses SendInput, so it takes the desktop keyboard and pointer for about
twenty seconds; run it only when nobody uses them. The foreground and the
pointer are given back at the end.
"""
import argparse
import os
import pathlib
import subprocess
import sys
import time

import win32
from real_input_win32 import VK_LCONTROL, VK_LMENU, RealInput, foreground

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=pathlib.Path, required=True)
parser.add_argument('--output', type=pathlib.Path)
args = parser.parse_args()
build = args.build.resolve()
if sys.platform != 'win32':
    print('SKIP: Windows only')
    sys.exit(77)
output = (args.output or build / 'smoke-evidence' / 'windows-daemon').resolve()
output.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
for name in ['GLYPHSHIFT_CONFIG', 'GLYPHSHIFT_PROFILE', 'GLYPHSHIFT_PRESET', 'GLYPHSHIFT_LOG',
             'GLYPHSHIFT_FIXTURE_NO_FOCUS', 'GLYPHSHIFT_OVERLAY_NO_FOCUS',
             'GLYPHSHIFT_FIXTURE_MULTI', 'GLYPHSHIFT_FIXTURE_SCROLL']:
    env.pop(name, None)
suffix = f' daemon-{os.getpid()}'
env.update(APPDATA=str(output / 'config'), GLYPHSHIFT_FIXTURE_TITLE_SUFFIX=suffix,
           GLYPHSHIFT_FIXTURE_INPUT_LOG='1', GLYPHSHIFT_NO_NOTIFICATIONS='1',
           VK_LOADER_LAYERS_DISABLE='VK_LAYER_OBS_HOOK,VK_LAYER_RTSS,'
                                    'VK_LAYER_VALVE_steam_overlay,VK_LAYER_VALVE_steam_fossilize')
daemon_log = output / 'daemon.log'
fixture_log = output / 'fixture.log'


def eventually(condition, message, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(.1)
    raise AssertionError(message + '\n' + daemon_log.read_text(errors='replace'))


def overlays():
    return win32.children(daemon.pid)


def changed():
    """Share of the fixture's client area that differs on screen from its own
    pixels."""
    width, height, own = win32.image(window)
    x, y = win32.client_origin(window)
    _, _, seen = win32.screen(x, y, width, height)
    count = sum(1 for i in range(0, len(own), 3)
                if max(abs(own[i + k] - seen[i + k]) for k in range(3)) > 40)
    return count / (width * height)


def hotkey():
    control.key(ord('G'), (VK_LCONTROL, VK_LMENU))


def fixture_input():
    return [line for line in fixture_log.read_text(errors='replace').splitlines()
            if line.startswith('Fixture input:')]


def center():
    x, y = win32.client_origin(window)
    width, height = win32.client_size(window)
    return x + width // 2, y + height // 2


daemon = fixture = control = None
try:
    with daemon_log.open('w') as stream:
        daemon = subprocess.Popen([str(build / 'glyphshift-overlay'), '--preset', 'art-comic',
                                   '--log'], env=env, stdout=stream, stderr=subprocess.STDOUT)
    eventually(lambda: 'running;' in daemon_log.read_text() or
               'already running' in daemon_log.read_text(), 'daemon did not start')
    if 'already running' in daemon_log.read_text():
        raise SystemExit('stop your own GlyphShift daemon first: only one runs per session')
    with fixture_log.open('w') as stream:
        fixture = subprocess.Popen([str(build / 'glyphshift-vulkan-fixture'), '--validation',
                                    '--fixed-time', '1.25', '--frames', '100000'], env=env,
                                   stdout=stream, stderr=subprocess.STDOUT)
    window = win32.wait_window('GlyphShift Vulkan fixture' + suffix, fixture, fixture_log)
    win32.move(window, 80, 80)
    assert win32.resize(window, 800, 600), win32.client_size(window)
    control = RealInput(window)
    time.sleep(1)
    before = changed()
    print(f'unattached: {before:.3%} of the window differs on screen')
    assert before < .01, before

    hotkey()
    eventually(lambda: len(overlays()) == 1 and 'attached' in daemon_log.read_text(),
               'hotkey did not attach an overlay')
    eventually(lambda: changed() > .2, 'the attached overlay does not change the screen')
    print(f'attached: {changed():.1%} differs; overlay pid {overlays()[0]}')

    # Input reaches the game through the overlay, which stays in the background.
    seen = len(fixture_input())
    control.key(ord('A'))
    control.click(*center())
    eventually(lambda: fixture_input()[seen:] == [
        'Fixture input: key down 65', 'Fixture input: key up 65',
        'Fixture input: button down 1', 'Fixture input: button up 1'],
        f'input did not reach the fixture: {fixture_input()[seen:]}')
    control.guard()
    print('attached: a key and a click reached the fixture; it kept the foreground')

    hotkey()
    eventually(lambda: not overlays(), 'hotkey did not detach the overlay')
    eventually(lambda: changed() < .01, 'the screen did not return to the original')
    print('detached: screen matches the window again')

    hotkey()
    eventually(lambda: len(overlays()) == 1, 'hotkey did not attach again')
    eventually(lambda: changed() > .2, 'the overlay attached again does not change the screen')
    print('attached again')

    # The icon's menu, opened through its callback message as Explorer does
    # after a click; the daemon takes the foreground for it.
    tray = win32.window_of('GlyphShiftTray', daemon.pid)
    rect = win32.icon_rect(tray) or (0, 0, 0, 0)
    anchor = ((rect[1] + rect[3]) // 2 << 16) | ((rect[0] + rect[2]) // 2 & 0xffff)

    def open_menu():
        control.allow(tray, daemon.pid)
        win32.post(tray, win32.WM_APP + 1, anchor, win32.WM_CONTEXTMENU | 1 << 16)
        found = []
        eventually(lambda: found.append(win32.window_of('#32768', daemon.pid, True)) or
                   found[-1], 'the menu did not open')
        eventually(lambda: foreground() == tray, 'the daemon did not take the foreground')
        time.sleep(.2)
        return found[-1]

    menu = open_menu()
    rows = [label for label, _ in win32.menu_rows(menu) if label]
    detach = next((label for label in rows if label.startswith('Detach from ')), None)
    assert detach == 'Detach from GlyphShift Vulkan fixture' + suffix, rows
    # A click elsewhere (on the game) closes the menu without a choice.
    control.click(*center())
    eventually(lambda: not win32.visible(menu), 'a click elsewhere did not close the menu')
    time.sleep(.5)
    assert len(overlays()) == 1, 'the click outside the menu detached the game'
    print('menu: lists "Detach from" the fixture; a click elsewhere closes it')
    control.activate(window)
    menu = open_menu()
    for _ in range(30):
        if any(name == detach and state & win32.MF_HILITE
               for name, state in win32.menu_rows(menu)):
            break
        control.key(win32.VK_DOWN)
    else:
        raise AssertionError(f'{detach} was never highlighted')
    control.key(win32.VK_RETURN)
    eventually(lambda: not overlays(), '"Detach from" did not detach the game')
    eventually(lambda: changed() < .01, 'the screen did not return after the menu')
    print('menu: "Detach from" detached the game')

    control.activate(window)
    hotkey()
    eventually(lambda: len(overlays()) == 1, 'hotkey did not attach after the menu')
    eventually(lambda: changed() > .2, 'the overlay attached after the menu does not draw')
    control.close()
    control = None
    win32.close(window)
    assert fixture.wait(timeout=10) == 0, fixture_log.read_text(errors='replace')
    assert 'Vulkan validation:' not in fixture_log.read_text(errors='replace')
    eventually(lambda: not overlays(), 'the overlay did not end with its game')
    assert daemon.poll() is None, daemon_log.read_text()
    print('game closed: its overlay ended; daemon still running')

    assert win32.request_stop(daemon.pid)
    assert daemon.wait(timeout=10) == 0, daemon_log.read_text()
    print('daemon stopped cleanly')
finally:
    if control:
        control.close()
    for process in (fixture, daemon):
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
print('PASS: overlay daemon hotkey')
