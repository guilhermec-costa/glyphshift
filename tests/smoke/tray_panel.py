"""A stand-in system tray manager (XEmbed) for tests, plus tray menu helpers.

It owns the tray selection like a panel would, announces itself with MANAGER,
embeds the icon a client offers, and reads windows back. No keyboard or
pointer input is generated here.
"""
import ctypes as c
import time

from x11 import Event, X11

CLIENT_MESSAGE = 33
XK_UP, XK_DOWN, XK_RETURN, XK_ESCAPE = 0xff52, 0xff54, 0xff0d, 0xff1b


class WindowAttributes(c.Structure):
    _fields_ = [('x', c.c_int), ('y', c.c_int), ('width', c.c_int), ('height', c.c_int),
                ('border_width', c.c_int), ('depth', c.c_int), ('visual', c.c_void_p),
                ('root', c.c_ulong), ('class_', c.c_int), ('bit_gravity', c.c_int),
                ('win_gravity', c.c_int), ('backing_store', c.c_int),
                ('backing_planes', c.c_ulong), ('backing_pixel', c.c_ulong),
                ('save_under', c.c_int), ('colormap', c.c_ulong), ('map_installed', c.c_int),
                ('map_state', c.c_int), ('all_event_masks', c.c_long),
                ('your_event_mask', c.c_long), ('do_not_propagate_mask', c.c_long),
                ('override_redirect', c.c_int), ('screen', c.c_void_p)]


class SetAttributes(c.Structure):
    _fields_ = [('background_pixmap', c.c_ulong), ('background_pixel', c.c_ulong),
                ('border_pixmap', c.c_ulong), ('border_pixel', c.c_ulong),
                ('bit_gravity', c.c_int), ('win_gravity', c.c_int), ('backing_store', c.c_int),
                ('backing_planes', c.c_ulong), ('backing_pixel', c.c_ulong),
                ('save_under', c.c_int), ('event_mask', c.c_long),
                ('do_not_propagate_mask', c.c_long), ('override_redirect', c.c_int),
                ('colormap', c.c_ulong), ('cursor', c.c_ulong)]


def prepare(lib):
    for name, result, arguments in [
            ('XCreateSimpleWindow', c.c_ulong, [c.c_void_p, c.c_ulong, c.c_int, c.c_int, c.c_uint,
                                                c.c_uint, c.c_uint, c.c_ulong, c.c_ulong]),
            ('XSetSelectionOwner', c.c_int, [c.c_void_p, c.c_ulong, c.c_ulong, c.c_ulong]),
            ('XGetSelectionOwner', c.c_ulong, [c.c_void_p, c.c_ulong]),
            ('XReparentWindow', c.c_int, [c.c_void_p, c.c_ulong, c.c_ulong, c.c_int, c.c_int]),
            ('XMapWindow', c.c_int, [c.c_void_p, c.c_ulong]),
            ('XResizeWindow', c.c_int, [c.c_void_p, c.c_ulong, c.c_uint, c.c_uint]),
            ('XDestroyWindow', c.c_int, [c.c_void_p, c.c_ulong]),
            ('XAddToSaveSet', c.c_int, [c.c_void_p, c.c_ulong]),
            ('XCloseDisplay', c.c_int, [c.c_void_p]),
            ('XPending', c.c_int, [c.c_void_p]),
            ('XNextEvent', c.c_int, [c.c_void_p, c.POINTER(Event)]),
            ('XChangeWindowAttributes', c.c_int, [c.c_void_p, c.c_ulong, c.c_ulong, c.c_void_p]),
            ('XGetWindowAttributes', c.c_int, [c.c_void_p, c.c_ulong, c.c_void_p])]:
        function = getattr(lib, name)
        function.restype, function.argtypes = result, arguments


def atoms(x):
    prepare(x.lib)
    return (x.lib.XInternAtom(x.display, b'_NET_SYSTEM_TRAY_S0', False),
            x.lib.XInternAtom(x.display, b'_NET_SYSTEM_TRAY_OPCODE', False),
            x.lib.XInternAtom(x.display, b'MANAGER', False))


def manager_running(x):
    selection, _, _ = atoms(x)
    return bool(x.lib.XGetSelectionOwner(x.display, selection))


