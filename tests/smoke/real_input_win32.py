"""Real Win32 input (SendInput) scoped to a test window, the counterpart of
real_input.py: every key and click first checks that the foreground belongs to
the test, and closing gives the foreground and the pointer back."""
import ctypes as c
import ctypes.wintypes as w
import time

import win32

user32 = c.WinDLL('user32', use_last_error=True)
ULONG_PTR = c.c_size_t


class MouseInput(c.Structure):
    _fields_ = [('dx', w.LONG), ('dy', w.LONG), ('mouseData', w.DWORD), ('dwFlags', w.DWORD),
                ('time', w.DWORD), ('dwExtraInfo', ULONG_PTR)]


class KeyboardInput(c.Structure):
    _fields_ = [('wVk', w.WORD), ('wScan', w.WORD), ('dwFlags', w.DWORD), ('time', w.DWORD),
                ('dwExtraInfo', ULONG_PTR)]


class _Union(c.Union):
    _fields_ = [('mi', MouseInput), ('ki', KeyboardInput), ('padding', c.c_byte * 32)]


class Input(c.Structure):
    _fields_ = [('type', w.DWORD), ('union', _Union)]


for _name, (_result, _args) in {
        'SendInput': (w.UINT, [w.UINT, c.POINTER(Input), c.c_int]),
        'GetAsyncKeyState': (c.c_short, [c.c_int]),
        'GetCursorPos': (w.BOOL, [c.POINTER(w.POINT)]),
        'SetCursorPos': (w.BOOL, [c.c_int, c.c_int]),
        'SetForegroundWindow': (w.BOOL, [w.HWND]),
        'GetForegroundWindow': (w.HWND, []),
        'AllowSetForegroundWindow': (w.BOOL, [w.DWORD]),
        'MapVirtualKeyW': (w.UINT, [w.UINT, w.UINT]),
        'VkKeyScanW': (c.c_short, [w.WCHAR]),
        'CreateWindowExW': (w.HWND, [w.DWORD, w.LPCWSTR, w.LPCWSTR, w.DWORD, c.c_int, c.c_int,
                                     c.c_int, c.c_int, w.HWND, w.HMENU, w.HINSTANCE, w.LPVOID]),
        'DestroyWindow': (w.BOOL, [w.HWND]),
        'PeekMessageW': (w.BOOL, [c.POINTER(w.MSG), w.HWND, w.UINT, w.UINT, w.UINT]),
        'TranslateMessage': (w.BOOL, [c.POINTER(w.MSG)]),
        'DispatchMessageW': (c.c_ssize_t, [c.POINTER(w.MSG)]),
}.items():
    _function = getattr(user32, _name)
    _function.restype, _function.argtypes = _result, _args

INPUT_MOUSE, INPUT_KEYBOARD = 0, 1
KEYEVENTF_EXTENDEDKEY, KEYEVENTF_KEYUP = 0x1, 0x2
MOUSEEVENTF_MOVE, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x1, 0x2, 0x4
VK_LCONTROL, VK_LMENU, VK_LSHIFT = 0xA2, 0xA4, 0xA0
# Keys and buttons that must not be held when the test starts.
_HELD = [0x01, 0x02, 0x04, 0x10, 0x11, 0x12, 0x5B, 0x5C]


def _send(*events):
    array = (Input * len(events))(*events)
    if user32.SendInput(len(events), array, c.sizeof(Input)) != len(events):
        raise RuntimeError(f'SendInput was blocked (error {c.get_last_error()})')


# Keys whose scan codes need the extended prefix; without it the arrows,
# for example, arrive as the numeric keypad's keys in applications that read
# scan codes (SDL games).
_EXTENDED = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2D, 0x2E, 0xA3, 0xA5, 0x5B, 0x5C}


def _key_event(virtual, release):
    event = Input(INPUT_KEYBOARD)
    flags = (KEYEVENTF_KEYUP if release else 0) | \
        (KEYEVENTF_EXTENDEDKEY if virtual in _EXTENDED else 0)
    event.union.ki = KeyboardInput(virtual, user32.MapVirtualKeyW(virtual, 0), flags, 0, 0)
    return event


def _mouse_event(flags):
    event = Input(INPUT_MOUSE)
    event.union.mi = MouseInput(0, 0, 0, flags, 0, 0)
    return event


def foreground():
    return user32.GetForegroundWindow() or None


