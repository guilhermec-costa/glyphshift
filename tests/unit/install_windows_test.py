"""cmake --install on Windows: layout, Start menu shortcut, icon and version
resources, the installed overlay running, start at sign-in through the
launcher, and GlyphShift.exe starting the background overlay without a
console window. The counterpart of install_test.py."""
import ctypes as c
import ctypes.wintypes as w
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import winreg

cmake, build = sys.argv[1], pathlib.Path(sys.argv[2])
user32 = c.WinDLL('user32', use_last_error=True)
kernel32 = c.WinDLL('kernel32', use_last_error=True)
version = c.WinDLL('version', use_last_error=True)
RUN_KEY = r'Software\Microsoft\Windows\CurrentVersion\Run'
VALUE = 'GlyphShiftInstallTest'
WNDENUMPROC = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
user32.EnumWindows.argtypes = [WNDENUMPROC, w.LPARAM]
user32.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
user32.IsWindowVisible.argtypes = [w.HWND]
user32.FindWindowW.restype = w.HWND
user32.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
user32.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
kernel32.LoadLibraryExW.restype = w.HMODULE
kernel32.LoadLibraryExW.argtypes = [w.LPCWSTR, w.HANDLE, w.DWORD]
kernel32.FindResourceW.restype = w.HANDLE
kernel32.FindResourceW.argtypes = [w.HMODULE, w.LPCWSTR, w.LPCWSTR]
kernel32.FreeLibrary.argtypes = [w.HMODULE]
kernel32.OpenEventW.restype = w.HANDLE
kernel32.OpenEventW.argtypes = [w.DWORD, w.BOOL, w.LPCWSTR]
kernel32.OpenProcess.restype = w.HANDLE
kernel32.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
kernel32.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
kernel32.SetEvent.argtypes = [w.HANDLE]
version.GetFileVersionInfoSizeW.argtypes = [w.LPCWSTR, c.POINTER(w.DWORD)]
version.GetFileVersionInfoW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p]
version.VerQueryValueW.argtypes = [c.c_void_p, w.LPCWSTR, c.POINTER(c.c_void_p),
                                   c.POINTER(w.UINT)]


def description(path):
    size = version.GetFileVersionInfoSizeW(str(path), None)
    assert size, f'{path} has no version information'
    data = c.create_string_buffer(size)
    assert version.GetFileVersionInfoW(str(path), 0, size, data)
    value, length = c.c_void_p(), w.UINT()
    assert version.VerQueryValueW(data, r'\StringFileInfo\040904B0\FileDescription',
                                  c.byref(value), c.byref(length))
    return c.wstring_at(value, length.value).rstrip('\0')


def has_icon(path):
    module = kernel32.LoadLibraryExW(str(path), None, 0x2)  # LOAD_LIBRARY_AS_DATAFILE
    try:
        return bool(kernel32.FindResourceW(module, c.c_wchar_p(1), c.c_wchar_p(14)))  # RT_GROUP_ICON
    finally:
        kernel32.FreeLibrary(module)


def consoles():
    """Visible console windows (classic host and Windows Terminal)."""
    found = []

    def visit(window, _):
        name = c.create_unicode_buffer(64)
        user32.GetClassNameW(window, name, 64)
        if name.value in ('ConsoleWindowClass', 'CASCADIA_HOSTING_WINDOW_CLASS') and \
                user32.IsWindowVisible(window):
            found.append(window)
        return True
    user32.EnumWindows(WNDENUMPROC(visit), 0)
    return set(found)


def run_value():
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as key:
            return winreg.QueryValueEx(key, VALUE)[0]
    except FileNotFoundError:
        return None


