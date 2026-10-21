#!/usr/bin/env python3
"""Persistent test environment for the game and validation tests.

Downloads pinned, checksummed copies of GE-Proton, SuperTux for Windows, the
Khronos validation layer, glxgears and vkcube once, and keeps them, a Proton prefix and test game
profiles outside /tmp, so they survive reboots. Nothing is installed system-wide
and Steam is not used.

    python3 tests/smoke/test_env.py setup     # download (once) and prepare
    python3 tests/smoke/test_env.py status
    python3 tests/smoke/test_env.py run [--offline] -- COMMAND...

`run` adds the validation layer to VK_LAYER_PATH. `--offline` runs COMMAND in
a new user and network namespace with no network interfaces (X11 still works
through its Unix socket); Proton games are only run that way.

On Windows (M7) the environment is glyphshift-test under %LOCALAPPDATA% and
holds only SuperTux 0.7.0 for Windows, run natively, and its test profile; the
validation layer comes from the Vulkan SDK. Windows has no unprivileged network
namespace, so `--offline` is refused there: the test profile turns the game's
networking, release check and Discord integration off.
"""
import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import urllib.request
import zipfile

WINDOWS = sys.platform == 'win32'
if WINDOWS:
    _DATA = Path(os.environ.get('LOCALAPPDATA') or Path.home() / 'AppData/Local')
else:
    _DATA = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')
ROOT = Path(os.environ.get('GLYPHSHIFT_TEST_ENV') or _DATA / 'glyphshift-test')
DOWNLOADS = {
    'GE-Proton11-9-x86_64.tar.gz': (
        'https://github.com/GloriousEggroll/proton-ge-custom/releases/download/GE-Proton11-9/'
        'GE-Proton11-9-x86_64.tar.gz',
        'b2cf4a632a70ec321cdb9904fa396a2a45d4061129f9fdc89d8a27ea5ceeb9d6'),
    'SuperTux-v0.7.0-win64-portable.zip': (
        'https://github.com/SuperTux/supertux/releases/download/v0.7.0/'
        'SuperTux-v0.7.0-win64-portable.zip',
        'd637f79e8a77fc4d42982cfea40f74f812d5050b9d6d53309b287dae734e1970'),
    'vulkan-validation-layers-1.4.363.0-1-x86_64.pkg.tar.zst': (
        'https://archive.archlinux.org/packages/v/vulkan-validation-layers/'
        'vulkan-validation-layers-1.4.363.0-1-x86_64.pkg.tar.zst',
        '904f4eba2e708c9c96579900688a284309cd56dc09b5af3f78c563ffda13df9e'),
    'mesa-utils-9.0.0-7-x86_64.pkg.tar.zst': (
        'https://archive.archlinux.org/packages/m/mesa-utils/mesa-utils-9.0.0-7-x86_64.pkg.tar.zst',
        '2433deec3f9b3c166c1976ea0cecf1de9466a5b55525df9b6a0cf273b4747fc1'),
    'vulkan-tools-1.4.363.0-1-x86_64.pkg.tar.zst': (
        'https://archive.archlinux.org/packages/v/vulkan-tools/'
        'vulkan-tools-1.4.363.0-1-x86_64.pkg.tar.zst',
        'b8d0cab28210881c237bdee344bfe6c4a43cf77373e6f8e6cfc3e98a575e2045'),
}
PROTON = ROOT / 'GE-Proton11-9-x86_64'
SUPERTUX_EXE = ROOT / 'SuperTux-portable/bin/supertux2.exe'
PREFIX = ROOT / 'proton-prefix'
STEAM_EMPTY = ROOT / 'steam-empty'
VALIDATION = ROOT / 'validation'
LAYER_PATH = VALIDATION / 'usr/share/vulkan/explicit_layer.d'
# Native glxgears and vkcube for the focused editor tests.
APPS = ROOT / 'apps'
GLXGEARS = APPS / 'usr/bin/glxgears'
VKCUBE = APPS / 'usr/bin/vkcube'
# Game profiles: networking, release checks, Discord, audio and pausing on
# focus loss off. Separate from the user's own SuperTux profile.
NATIVE_USER = ROOT / 'supertux-native-user'
PROTON_USER = ROOT / 'supertux-proton-user'
WINDOWS_USER = ROOT / 'supertux-windows-user'
# The Windows build rejects converted paths with separators, so it loads an
# unmodified copy of a shipped level by bare file name from its data directory.
LEVEL = 'glyphshift-test.stl'
NATIVE_LEVEL = Path('/usr/share/games/supertux2/levels/world1/welcome_antarctica.stl')
SUPERTUX_TITLE = 'SuperTux v0.7.0'
SUPERTUX_CONFIG = '''(supertux-config
  (profile 1)
  (confirmation_dialog #f)
  (pause_on_focusloss #f)
  (do_release_check #f)
  (disable_network #t)
  (integrations
    (enable_discord #f)
  )
  (video
    (fullscreen #f)
    (vsync 1)
    (window_width 800)
    (window_height 600)
    (window_resizable #t)
  )
  (audio
    (sound_enabled #f)
    (music_enabled #f)
  )
)
'''