class RealInput:
    def __init__(self, window):
        self.window = window
        self.allowed = {window}
        self._sentinel = None
        self.previous = foreground()
        point = w.POINT()
        user32.GetCursorPos(c.byref(point))
        self.pointer = point.x, point.y
        held = [key for key in _HELD if user32.GetAsyncKeyState(key) & 0x8000]
        if held:
            raise RuntimeError(f'release held keys and mouse buttons first: {held}')
        self.activate(window)

    def activate(self, window):
        """Makes `window` the foreground window, as a user's click would. Input
        this process sends allows it to set the foreground."""
        if foreground() == window:
            return
        _send(_mouse_event(MOUSEEVENTF_MOVE))  # A no-op move: our input is the latest.
        user32.SetForegroundWindow(window)
        deadline = time.monotonic() + 3
        while foreground() != window:
            if time.monotonic() > deadline:
                raise RuntimeError(f'cannot bring the test window {window} to the foreground')
            time.sleep(.05)
        time.sleep(.15)

    def allow(self, window, pid=None):
        """Accepts another test-owned window in the foreground (the daemon's
        menu), and lets process `pid` take the foreground."""
        self.allowed.add(window)
        if pid:
            user32.AllowSetForegroundWindow(pid)

    def guard(self):
        current = foreground()
        if current not in self.allowed:
            raise RuntimeError(f'the foreground went to {current} ({win32.class_name(current)}); '
                               'refusing desktop input')

    def key(self, virtual, modifiers=()):
        """Presses modifiers in order, the key, then releases them in reverse."""
        self.guard()
        pressed = []
        try:
            for modifier in modifiers:
                _send(_key_event(modifier, False))
                pressed.append(modifier)
            _send(_key_event(virtual, False))
            time.sleep(.05)
        finally:
            _send(_key_event(virtual, True))
            for modifier in reversed(pressed):
                _send(_key_event(modifier, True))
        time.sleep(.12)

    def key_down(self, virtual):
        self.guard()
        _send(_key_event(virtual, False))
        time.sleep(.06)

    def key_up(self, virtual):
        _send(_key_event(virtual, True))
        time.sleep(.06)

    def hold(self, virtual, seconds):
        """Holds a key down for a while, as a player walking."""
        self.key_down(virtual)
        try:
            time.sleep(seconds)
        finally:
            self.key_up(virtual)

    def turn(self, dx, seconds):
        """Relative mouse motion of `dx` per 20 ms for a while, as a player
        turning the camera in a 3D game."""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.guard()
            event = _mouse_event(MOUSEEVENTF_MOVE)
            event.union.mi.dx = dx
            _send(event)
            time.sleep(.02)
        time.sleep(.12)

    def text(self, text):
        """Types characters through their keys in the current layout."""
        for character in text:
            scan = user32.VkKeyScanW(character)
            if scan == -1:
                raise ValueError(f'no key types {character!r} in this layout')
            self.key(scan & 0xff, (VK_LSHIFT,) if scan & 0x100 else ())

    def sentinel(self):
        """A tiny test-owned window to move the foreground to, instead of
        another application; the caller pumps its messages with pump()."""
        if not self._sentinel:
            self._sentinel = user32.CreateWindowExW(0x80, 'STATIC', 'GlyphShift test sentinel',
                                                    0x90000000, 0, 0, 1, 1, None, None, None,
                                                    None)  # WS_EX_TOOLWINDOW, WS_POPUP|VISIBLE
            self.allowed.add(self._sentinel)
        return self._sentinel

    @staticmethod
    def pump(seconds=.3):
        message = w.MSG()
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            while user32.PeekMessageW(c.byref(message), None, 0, 0, 1):  # PM_REMOVE
                user32.TranslateMessage(c.byref(message))
                user32.DispatchMessageW(c.byref(message))
            time.sleep(.02)

    def click(self, x, y):
        """A left click at screen coordinates, which must hit a test window."""
        self.guard()
        target = win32.window_at(x, y)
        if target not in self.allowed and win32.class_name(target) != 'GlyphShiftOverlay':
            raise RuntimeError(f'refusing a click on {target} ({win32.class_name(target)})')
        user32.SetCursorPos(x, y)
        _send(_mouse_event(MOUSEEVENTF_LEFTDOWN))
        try:
            time.sleep(.06)
        finally:
            _send(_mouse_event(MOUSEEVENTF_LEFTUP))
        time.sleep(.2)

    def close(self):
        # Give the foreground back only while the test still owns it, never
        # after the user has switched to another application themselves.
        current = foreground()
        if self.previous and (current in self.allowed or current is None or
                              not win32.exists(current)):
            _send(_mouse_event(MOUSEEVENTF_MOVE))
            user32.SetForegroundWindow(self.previous)
        user32.SetCursorPos(*self.pointer)
        if self._sentinel:
            user32.DestroyWindow(self._sentinel)
            self._sentinel = None