with tempfile.TemporaryDirectory(prefix='glyphshift-install-') as temporary:
    temporary = pathlib.Path(temporary)
    prefix = temporary / 'prefix with space'
    shortcuts = temporary / 'start menu'
    env = dict(os.environ, GLYPHSHIFT_SHORTCUT_DIR=str(shortcuts), APPDATA=str(temporary / 'appdata'),
               GLYPHSHIFT_AUTOSTART_VALUE=VALUE, GLYPHSHIFT_NO_NOTIFICATIONS='1')
    subprocess.run([cmake, '--install', str(build), '--prefix', str(prefix)], check=True,
                   stdout=subprocess.DEVNULL, env=env)
    for name in ['GlyphShift.exe', 'glyphshift-overlay.exe']:
        assert (prefix / name).is_file(), name
    assert list(prefix.glob('freetype*.dll')), 'FreeType (freetype.dll, Debug freetyped.dll) missing'
    assert 'MIT License' in (prefix / 'licenses/LICENSE').read_text(encoding='utf-8')
    assert 'Omar Cornut' in (prefix / 'licenses/LICENSE.imgui.txt').read_text(encoding='utf-8')
    for name in ['GlyphShift.exe', 'glyphshift-overlay.exe']:
        assert description(prefix / name) == 'GlyphShift', name
        assert has_icon(prefix / name), f'{name} has no application icon'
    # The shortcut points at the installed launcher.
    link = shortcuts / 'GlyphShift.lnk'
    assert link.is_file(), 'no Start menu shortcut'
    target = subprocess.run(
        ['powershell', '-NoProfile', '-NonInteractive', '-Command',
         f"(New-Object -ComObject WScript.Shell).CreateShortcut('{link}').TargetPath"],
        capture_output=True, text=True, check=True).stdout.strip()
    assert pathlib.Path(target) == prefix / 'GlyphShift.exe', target
    print('install: programs, DLLs, licenses, icon and version resources, Start menu shortcut')

    overlay = prefix / 'glyphshift-overlay.exe'
    usage = subprocess.run([str(overlay), '--autostart', 'maybe'], env=env, capture_output=True,
                           text=True)
    assert usage.returncode == 2 and 'Usage:' in usage.stderr, usage.stderr
    try:
        on = subprocess.run([str(overlay), '--autostart', 'on', '--hotkey', 'ctrl+alt+shift+F9'],
                            env=env, capture_output=True, text=True)
        assert on.returncode == 0, on.stderr
        line = run_value()
        assert line == f'"{prefix / "GlyphShift.exe"}" --hotkey ctrl+alt+shift+F9', line
    finally:
        subprocess.run([str(overlay), '--autostart', 'off'], env=env, capture_output=True)
    assert run_value() is None
    print('install: start at sign-in runs the installed GlyphShift.exe')

    # The launcher starts the background overlay without a console window and
    # returns; the daemon then runs until its stop event.
    before = consoles()
    launched = subprocess.run([str(prefix / 'GlyphShift.exe'), '--hotkey', 'ctrl+alt+shift+F10'],
                              env=env, timeout=10)
    assert launched.returncode == 0
    daemon = None
    deadline = time.monotonic() + 10
    while not daemon and time.monotonic() < deadline:
        daemon = user32.FindWindowW('GlyphShiftDaemon', 'ctrl+alt+shift+F10')
        time.sleep(.1)
    if not daemon:
        other = user32.FindWindowW('GlyphShiftDaemon', None)
        assert other, 'GlyphShift.exe did not start the background overlay'
        print('install: SKIPPED the launcher check, another GlyphShift daemon runs')
        sys.exit(0)
    pid = w.DWORD()
    user32.GetWindowThreadProcessId(daemon, c.byref(pid))
    time.sleep(1)
    assert consoles() <= before, 'a console window opened'
    process = kernel32.OpenProcess(0x00100000, False, pid.value)  # SYNCHRONIZE
    event = kernel32.OpenEventW(0x0002, False, f'Local\\GlyphShiftStop-{pid.value}')
    assert event, 'the daemon does not listen for stop requests'
    kernel32.SetEvent(event)
    assert kernel32.WaitForSingleObject(process, 10000) == 0, 'the daemon did not stop'
    print('install: GlyphShift.exe starts the background overlay without a console window')
