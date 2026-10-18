"""Focused overlay daemon check: the global hotkey attaches and detaches the overlay.

Uses real XTest keyboard input on a focused GLX fixture started without the
launcher, so it takes over the desktop keyboard for a few seconds.
"""
import argparse
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import time
from real_input import RealInput
from x11 import X11

XK_G, XK_CONTROL_L, XK_ALT_L = 0x67, 0xffe3, 0xffe9

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
for name in ['LD_PRELOAD', 'GLYPHSHIFT_CONFIG', 'GLYPHSHIFT_PROFILE', 'GLYPHSHIFT_FIXTURE_NO_FOCUS']:
    env.pop(name, None)
suffix = f' daemon-{os.getpid()}'
title = 'GlyphShift GLX fixture' + suffix
env.update(XDG_CONFIG_HOME=str(output / 'config'), GLYPHSHIFT_FIXTURE_TITLE_SUFFIX=suffix,
           GLYPHSHIFT_FIXTURE_FLOATING='1')


def eventually(condition, message, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(.1)
    raise AssertionError(message + '\n' + daemon_log.read_text())


def children():
    path = pathlib.Path(f'/proc/{daemon.pid}/task/{daemon.pid}/children')
    return [int(pid) for pid in path.read_text().split()] if path.exists() else []


def changed():
    # Fraction of the fixture's on-screen area that differs from its own pixels.
    w, h, own = x.image(window)
    _, _, seen = x.screen(window)
    count = sum(1 for i in range(0, len(own), 3)
                if max(abs(own[i + k] - seen[i + k]) for k in range(3)) > 40)
    return count / (w * h)


def hotkey():
    control.key(XK_G, (XK_CONTROL_L, XK_ALT_L))


daemon_log = output / 'daemon.log'
fixture_log = output / 'fixture.log'
previous = RealInput.read_focus(x)
daemon = fixture = control = None
try:
    with daemon_log.open('w') as stream:
        daemon = subprocess.Popen([str(build / 'glyphshift-overlay'), '--preset', 'art-comic',
                                   '--log'], env=env, stdout=stream, stderr=subprocess.STDOUT)
    eventually(lambda: 'running;' in daemon_log.read_text(), 'daemon did not start')
    with fixture_log.open('w') as stream:
        fixture = subprocess.Popen([str(build / 'glyphshift-glx-fixture'), '--fixed-time', '1.25'],
                                   env=env, stdout=stream, stderr=subprocess.STDOUT)
    eventually(lambda: x.windows(title), 'fixture window did not appear')
    window = x.windows(title)[0]
    control = RealInput(x, window, previous)
    # Focus through i3 as a user would: the hotkey's grab ends with a FocusIn
    # that makes i3 restore the window it believes is focused.
    if shutil.which('i3-msg'):
        subprocess.run(['i3-msg', f'[id={window}] focus'], stdout=subprocess.DEVNULL, check=True)
    time.sleep(1)
    control.guard()
    before = changed()
    print(f'unattached: {before:.3%} of the window differs on screen')
    assert before < .01, before

    hotkey()
    eventually(lambda: len(children()) == 1 and 'attached' in daemon_log.read_text(),
               'hotkey did not attach an overlay')
    eventually(lambda: changed() > .2, 'the attached overlay does not change the screen')
    print(f'attached: {changed():.1%} differs; overlay pid {children()[0]}')

    hotkey()
    eventually(lambda: not children(), 'hotkey did not detach the overlay')
    eventually(lambda: changed() < .01, 'the screen did not return to the original')
    print('detached: screen matches the window again')

    hotkey()
    eventually(lambda: len(children()) == 1, 'hotkey did not attach again')
    x.close(window)
    assert fixture.wait(timeout=10) == 0, fixture_log.read_text()
    eventually(lambda: not children(), 'overlay did not end with its game')
    assert daemon.poll() is None, daemon_log.read_text()
    print('game closed: its overlay ended; daemon still running')

    daemon.send_signal(signal.SIGTERM)
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
