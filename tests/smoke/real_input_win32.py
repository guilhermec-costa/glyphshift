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
}.items():
    _function = getattr(user32, _name)
    _function.restype, _function.argtypes = _result, _args

INPUT_MOUSE, INPUT_KEYBOARD = 0, 1
KEYEVENTF_KEYUP = 0x2
MOUSEEVENTF_MOVE, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x1, 0x2, 0x4
VK_LCONTROL, VK_LMENU, VK_LSHIFT = 0xA2, 0xA4, 0xA0
# Keys and buttons that must not be held when the test starts.
_HELD = [0x01, 0x02, 0x04, 0x10, 0x11, 0x12, 0x5B, 0x5C]


def _send(*events):
    array = (Input * len(events))(*events)
    if user32.SendInput(len(events), array, c.sizeof(Input)) != len(events):
        raise RuntimeError(f'SendInput was blocked (error {c.get_last_error()})')


def _key_event(virtual, release):
    event = Input(INPUT_KEYBOARD)
    event.union.ki = KeyboardInput(virtual, user32.MapVirtualKeyW(virtual, 0),
                                   KEYEVENTF_KEYUP if release else 0, 0, 0)
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
