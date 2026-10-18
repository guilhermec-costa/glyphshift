#!/usr/bin/env python3
"""Focused X11 editor acceptance test, including private-Xlib and Proton apps."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from real_input import RealInput
from x11 import X11


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build/dev'))
    parser.add_argument('--application', choices=['glx-fixture', 'vulkan-fixture', 'glxgears', 'vkcube', 'supertux'], required=True)
    parser.add_argument('--apps', type=Path, default=Path('/tmp/glyphshift-m5-apps'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--illustrated', action='store_true', help='Enable the new rendering mode through the actual panel')
    parser.add_argument('--level', action='store_true', help='Use the previously prepared offline SuperTux test level')
    parser.add_argument('--presets', action='store_true', help='Select every included style and save personal copies through the overlay')
    args = parser.parse_args()
    args.build = args.build.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    config = out / 'live.conf'
    config.write_text('color_mode=mono\nglyph_strength=1\n')
    env = os.environ.copy()
    env.update(GLYPHSHIFT_CONFIG=str(config), XDG_CONFIG_HOME=str(out / 'config'),
               GLYPHSHIFT_LOG='1', GLYPHSHIFT_FIXTURE_PRIVATE_XLIB='1',
               GLYPHSHIFT_FIXTURE_INPUT_LOG='1', GLYPHSHIFT_FIXTURE_FLOATING='1',
               GLYPHSHIFT_FIXTURE_STATE_CHECK='1', GLYPHSHIFT_FIXTURE_GL_DEBUG='1')
    env.pop('GLYPHSHIFT_FIXTURE_NO_FOCUS', None)
    app = args.application
    if app.endswith('fixture'):
        target = [str(args.build / ('glyphshift-' + app)), '--fixed-time', '1.25']
        title = 'GlyphShift ' + ('GLX' if app == 'glx-fixture' else 'Vulkan') + ' fixture'
        if app == 'vulkan-fixture':
            target.append('--validation')
    elif app == 'glxgears':
        target = [str(args.apps / 'root/usr/bin/glxgears'), '-geometry', '800x600+40+40']
        title = 'glxgears'
    elif app == 'vkcube':
        target = [str(args.apps / 'root/usr/bin/vkcube'), '--wsi', 'xlib', '--validate', '--width', '800', '--height', '600']
        title = None
    else:
        # Run this script inside the documented, authorized network-blocking
        # wrapper. It neither downloads software nor weakens that restriction.
        env.update(STEAM_COMPAT_DATA_PATH='/tmp/glyphshift-m5-proton-prefix',
                   STEAM_COMPAT_CLIENT_INSTALL_PATH='/tmp/glyphshift-m5-steam-empty',
                   PROTONFIXES_DISABLE='1', WINEDEBUG='-all', PROTON_LOG='0',
                   SteamGameId='0', SDL_AUDIODRIVER='dummy')
        target = ['/usr/bin/python3', str(args.apps / 'GE-Proton11-9-x86_64/proton'), 'run',
                  str(args.apps / 'supertux/SuperTux-portable/bin/supertux2.exe'),
                  '--userdir', r'Z:\tmp\glyphshift-m5-supertux-user',
                  '--window', '--geometry', '800x600', '--disable-sound', '--disable-music']
        if args.level:
            target.append('glyphshift-m5-test.stl')
        title = 'SuperTux v0.7.0'
    command = [str(args.build / 'glyphshift'), '--profile', 'editor-' + app, '--', *target]
    x = X11()
    previous_focus = RealInput.read_focus(x)
    control = None
    path = out / 'application.log'
    with path.open('w') as log:
        process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            window = None
            for _ in range(400):
                text = path.read_text()
                found = re.search(r'editor X11 input surface=0x[0-9a-f]+ client=0x([0-9a-f]+)', text)
                if found:
                    window = int(found.group(1), 16)
                    break
                assert process.poll() is None, text
                time.sleep(.1)
            assert window, path.read_text()
            if title:
                assert window in x.windows(title), (title, window, x.windows(title))
            if shutil.which('i3-msg'):
                subprocess.run(['i3-msg', f'[id={window}] floating enable'], stdout=subprocess.DEVNULL, check=True)
            x.resize(window, 800, 600)
            control = RealInput(x, window, previous_focus)
            time.sleep(2)

            def capture(label, editor):
                time.sleep(.3)
                assert process.poll() is None, path.read_text()
                w, h, rgb = x.image(window)
                (out / (label + '.ppm')).write_bytes(f'P6\n{w} {h}\n255\n'.encode() + rgb)
                colors = len(set(zip(rgb[0::3], rgb[1::3], rgb[2::3])))
                chromatic = any(r != g or g != b for r, g, b in zip(rgb[0::3], rgb[1::3], rgb[2::3]))
                assert chromatic == editor, (label, colors, chromatic)
                print(f'{app}: {label} {w}x{h} colors={colors}', flush=True)

            capture('closed', False)
            control.key(0xff63)
            capture('opened', True)
            if args.presets:
                profile_dir = out / 'config/glyphshift/profiles' / ('editor-' + app)
                def choose(position, expected):
                    control.click(140, 95)
                    time.sleep(.2)
                    if position == 1:
                        capture('included-preset-menu', True)
                    if position < 0:
                        capture('personal-preset-menu', True)
                        saved_count = len(list(profile_dir.glob('*.conf')))
                        # The "Saved presets" heading follows the last included row.
                        first_saved = 174 + (len(names)-1)*23 + 51
                        control.click(100, first_saved + (saved_count-1)*23)
                    else:
                        # The grouped popup has one default row, a heading,
                        # then one selectable row per included style.
                        control.click(100, 123 if position == 0 else 174 + (position-1)*23)
                    time.sleep(.35)
                    assert (profile_dir / 'active').read_text() == expected + '\n', (expected, (profile_dir / 'active').read_text())
                names = ['art-soft-color', 'art-newsprint', 'art-green-terminal', 'art-blueprint', 'art-amber-braille', 'art-mosaic',
                         'art-comic', 'art-sepia', 'art-chalk', 'art-midnight', 'art-crt', 'art-digital-rain']
                seen = []
                for index, name in enumerate(names, 1):
                    choose(index, name)
                    capture(name + '-selected', True)
                    w, h, rgb = x.image(window)
                    # Compare only the scene to the right of the panel, so
                    # changed labels alone cannot satisfy live-render checks.
                    scene = b''.join(rgb[(y*w+400)*3:(y*w+w)*3] for y in range(h))
                    assert all(scene != previous for previous in seen), name
                    seen.append(scene)
                    # Save copies of the first and last styles only, so the
                    # saved section stays within the popup without scrolling.
                    if index not in (1, len(names)):
                        continue
                    control.click(290, 124)
                    saved_copy = profile_dir / (name + '-copy.conf')
                    assert saved_copy.exists(), 'Included style did not propose a personal copy'
                    expected = dict(line.split('=', 1) for line in (Path(__file__).resolve().parents[2] / 'presets' / (name + '.conf')).read_text().splitlines() if '=' in line and not line.startswith('#'))
                    actual = dict(line.split('=', 1) for line in saved_copy.read_text().splitlines() if '=' in line)
                    for key, value in expected.items():
                        if value != actual[key]:
                            assert all(abs(float(a)-float(b)) < 1e-6 for a,b in zip(value.split(','),actual[key].split(','))), (name,key,value,actual[key])
                # Included styles and the saved copies coexist in the list; the
                # last saved row alphabetically is the soft color copy.
                choose(-1, 'art-soft-color-copy')
                capture('personal-copy-selected', True)
                choose(0, 'default')
                # A changed external config must still override an explicit
                # selection; an unchanged one must not undo it every frame.
                config.write_text('color_mode=mono\nglyph_strength=1\ncell_width=9\n')
                time.sleep(.35)
                config.write_text('color_mode=mono\nglyph_strength=1\n')
                time.sleep(.35)
                print(f'{app}: {len(names)} included styles, distinct live scene images and saved copies passed', flush=True)
            if args.illustrated:
                control.click(100, 388)  # Illustration section, after Reset.
                capture('illustration-controls', True)
                control.click(30, 416)   # Enable illustrated rendering.
                time.sleep(.5)
                capture('illustration-enabled', True)
                control.click(30, 505)   # Enable matching through the actual checkbox.
                time.sleep(.5)
                capture('shape-matching-enabled', True)
                control.click(100, 388)
                capture('illustrated-opened', True)
            # Actual mouse + keyboard numeric edit, then save a named preset.
            control.click(100, 214, 0xffe3)
            control.key(ord('a'), 0xffe3)
            control.text('21')
            control.key(0xff0d)
            control.click(100, 330)
            control.click(100, 359)
            control.click(100, 456)  # Binary set in the actual combo popup.
            control.click(100, 524)  # Apply characters.
            control.click(100, 330)
            control.click(100, 124)
            control.key(ord('a'), 0xffe3)
            control.text('app-editor')
            control.click(290, 124)
            saved = out / 'config/glyphshift/profiles' / ('editor-' + app) / 'app-editor.conf'
            assert saved.exists(), 'Save preset did not create a file'
            assert 'cell_width=21\n' in saved.read_text(), saved.read_text()
            assert 'character_set=binary\n' in saved.read_text(), saved.read_text()
            if args.illustrated:
                assert 'render_style=illustrated\n' in saved.read_text(), saved.read_text()
                assert 'shape_matching=true\n' in saved.read_text(), saved.read_text()
            capture('edited-saved', True)
            control.key(0xff1b)
            capture('escape-closed', False)
            control.key(0xffc7)
            time.sleep(.3)
            _, _, original = x.image(window)
            assert len(set(zip(original[0::3], original[1::3], original[2::3]))) > 2, 'F10 failed'
            control.key(0xffc7)
            capture('f10-restored', False)
            control.key(0xff63)
            capture('reopened', True)
            for index, size in enumerate([(640, 480), (1000, 300), (800, 600)]):
                x.resize(window, *size)
                capture('open-resize-' + str(index), True)
            # Focus a separate test-owned window, not the user's application.
            control.set_focus(control.sentinel)
            capture('focus-loss-closed', False)
            control.set_focus(window)
            control.key(0xff63)
            capture('focus-restored-opened', True)
            control.click(344, 18)
            capture('title-close', False)
            if app == 'supertux' and args.level:
                # Prove that input actually returns to Wine: Enter must advance
                # the level introduction into the colored world before reopening.
                control.key(0xff0d)
                time.sleep(2)
                config.write_text('enabled=false\n')
                time.sleep(.3)
                w, h, original = x.image(window)
                (out / 'world-original.ppm').write_bytes(f'P6\n{w} {h}\n255\n'.encode() + original)
                if args.illustrated:
                    config.write_text((Path(__file__).resolve().parents[2] / 'presets/illustrated.conf').read_text())
                else:
                    config.write_text('color_mode=source\nglyph_strength=0.35\ncell_width=6\ncell_height=10\n')
                time.sleep(.3)
                w, h, styled = x.image(window)
                (out / 'world-styled.ppm').write_bytes(f'P6\n{w} {h}\n255\n'.encode() + styled)
                time.sleep(.3)
                control.key(0xff63)
                time.sleep(.3)
                w, h, rgb = x.image(window)
                (out / 'readable-play-editor.ppm').write_bytes(f'P6\n{w} {h}\n255\n'.encode() + rgb)
                scene = b''.join(rgb[(row * w + 400) * 3:(row + 1) * w * 3] for row in range(h))
                assert sum(r != g or g != b for r, g, b in zip(scene[0::3], scene[1::3], scene[2::3])) > 1000, 'Game did not advance after editor closed'
                control.key(0xff1b)
                print('supertux: game input restored; readable editor captured over the running level', flush=True)
            control.key(ord('a'))
            control.click(500, 450)
            if app.endswith('fixture'):
                text = path.read_text()
                assert 'Fixture input: key down 97' in text, 'Keyboard not restored'
                assert 'Fixture input: button down 1' in text, 'Pointer not restored'
                assert 'Fixture input: key down 65307' not in text, 'UI Escape leaked'
                assert 'Fixture input: key down 50' not in text, 'UI typing leaked'
                assert text.count('Fixture input: button down 1') == 1, 'UI mouse clicks leaked'
                events = [line for line in text.splitlines() if line.startswith('Fixture input:')]
                assert events == ['Fixture input: key down 97', 'Fixture input: key up 97',
                                  'Fixture input: button down 1', 'Fixture input: button up 1'], events
                control.key(0xff1b)
            elif app == 'glxgears':
                control.key(0xff1b)
            else:
                x.close(window)
            assert process.wait(timeout=20) == 0, path.read_text()
            text = path.read_text()
            for error in ['VUID-', 'Validation Error', 'Vulkan validation:', 'editor input:', 'OpenGL error', 'state mismatch']:
                assert error not in text, (error, text)
            print(f'{app}: editor mouse/keyboard/save/hotkeys/resize/focus/clean exit passed', flush=True)
        finally:
            if control:
                control.close()
            if process.poll() is None:
                if app == 'supertux':
                    subprocess.run([str(args.apps / 'GE-Proton11-9-x86_64/files/bin/wineserver'), '-k'], env=dict(env, WINEPREFIX=env['STEAM_COMPAT_DATA_PATH'] + '/pfx'), check=True)
                else:
                    process.terminate()
                process.wait(timeout=15)
            x.shutdown()


if __name__ == '__main__':
    main()
