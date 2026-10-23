#!/usr/bin/env python3
"""Records the README animation on Windows: a running game, first as it is,
then with the capture overlay attached, switching included styles live while
the camera turns slowly.

Uses SendInput (it brings the game to the front and moves the mouse to turn
the camera), so run it only when nobody uses the keyboard and pointer; the
foreground and pointer are given back at the end. Needs Pillow. Writes an
animated WebP:

    python tools/record_demo_win32.py --build build/windows-release \\
        --running eldenring.exe --output docs/demo.webp
"""
import argparse
import os
import pathlib
import subprocess
import sys
import tempfile
import time

from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tests/smoke'))
import win32  # noqa: E402
import real_input_win32  # noqa: E402
from real_input_win32 import MOUSEEVENTF_MOVE, RealInput  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build', type=pathlib.Path, required=True)
parser.add_argument('--running', required=True, metavar='EXE')
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--width', type=int, default=640, help='Width of the animation')
parser.add_argument('--fps', type=int, default=12)
parser.add_argument('--turn', type=int, default=2, help='Mouse motion per 1/60 s (camera turn)')
args = parser.parse_args()
presets = pathlib.Path(__file__).resolve().parents[1] / 'presets'
# (seconds from the start, style config or None for the game as it is)
timeline = [(0, None), (2.5, 'art-true-color'), (5.5, 'art-digital-rain'), (8.5, 'art-crt'),
            (11.5, 'art-midnight')]
length = 14.5

pids = win32.processes(args.running)
window = next(filter(None, map(win32.main_window, pids)), None)
if not window:
    raise SystemExit(f'{args.running} is not running with a visible window')
x, y = win32.client_origin(window)
width, height = win32.client_size(window)
size = (args.width, round(args.width * height / width))
with tempfile.TemporaryDirectory(prefix='glyphshift-demo-') as temporary:
    work = pathlib.Path(temporary)
    config = work / 'live.conf'
    env = dict(os.environ, GLYPHSHIFT_CONFIG=str(config), APPDATA=str(work / 'config'),
               GLYPHSHIFT_NO_NOTIFICATIONS='1')
    overlay = None
    control = RealInput(window)
    frames = []
    try:
        time.sleep(.5)
        start = time.monotonic()
        step = 0
        next_frame = start
        while (now := time.monotonic()) < start + length:
            while step < len(timeline) and now - start >= timeline[step][0]:
                style = timeline[step][1]
                step += 1
                if style is None:
                    continue
                temp = config.with_suffix('.tmp')
                temp.write_text((presets / f'{style}.conf').read_text(encoding='utf-8'),
                                encoding='utf-8')
                temp.replace(config)
                if overlay is None:
                    overlay = subprocess.Popen([str(args.build.resolve() / 'glyphshift-overlay'),
                                                '--window', str(window)], env=env,
                                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            # One small relative move per pass: a slow, steady camera turn.
            control.guard()
            event = real_input_win32._mouse_event(MOUSEEVENTF_MOVE)
            event.union.mi.dx = args.turn
            real_input_win32._send(event)
            time.sleep(1 / 60)
            if now >= next_frame:
                w, h, rgb = win32.screen(x, y, width, height)
                frames.append(Image.frombytes('RGB', (w, h), rgb).resize(size, Image.LANCZOS))
                next_frame += 1 / args.fps
    finally:
        control.close()
        if overlay and overlay.poll() is None:
            win32.request_stop(overlay.pid)
            try:
                overlay.wait(timeout=10)
            except subprocess.TimeoutExpired:
                overlay.kill()
    frames[0].save(args.output, save_all=True, append_images=frames[1:],
                   duration=round(1000 / args.fps), loop=0, quality=70, method=6)
    print(f'{args.output}: {len(frames)} frames, {args.output.stat().st_size / 1e6:.1f} MB')
