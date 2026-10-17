"""Real X11 input scoped to a focused test client; restores focus and pointer."""
import ctypes as c
import ctypes.util
import time


class RealInput:
    def __init__(self, x11, window, previous=None):
        self.x = x11
        self.window = window
        lib = x11.lib
        signatures = {
            'XGetInputFocus': (c.c_int, [c.c_void_p, c.POINTER(c.c_ulong), c.POINTER(c.c_int)]),
            'XSetInputFocus': (c.c_int, [c.c_void_p, c.c_ulong, c.c_int, c.c_ulong]),
            'XTranslateCoordinates': (c.c_int, [c.c_void_p, c.c_ulong, c.c_ulong, c.c_int, c.c_int, c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_ulong)]),
            'XQueryPointer': (c.c_int, [c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong), c.POINTER(c.c_ulong), c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_uint)]),
            'XCreateSimpleWindow': (c.c_ulong, [c.c_void_p, c.c_ulong, c.c_int, c.c_int, c.c_uint, c.c_uint, c.c_uint, c.c_ulong, c.c_ulong]),
            'XMapWindow': (c.c_int, [c.c_void_p, c.c_ulong]),
            'XDestroyWindow': (c.c_int, [c.c_void_p, c.c_ulong]),
        }
        for name, (result, args) in signatures.items():
            fn = getattr(lib, name)
            fn.restype, fn.argtypes = result, args
        self.test = c.CDLL(ctypes.util.find_library('Xtst'))
        self.test.XTestFakeKeyEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
        self.test.XTestFakeButtonEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
        self.test.XTestFakeMotionEvent.argtypes = [c.c_void_p, c.c_int, c.c_int, c.c_int, c.c_ulong]
        self.previous = self.focus() if previous is None else previous
        root, child = c.c_ulong(), c.c_ulong()
        rx, ry, wx, wy = [c.c_int() for _ in range(4)]
        mask = c.c_uint()
        lib.XQueryPointer(x11.display, x11.root, c.byref(root), c.byref(child), c.byref(rx), c.byref(ry), c.byref(wx), c.byref(wy), c.byref(mask))
        self.pointer = (rx.value, ry.value)
        if mask.value & 0x1f4d:
            raise RuntimeError('Release held keyboard modifiers and mouse buttons before this focused test')
        self.sentinel = lib.XCreateSimpleWindow(x11.display, x11.root, 0, 0, 1, 1, 0, 0, 0)
        lib.XMapWindow(x11.display, self.sentinel)
        self.set_focus(window)

    @staticmethod
    def read_focus(x11):
        fn = x11.lib.XGetInputFocus
        fn.restype = c.c_int
        fn.argtypes = [c.c_void_p, c.POINTER(c.c_ulong), c.POINTER(c.c_int)]
        window, revert = c.c_ulong(), c.c_int()
        fn(x11.display, c.byref(window), c.byref(revert))
        return window.value

    def focus(self):
        return self.read_focus(self.x)

    def set_focus(self, window):
        self.x.lib.XSetInputFocus(self.x.display, window, 2, 0)
        self.x.lib.XSync(self.x.display, False)
        time.sleep(.15)

    def belongs(self, window):
        for _ in range(32):
            if window == self.window:
                return True
            root, parent = c.c_ulong(), c.c_ulong()
            children, count = c.POINTER(c.c_ulong)(), c.c_uint()
            if not self.x.lib.XQueryTree(self.x.display, window, c.byref(root), c.byref(parent), c.byref(children), c.byref(count)):
                return False
            if children:
                self.x.lib.XFree(children)
            if parent.value == root.value:
                return False
            window = parent.value
        return False

    def guard(self):
        if not self.belongs(self.focus()):
            raise RuntimeError('Test application lost focus; refusing desktop input')

    def key(self, symbol, modifier=None):
        self.guard()
        code = self.x.lib.XKeysymToKeycode(self.x.display, symbol)
        mod = self.x.lib.XKeysymToKeycode(self.x.display, modifier) if modifier else None
        try:
            if mod:
                self.test.XTestFakeKeyEvent(self.x.display, mod, True, 0)
            self.test.XTestFakeKeyEvent(self.x.display, code, True, 0)
            self.x.lib.XFlush(self.x.display)
            time.sleep(.04)
        finally:
            self.test.XTestFakeKeyEvent(self.x.display, code, False, 0)
            if mod:
                self.test.XTestFakeKeyEvent(self.x.display, mod, False, 0)
            self.x.lib.XFlush(self.x.display)
        time.sleep(.12)

    def text(self, text):
        for char in text:
            self.key(ord(char))

    def click(self, x, y, modifier=None):
        self.guard()
        rx, ry, child = c.c_int(), c.c_int(), c.c_ulong()
        assert self.x.lib.XTranslateCoordinates(self.x.display, self.window, self.x.root, x, y, c.byref(rx), c.byref(ry), c.byref(child))
        self.test.XTestFakeMotionEvent(self.x.display, -1, rx.value, ry.value, 0)
        mod = self.x.lib.XKeysymToKeycode(self.x.display, modifier) if modifier else None
        try:
            if mod:
                self.test.XTestFakeKeyEvent(self.x.display, mod, True, 0)
            self.test.XTestFakeButtonEvent(self.x.display, 1, True, 0)
            self.x.lib.XFlush(self.x.display)
            time.sleep(.06)
        finally:
            self.test.XTestFakeButtonEvent(self.x.display, 1, False, 0)
            if mod:
                self.test.XTestFakeKeyEvent(self.x.display, mod, False, 0)
            self.x.lib.XFlush(self.x.display)
        time.sleep(.2)

    def close(self):
        # Restore only if focus is still owned by this test, never after the
        # user has switched to another application themselves.
        if self.belongs(self.focus()) or self.focus() in (self.sentinel, 0, 1, self.x.root):
            self.set_focus(self.previous)
            self.test.XTestFakeMotionEvent(self.x.display, -1, *self.pointer, 0)
        self.x.lib.XDestroyWindow(self.x.display, self.sentinel)
        self.x.lib.XFlush(self.x.display)