def windows_path(path):
    return 'Z:' + str(path).replace('/', '\\')


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def fetch(name):
    url, expected = DOWNLOADS[name]
    target = ROOT / 'downloads' / name
    if target.exists() and sha256(target) == expected:
        return target
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(name + '.part')
    print(f'downloading {url}', flush=True)
    with urllib.request.urlopen(url) as response, partial.open('wb') as stream:
        shutil.copyfileobj(response, stream, 1 << 20)
    actual = sha256(partial)
    if actual != expected:
        partial.unlink()
        raise SystemExit(f'{name}: checksum {actual} does not match the pinned {expected}')
    partial.rename(target)
    return target


def marked(path):
    return (path / '.glyphshift-complete').exists()


def mark(path):
    (path / '.glyphshift-complete').write_text('')


def setup_game():
    game = SUPERTUX_EXE.parents[1]
    if not marked(game):
        shutil.rmtree(game, ignore_errors=True)
        with zipfile.ZipFile(fetch('SuperTux-v0.7.0-win64-portable.zip')) as archive:
            archive.extractall(ROOT)
        shutil.copyfile(game / 'data/levels/world1/welcome_antarctica.stl', game / 'data' / LEVEL)
        mark(game)


def setup():
    ROOT.mkdir(parents=True, exist_ok=True)
    if WINDOWS:
        setup_game()
        WINDOWS_USER.mkdir(exist_ok=True)
        (WINDOWS_USER / 'config').write_text(SUPERTUX_CONFIG)
        status()
        return
    if not marked(PROTON):
        shutil.rmtree(PROTON, ignore_errors=True)
        subprocess.run(['tar', '-xzf', fetch('GE-Proton11-9-x86_64.tar.gz'), '-C', ROOT], check=True)
        mark(PROTON)
    setup_game()
    if not marked(VALIDATION):
        shutil.rmtree(VALIDATION, ignore_errors=True)
        VALIDATION.mkdir()
        subprocess.run(['tar', '--zstd', '-xf',
                        fetch('vulkan-validation-layers-1.4.363.0-1-x86_64.pkg.tar.zst'),
                        '-C', VALIDATION, 'usr'], check=True)
        # The packaged manifest names the library for the system search path.
        manifest = LAYER_PATH / 'VkLayer_khronos_validation.json'
        data = json.loads(manifest.read_text())
        data['layer']['library_path'] = str(VALIDATION / 'usr/lib/libVkLayer_khronos_validation.so')
        manifest.write_text(json.dumps(data, indent=4))
        mark(VALIDATION)
    if not marked(APPS):
        shutil.rmtree(APPS, ignore_errors=True)
        APPS.mkdir()
        for package in ('mesa-utils-9.0.0-7-x86_64.pkg.tar.zst',
                        'vulkan-tools-1.4.363.0-1-x86_64.pkg.tar.zst'):
            subprocess.run(['tar', '--zstd', '-xf', fetch(package), '-C', APPS, 'usr'], check=True)
        mark(APPS)
    for directory in (PREFIX, STEAM_EMPTY, NATIVE_USER, PROTON_USER):
        directory.mkdir(exist_ok=True)
    for user in (NATIVE_USER, PROTON_USER):
        (user / 'config').write_text(SUPERTUX_CONFIG)
    status()


