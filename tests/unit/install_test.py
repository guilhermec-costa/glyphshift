"""cmake --install: layout, desktop entry, and that the installed programs run."""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

cmake, build = sys.argv[1], pathlib.Path(sys.argv[2])
with tempfile.TemporaryDirectory(prefix='glyphshift-install-') as temporary:
    prefix = pathlib.Path(temporary) / 'prefix with space'
    subprocess.run([cmake, '--install', str(build), '--prefix', str(prefix)], check=True,
                   stdout=subprocess.DEVNULL)
    private = prefix / 'lib/glyphshift'
    for name in ['glyphshift', 'glyphshift-overlay', 'libglyphshift_gl.so', 'libglyphshift_vk.so',
                 'libglyphshift_editor.so', 'layers/VK_LAYER_GLYPHSHIFT_overlay.json']:
        assert (private / name).is_file(), name
    # The layer manifest's relative library path must resolve after installation.
    assert (private / 'layers/../libglyphshift_vk.so').is_file()
    for name in ['glyphshift', 'glyphshift-overlay']:
        link = prefix / 'bin' / name
        assert link.is_symlink() and link.resolve() == (private / name).resolve(), name
    entry = prefix / 'share/applications/glyphshift.desktop'
    text = entry.read_text()
    assert f'\nExec="{prefix}/bin/glyphshift-overlay"\n' in text, text
    assert '\nIcon=glyphshift\n' in text and '\nTerminal=false\n' in text, text
    assert (prefix / 'share/icons/hicolor/scalable/apps/glyphshift.svg').is_file()
    if shutil.which('desktop-file-validate'):
        subprocess.run(['desktop-file-validate', str(entry)], check=True)

    env = dict(os.environ, XDG_CONFIG_HOME=str(pathlib.Path(temporary) / 'config'),
               PATH=str(pathlib.Path(temporary) / 'empty'))
    env.pop('LD_PRELOAD', None)
    # The launcher cannot preload from a path with whitespace and says so; from
    # a plain prefix it finds its libraries through the bin/ link.
    refused = subprocess.run([str(prefix / 'bin/glyphshift'), '--', '/usr/bin/true'], env=env,
                             capture_output=True, text=True)
    assert refused.returncode == 126 and 'whitespace' in refused.stderr, refused.stderr
    plain = pathlib.Path(temporary) / 'plain'
    subprocess.run([cmake, '--install', str(build), '--prefix', str(plain)], check=True,
                   stdout=subprocess.DEVNULL)
    launched = subprocess.run([str(plain / 'bin/glyphshift'), '--', '/usr/bin/true'], env=env,
                              capture_output=True, text=True)
    assert launched.returncode == 0, launched.stderr
    # The installed overlay loads (its editor library through $ORIGIN) and
    # rejects bad arguments with usage.
    usage = subprocess.run([str(prefix / 'bin/glyphshift-overlay'), '--autostart', 'maybe'],
                           env=env, capture_output=True, text=True)
    assert usage.returncode == 2 and 'Usage:' in usage.stderr, usage.stderr
    bad = subprocess.run([str(prefix / 'bin/glyphshift-overlay'), '--autostart', 'on',
                          '--hotkey', 'hyper+q'], env=env, capture_output=True, text=True)
    assert bad.returncode == 2 and 'modifier' in bad.stderr, bad.stderr
    autostart = pathlib.Path(env['XDG_CONFIG_HOME']) / 'autostart/glyphshift.desktop'
    assert not autostart.exists()
    on = subprocess.run([str(prefix / 'bin/glyphshift-overlay'), '--autostart', 'on', '--hotkey',
                         'super+shift+F9', '--preset', 'art-comic'], env=env, capture_output=True,
                        text=True)
    assert on.returncode == 0 and str(autostart) in on.stdout, on.stdout + on.stderr
    expected = f'\nExec="{private}/glyphshift-overlay" --hotkey super+shift+F9 --preset art-comic\n'
    assert expected in autostart.read_text(), autostart.read_text()
    if shutil.which('desktop-file-validate'):
        subprocess.run(['desktop-file-validate', str(autostart)], check=True)
    off = subprocess.run([str(prefix / 'bin/glyphshift-overlay'), '--autostart', 'off'], env=env,
                         capture_output=True, text=True)
    assert off.returncode == 0 and not autostart.exists(), off.stderr
print('install test passed')