class Panel:
    """A tray manager on its own X connection, like a real panel process."""

    def __init__(self):
        self.x = X11()
        lib, d = self.x.lib, self.x.display
        self.selection, self.opcode, manager = atoms(self.x)
        self.window = lib.XCreateSimpleWindow(d, self.x.root, 4, 4, 24, 24, 0, 0, 0)
        values = SetAttributes(override_redirect=1)
        lib.XChangeWindowAttributes(d, self.window, 1 << 9, c.byref(values))  # CWOverrideRedirect
        lib.XSetSelectionOwner(d, self.selection, self.window, 0)
        assert lib.XGetSelectionOwner(d, self.selection) == self.window
        event = Event()
        event.client.type = CLIENT_MESSAGE
        event.client.window = self.x.root
        event.client.message_type = manager
        event.client.format = 32
        event.client.data.l[1] = self.selection
        event.client.data.l[2] = self.window
        lib.XSendEvent(d, self.x.root, False, 1 << 17, c.byref(event))  # StructureNotifyMask
        lib.XSync(d, False)

    def dock_request(self, seconds=10):
        lib, d = self.x.lib, self.x.display
        deadline = time.monotonic() + seconds
        event = Event()
        while time.monotonic() < deadline:
            while lib.XPending(d):
                lib.XNextEvent(d, c.byref(event))
                if (event.client.type == CLIENT_MESSAGE and event.client.window == self.window
                        and event.client.message_type == self.opcode
                        and event.client.data.l[1] == 0):
                    return event.client.data.l[2]
            time.sleep(.02)
        raise AssertionError('the daemon did not ask to dock')

    def embed(self, icon):
        # Like real tray managers, the save-set keeps the icon alive when this
        # connection closes.
        lib, d = self.x.lib, self.x.display
        lib.XAddToSaveSet(d, icon)
        lib.XReparentWindow(d, icon, self.window, 0, 0)
        lib.XResizeWindow(d, icon, 24, 24)
        lib.XMapWindow(d, icon)
        lib.XMapWindow(d, self.window)
        lib.XSync(d, False)
        time.sleep(.4)

    def quit(self):
        # The panel process exits: its connection closes.
        if self.x:
            self.x.lib.XCloseDisplay(self.x.display)
            self.x = None

    def crash_with_icon(self):
        # A panel destroying its window (and the embedded icon) while running.
        self.x.lib.XDestroyWindow(self.x.display, self.window)
        self.x.lib.XSync(self.x.display, False)


def capture(x, window, w, h):
    image = x.lib.XGetImage(x.display, window, 0, 0, w, h, c.c_ulong(-1).value, 2)
    assert image, 'cannot read the window'
    info = image.contents
    raw = c.string_at(info.data, info.bytes_per_line * h)
    x.lib.XDestroyImage(image)
    rgb = bytearray()
    for row in range(h):
        line = raw[row * info.bytes_per_line:row * info.bytes_per_line + w * 4]
        for i in range(0, len(line), 4):
            rgb += bytes((line[i + 2], line[i + 1], line[i]))
    return bytes(rgb)


def save(path, x, window, w, h):
    path.write_bytes(f'P6\n{w} {h}\n255\n'.encode() + capture(x, window, w, h))


def drawn(x, icon):
    # The icon's background and glyph colors (data/glyphshift.svg).
    rgb = capture(x, icon, 24, 24)
    colors = {rgb[i:i + 3] for i in range(0, len(rgb), 3)}

    def near(color):
        return any(max(abs(p - q) for p, q in zip(color, seen)) <= 2 for seen in colors)
    return near((0xff, 0xd1, 0x66)) and near((0x15, 0x18, 0x24))


def menu_window(x):
    found = x.windows('GlyphShift menu')
    assert len(found) == 1, found
    return found[0]


def mapped(x, window):
    attributes = WindowAttributes()
    x.lib.XGetWindowAttributes(x.display, window, c.byref(attributes))
    return attributes.map_state == 2  # IsViewable


def key(x, window, symbol):
    # A synthetic key event for the daemon's menu window (XSendEvent).
    x.send(window, 2, symbol)
    x.lib.XSync(x.display, False)
    time.sleep(.08)
