#!/usr/bin/env python3
"""Overlay latency and GPU cost against the launcher. No keyboard or pointer input.

latency: the GLX fixture (GLYPHSHIFT_FIXTURE_LATENCY) alternates whole black
and white frames and records the monotonic time just before each phase's first
swap. The composited screen at the window's center is polled with XGetImage;
each change of phase on screen is matched to the swap that started it. Modes:
direct (no GlyphShift), launcher (injected) and overlay (attached by window ID).

cost: native SuperTux on its title screen, alone, through the launcher and with
the overlay attached, with GLYPHSHIFT_GPU_TIMING=1 (GPU time of GlyphShift's
passes from GL timestamp queries) and nvidia-smi samples of the whole GPU.
"""
import argparse
import ctypes as c
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import time

import test_env
from x11 import X11


def percentile(values, share):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * share))]


def wait_window(x, title, process, log, seconds=20):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        found = x.windows(title)
        if found:
            return found[0]
        if process.poll() is not None:
            raise RuntimeError(Path(log).read_text())
        time.sleep(.05)
    raise RuntimeError('window did not appear: ' + title)


def root_position(x, window):
    fn = x.lib.XTranslateCoordinates
    fn.argtypes = [c.c_void_p, c.c_ulong, c.c_ulong, c.c_int, c.c_int, c.POINTER(c.c_int),
                   c.POINTER(c.c_int), c.POINTER(c.c_ulong)]
    px, py, child = c.c_int(), c.c_int(), c.c_ulong()
    fn(x.display, window, x.root, 0, 0, c.byref(px), c.byref(py), c.byref(child))
    return px.value, py.value


def brightness(x, left, top):
    """Mean of an 8x8 block of the composited screen, and the poll's midpoint time."""
    before = time.monotonic_ns()
    image = x.lib.XGetImage(x.display, x.root, left, top, 8, 8, c.c_ulong(-1).value, 2)
    after = time.monotonic_ns()
    if not image:
        raise RuntimeError('XGetImage failed on the screen')
    try:
        info = image.contents
        raw = c.string_at(info.data, info.bytes_per_line * 8)
        return sum(raw[row * info.bytes_per_line + i] for row in range(8)
                   for i in range(32) if i % 4 != 3) / 192, (before + after) // 2, after - before
    finally:
        x.lib.XDestroyImage(image)


def latency_mode(x, args, mode):
    out = args.output / mode
    out.mkdir(parents=True, exist_ok=True)
    flashes = out / 'flashes.log'
    env = os.environ.copy()
    for name in ['LD_PRELOAD', 'GLYPHSHIFT_PROFILE', 'GLYPHSHIFT_LOG', 'GLYPHSHIFT_GPU_TIMING']:
        env.pop(name, None)
    suffix = f' latency-{mode}-{os.getpid()}'
    env.update(GLYPHSHIFT_CONFIG=str(args.config), XDG_CONFIG_HOME=str(out / 'config'),
               GLYPHSHIFT_FIXTURE_LATENCY=str(flashes), GLYPHSHIFT_FIXTURE_TITLE_SUFFIX=suffix,
               GLYPHSHIFT_FIXTURE_NO_FOCUS='1', GLYPHSHIFT_FIXTURE_FLOATING='1')
    fixture = [str(args.build / 'glyphshift-glx-fixture')]
    command = fixture if mode != 'launcher' else [
        str(args.build / 'glyphshift'), '--profile', 'measure-latency', '--', *fixture]
    log = out / 'fixture.log'
    overlay = None
    with log.open('w') as stream:
        process = subprocess.Popen(command, env=env, stdout=stream, stderr=subprocess.STDOUT)
    try:
        window = wait_window(x, 'GlyphShift GLX fixture' + suffix, process, log)
        x.resize(window, 800, 600)
        x.visible(window)
        if mode == 'overlay':
            with (out / 'overlay.log').open('w') as stream:
                overlay = subprocess.Popen([str(args.build / 'glyphshift-overlay'), '--window',
                                            str(window), '--log'], env=env, stdout=stream,
                                           stderr=subprocess.STDOUT)
        time.sleep(2)
        left, top = root_position(x, window)
        left, top = left + 396, top + 296
        # Calibrate both phases on screen, then classify against their midpoint.
        levels, until = [], time.monotonic() + 1.5
        while time.monotonic() < until:
            levels.append(brightness(x, left, top)[0])
        low, high = min(levels), max(levels)
        if high - low < 40:
            raise RuntimeError(f'{mode}: no visible flashes (levels {low:.0f}..{high:.0f})')
        threshold = (low + high) / 2
        # Record only changes of the on-screen phase; each flash lasts 500 ms.
        changes, polls = [], 0
        state = brightness(x, left, top)[0] > threshold
        until = time.monotonic() + args.samples * .5 + 1
        while time.monotonic() < until:
            level, moment, _ = brightness(x, left, top)
            polls += 1
            now = level > threshold
            if now != state:
                changes.append((int(now), moment))
                state = now
        changes.append((-1, time.monotonic_ns()))
    finally:
        x.close(window)
        process.wait(timeout=10)
        if overlay:
            overlay.wait(timeout=10)
    swaps = [tuple(map(int, line.split())) for line in flashes.read_text().splitlines()]
    # Before each swap both compositor buffers showed the other phase for
    # 500 ms, so the first poll showing the new phase is the new frame. The
    # screen pixmap read by XGetImage can still return the previous buffer of
    # a page flip once; a phase is stable when it then lasts at least 30 ms.
    stable = [(phase, start) for (phase, start), (_, end) in zip(changes, changes[1:])
              if end - start >= 30_000_000]
    first, settled = [], []
    for phase, swap in swaps:
        for found, runs in ((first, changes), (settled, stable)):
            later = [start for p, start in runs if p == phase and start >= swap]
            if later and later[0] - swap < 200_000_000:
                found.append((later[0] - swap) / 1e6)
    if len(first) < args.samples * .8 or len(settled) < len(first) * .8:
        raise RuntimeError(f'{mode}: matched {len(first)}/{len(settled)} of {len(swaps)} flashes')
    seconds = args.samples * .5 + 1
    summary = (f'{mode}: {len(first)} flashes; first shown median {statistics.median(first):.2f} '
               f'ms, p95 {percentile(first, .95):.2f}, min {min(first):.2f}, max {max(first):.2f}; '
               f'stable median {statistics.median(settled):.2f} ms; '
               f'{len(changes) - 1 - len(stable)} short reversions; '
               f'screen polled every {seconds / polls * 1e3:.3f} ms')
    results = first
    (out / 'latency.txt').write_text('\n'.join(f'{value:.3f}' for value in results) + '\n')
    print(summary, flush=True)
    return statistics.median(results)


