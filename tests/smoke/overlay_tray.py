"""Overlay daemon tray icon and menu against a stand-in tray manager.

No XTest: the test owns the tray selection like a panel would, embeds the icon
the daemon offers, and drives the menu with synthetic events (XSendEvent).
While the menu is open the daemon grabs pointer and keyboard briefly, like any
popup menu. Skipped when a real tray manager or another daemon runs.
"""
import argparse
import os
import pathlib
import signal
import subprocess
import sys
import time

from tray_panel import (XK_DOWN, XK_ESCAPE, XK_RETURN, XK_UP, Panel, drawn, key,
                        manager_running, mapped, menu_window, save)
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
if manager_running(x):
    print('Tray: SKIPPED, a tray manager already runs on this display')
    sys.exit(0)


def eventually(condition, message, seconds=5):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(.05)
    raise AssertionError(message + '\n' + log.read_text())


env = dict(os.environ, XDG_CONFIG_HOME=str(output / 'config'), PATH=str(output / 'no-programs'))
env.pop('LD_PRELOAD', None)
log = output / 'daemon.log'
autostart = output / 'config/autostart/glyphshift.desktop'
panels = []
with log.open('w') as stream:
    daemon = subprocess.Popen([str(build / 'glyphshift-overlay'), '--hotkey', 'ctrl+alt+shift+F12',
                               '--log'], env=env, stdout=stream, stderr=subprocess.STDOUT)
try:
    eventually(lambda: 'running' in log.read_text(), 'daemon did not start')
    if 'already running' in log.read_text():
        print('Tray: SKIPPED, another GlyphShift daemon runs on this display')
        sys.exit(0)

    def new_panel():
        panels.append(Panel())
        return panels[-1]

    # The panel appears after the daemon: it docks then.
    panel = new_panel()
    icon = panel.dock_request()
    panel.embed(icon)
    assert drawn(x, icon), 'the tray icon is not drawn'
    # The panel restarts: a fresh icon docks into the new one (the orphaned
    # one is left to no window manager).
    panel.quit()
    time.sleep(.3)
    panel = new_panel()
    replacement = panel.dock_request()
    assert replacement != icon
    icon = replacement
    panel.embed(icon)
    assert drawn(x, icon), 'the docked icon is not drawn after the restart'
    assert len(x.windows('GlyphShift')) == 1, 'an orphaned icon window was left behind'
    # A panel that destroys the icon with itself: a new icon docks next time.
    panel.crash_with_icon()
    time.sleep(.3)
    panel = new_panel()
    replacement = panel.dock_request()
    assert replacement != icon
    icon = replacement
    panel.embed(icon)
    assert drawn(x, icon), 'the replacement icon is not drawn'
    print('Tray: the icon docks when a tray manager appears, after it restarts and after a manager '
          'destroyed it; drawn each time, no orphaned icon left')

    menu = menu_window(x)

    def open_menu():
        x.send(icon, 4, 3, 10, 10)
        eventually(lambda: mapped(x, menu), 'the menu did not open')
        time.sleep(.1)

    open_menu()
    w, h = x.geometry(menu)
    assert w > 150 and h > 200, (w, h)
    save(output / 'menu.ppm', x, menu, w, h)
    save(output / 'icon.ppm', x, icon, 24, 24)
    # Down, Down: past "Each game's saved style" to the first included style.
    key(x, menu, XK_DOWN)
    key(x, menu, XK_DOWN)
    key(x, menu, XK_RETURN)
    eventually(lambda: not mapped(x, menu), 'Return did not close the menu')
    eventually(lambda: 'newly attached games use art-comic' in log.read_text(),
               'choosing a style had no effect')
    # Up, Up from the end: "Start at login".
    open_menu()
    key(x, menu, XK_UP)
    key(x, menu, XK_UP)
    key(x, menu, XK_RETURN)
    eventually(autostart.exists, 'Start at login wrote no entry')
    text = autostart.read_text()
    assert '--hotkey ctrl+alt+shift+F12 --preset art-comic' in text, text
    # Escape and a click outside close without choosing.
    open_menu()
    key(x, menu, XK_ESCAPE)
    eventually(lambda: not mapped(x, menu), 'Escape did not close the menu')
    open_menu()
    x.send(menu, 4, 1, -20, -20)
    eventually(lambda: not mapped(x, menu), 'a click outside did not close the menu')
    # Start at login again turns it off.
    open_menu()
    key(x, menu, XK_UP)
    key(x, menu, XK_UP)
    key(x, menu, XK_RETURN)
    eventually(lambda: not autostart.exists(), 'Start at login did not remove the entry')
    print('Tray: menu opens on the icon; style and start-at-login choices apply; Escape and '
          'clicks outside close it')
    open_menu()
    key(x, menu, XK_UP)
    key(x, menu, XK_RETURN)
    assert daemon.wait(timeout=10) == 0, log.read_text()
    print('Tray: Quit ends the daemon cleanly')
finally:
    if daemon.poll() is None:
        daemon.send_signal(signal.SIGTERM)
        daemon.wait(timeout=10)
    for panel in panels:
        panel.quit()
    x.shutdown()
