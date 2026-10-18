#!/usr/bin/env python3
"""Records the README animation: native SuperTux's animated title screen, then
the capture overlay attached to it, switching included styles live.

No keyboard or pointer input. Needs native SuperTux (Arch `supertux`), ffmpeg,
the persistent test profile (`tests/smoke/test_env.py setup`) and a built
release tree. Writes an animated WebP (or GIF, by extension):

    python3 tools/record_demo.py --build build/release --output docs/demo.webp
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tests/smoke'))
import test_env  # noqa: E402
from x11 import X11  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build', type=pathlib.Path, default=pathlib.Path('build/release'))
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--width', type=int, default=960)
parser.add_argument('--height', type=int, default=540)
parser.add_argument('--gif-width', type=int, default=560)
parser.add_argument('--fps', type=int, default=12)
args = parser.parse_args()
presets = pathlib.Path(__file__).resolve().parents[1] / 'presets'
# (seconds from the start, style config or None for the original)
timeline = [(0, None), (3, 'art-comic'), (6.5, 'art-newsprint'), (10, 'art-blueprint')]
length = 13.5

x = X11()
with tempfile.TemporaryDirectory(prefix='glyphshift-demo-') as temporary:
    work = pathlib.Path(temporary)
    config = work / 'live.conf'
    config.write_text((presets / 'art-comic.conf').read_text())
    env = dict(os.environ, GLYPHSHIFT_CONFIG=str(config), XDG_CONFIG_HOME=str(work / 'config'))
    env.pop('LD_PRELOAD', None)
    game = subprocess.Popen(test_env.supertux_native('--geometry', f'{args.width}x{args.height}'),
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    overlay = recorder = None
    try:
        deadline = time.monotonic() + 20
        while not x.windows(test_env.SUPERTUX_TITLE):
            if time.monotonic() > deadline:
                raise SystemExit('SuperTux did not open')
            time.sleep(.1)
        window = x.windows(test_env.SUPERTUX_TITLE)[0]
        if shutil.which('i3-msg'):
            subprocess.run(['i3-msg', f'[id={window}] floating enable'], stdout=subprocess.DEVNULL)
        x.resize(window, args.width, args.height)
        x.visible(window)
        time.sleep(4)  # The title screen settles.
        fn = x.lib.XTranslateCoordinates
        import ctypes as c
        fn.argtypes = [c.c_void_p, c.c_ulong, c.c_ulong, c.c_int, c.c_int, c.POINTER(c.c_int),
                       c.POINTER(c.c_int), c.POINTER(c.c_ulong)]
        left, top, child = c.c_int(), c.c_int(), c.c_ulong()
        fn(x.display, window, x.root, 0, 0, c.byref(left), c.byref(top), c.byref(child))
        video = work / 'demo.mkv'
        recorder = subprocess.Popen(
            ['ffmpeg', '-loglevel', 'error', '-y', '-f', 'x11grab', '-framerate', '30',
             '-video_size', f'{args.width}x{args.height}', '-i',
             f'{os.environ["DISPLAY"]}+{left.value},{top.value}', '-t', str(length),
             '-c:v', 'libx264rgb', '-qp', '0', '-preset', 'ultrafast', str(video)])
        start = time.monotonic()
        for at, style in timeline:
            time.sleep(max(0, start + at - time.monotonic()))
            if style is None:
                continue
            temp = config.with_suffix('.tmp')
            temp.write_text((presets / f'{style}.conf').read_text())
            temp.replace(config)
            if overlay is None:
                overlay = subprocess.Popen([str(args.build.resolve() / 'glyphshift-overlay'), '--window',
                                            str(window)], env=env, stdout=subprocess.DEVNULL,
                                           stderr=subprocess.DEVNULL)
        recorder.wait(timeout=length + 30)
        if args.output.suffix == '.webp':
            # Animated WebP: far smaller than GIF for glyph detail; browsers
            # and GitHub show it as an image.
            subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', str(video), '-vf',
                            f'fps={args.fps},scale={args.gif_width}:-1:flags=lanczos', '-c:v',
                            'libwebp_anim', '-lossless', '0', '-q:v', '70', '-loop', '0',
                            str(args.output)], check=True)
            print(f'{args.output}: {args.output.stat().st_size / 1e6:.1f} MB')
            sys.exit(0)
        palette = work / 'palette.png'
        scale = f'fps={args.fps},scale={args.gif_width}:-1:flags=lanczos'
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', str(video), '-vf',
                        f'{scale},palettegen=stats_mode=diff', str(palette)], check=True)
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', str(video), '-i', str(palette),
                        '-lavfi', f'{scale}[v];[v][1:v]paletteuse=dither=bayer:bayer_scale=4',
                        str(args.output)], check=True)
        print(f'{args.output}: {args.output.stat().st_size / 1e6:.1f} MB')
    finally:
        for process in (recorder, overlay, game):
            if process and process.poll() is None:
                process.terminate()
                process.wait(timeout=20)
        x.shutdown()