def sample_gpu(seconds):
    if not shutil.which('nvidia-smi'):
        return None
    process = subprocess.run(['timeout', str(seconds), 'nvidia-smi', '--query-gpu='
                              'utilization.gpu,power.draw,clocks.gr', '--format=csv,noheader,nounits',
                              '-lms', '250'], capture_output=True, text=True)
    rows = [[float(v) for v in line.split(',')] for line in process.stdout.splitlines()
            if line.strip() and 'N/A' not in line]
    if not rows:
        return None
    return [statistics.mean(column) for column in zip(*rows)]


def cost_mode(x, args, mode):
    out = args.output / mode
    out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    for name in ['LD_PRELOAD', 'GLYPHSHIFT_PROFILE', 'GLYPHSHIFT_LOG']:
        env.pop(name, None)
    env.update(GLYPHSHIFT_CONFIG=str(args.config), XDG_CONFIG_HOME=str(out / 'config'),
               GLYPHSHIFT_GPU_TIMING='1')
    game = test_env.supertux_native('--geometry', args.size)
    command = game if mode != 'launcher' else [
        str(args.build / 'glyphshift'), '--profile', 'measure-cost', '--', *game]
    log = out / 'game.log'
    overlay = None
    width, height = map(int, args.size.split('x'))
    with log.open('w') as stream:
        process = subprocess.Popen(command, env=env, stdout=stream, stderr=subprocess.STDOUT)
    try:
        window = wait_window(x, test_env.SUPERTUX_TITLE, process, log)
        if shutil.which('i3-msg'):
            subprocess.run(['i3-msg', f'[id={window}] floating enable'], stdout=subprocess.DEVNULL)
        x.resize(window, width, height)
        x.visible(window)
        if mode == 'overlay':
            with (out / 'overlay.log').open('w') as stream:
                overlay = subprocess.Popen([str(args.build / 'glyphshift-overlay'), '--window',
                                            str(window), '--log'], env=env, stdout=stream,
                                           stderr=subprocess.STDOUT)
        time.sleep(5)
        gpu = sample_gpu(args.seconds)
    finally:
        x.close(window)
        process.wait(timeout=20)
        if overlay:
            overlay.wait(timeout=10)
    text = log.read_text() + ((out / 'overlay.log').read_text() if overlay else '')
    summary = f'{mode} {args.size}: '
    if gpu:
        summary += f'whole GPU {gpu[0]:.1f}% busy, {gpu[1]:.1f} W, {gpu[2]:.0f} MHz'
    print(summary, flush=True)
    # The last report of each kind: GPU time of GlyphShift, overlay frame rate.
    for marker in ('glyphshift: GPU', 'glyphshift-overlay:'):
        found = [line for line in text.splitlines() if line.startswith(marker)]
        if found:
            print('  ' + found[-1], flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('what', choices=['latency', 'cost'])
    parser.add_argument('--build', type=Path, default=Path('build/release'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True, help='Live config for the effect')
    parser.add_argument('--modes', default='direct,launcher,overlay')
    parser.add_argument('--samples', type=int, default=80, help='Flashes per latency mode')
    parser.add_argument('--seconds', type=int, default=20, help='GPU sampling per cost mode')
    parser.add_argument('--size', default='1280x720', help='SuperTux window size for cost')
    args = parser.parse_args()
    if not os.getenv('DISPLAY'):
        print('SKIP: DISPLAY is not set')
        sys.exit(77)
    args.build = args.build.resolve()
    args.output = args.output.resolve()
    args.config = args.config.resolve()
    x = X11()
    for mode in args.modes.split(','):
        if args.what == 'latency':
            latency_mode(x, args, mode)
        else:
            cost_mode(x, args, 'game' if mode == 'direct' else mode)


if __name__ == '__main__':
    main()