def status():
    print(f'test environment: {ROOT}')
    if WINDOWS:
        rows = [('SuperTux 0.7.0 Windows', SUPERTUX_EXE, marked(SUPERTUX_EXE.parents[1])),
                ('SuperTux test profile', WINDOWS_USER, (WINDOWS_USER / 'config').exists())]
        for label, path, ready in rows:
            print(f'  {"ok     " if ready else "missing"} {label}: {path}')
        return
    for label, path, ready in [
            ('GE-Proton11-9', PROTON / 'proton', marked(PROTON)),
            ('SuperTux 0.7.0 Windows', SUPERTUX_EXE, marked(SUPERTUX_EXE.parents[1])),
            ('Khronos validation layer', LAYER_PATH, marked(VALIDATION)),
            ('glxgears and vkcube', APPS / 'usr/bin', marked(APPS)),
            ('Proton prefix', PREFIX, PREFIX.exists()),
            ('native SuperTux test profile', NATIVE_USER, (NATIVE_USER / 'config').exists())]:
        print(f'  {"ok     " if ready else "missing"} {label}: {path}')


def ready():
    return marked(PROTON) and marked(SUPERTUX_EXE.parents[1]) and marked(VALIDATION)


def vulkan_env(env):
    env['VK_LAYER_PATH'] = os.pathsep.join(filter(None, [str(LAYER_PATH), env.get('VK_LAYER_PATH')]))
    return env


def offline():
    """True when this process has no route to the network."""
    with socket.socket() as probe:
        probe.settimeout(2)
        try:
            probe.connect(('1.1.1.1', 80))
        except OSError as error:
            return error.errno in (errno.ENETUNREACH, errno.EHOSTUNREACH)
    return False


def proton_env(env):
    """Environment for `proton run` without Steam. Requires an offline process."""
    if not offline():
        raise SystemExit('refusing to run Proton with network access; use test_env.py run --offline')
    env.update(STEAM_COMPAT_DATA_PATH=str(PREFIX), STEAM_COMPAT_CLIENT_INSTALL_PATH=str(STEAM_EMPTY),
               PROTONFIXES_DISABLE='1', WINEDEBUG='-all', PROTON_LOG='0', SteamGameId='0',
               SDL_AUDIODRIVER='dummy')
    return env


def supertux_proton(*arguments):
    return ['/usr/bin/python3', str(PROTON / 'proton'), 'run', str(SUPERTUX_EXE),
            '--userdir', windows_path(PROTON_USER), '--window', '--disable-sound', '--disable-music',
            *arguments]


def supertux_windows(*arguments):
    """SuperTux for Windows run natively with the test profile (Windows only)."""
    return [str(SUPERTUX_EXE), '--userdir', str(WINDOWS_USER), '--window', '--disable-sound',
            '--disable-music', *arguments]


def supertux_native(*arguments):
    return ['/usr/bin/supertux2', '--userdir', str(NATIVE_USER), '--window', '--disable-sound',
            '--disable-music', *arguments]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('setup')
    commands.add_parser('status')
    run = commands.add_parser('run')
    run.add_argument('--offline', action='store_true')
    run.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.command == 'setup':
        setup()
    elif args.command == 'status':
        status()
    else:
        command = args.arguments[1:] if args.arguments[:1] == ['--'] else args.arguments
        if not command:
            parser.error('run needs a command')
        if WINDOWS:
            if args.offline:
                raise SystemExit('--offline needs a network namespace, which Windows lacks')
            if not marked(SUPERTUX_EXE.parents[1]):
                raise SystemExit('the test environment is incomplete; run: test_env.py setup')
            raise SystemExit(subprocess.run(command).returncode)
        if not marked(VALIDATION):
            raise SystemExit('the test environment is incomplete; run: test_env.py setup')
        if args.offline:
            command = ['unshare', '--user', '--map-current-user', '--net', '--', *command]
        os.execvpe(command[0], command, vulkan_env(os.environ.copy()))


if __name__ == '__main__':
    main()
