"""Small Win32 test driver (ctypes only): windows by title, captures and posted
messages. It never takes the desktop keyboard, pointer or focus: input is
posted to a window's queue, and moves and resizes do not activate it."""
import ctypes as c
import ctypes.wintypes as w
import time

user32 = c.WinDLL('user32', use_last_error=True)
gdi32 = c.WinDLL('gdi32', use_last_error=True)
# Physical pixels, as the fixture and the overlay use them.
user32.SetProcessDpiAwarenessContext.argtypes = [c.c_void_p]
user32.SetProcessDpiAwarenessContext(c.c_void_p(-4))  # PER_MONITOR_AWARE_V2

WNDENUMPROC = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
_signatures = {
    'EnumWindows': (w.BOOL, [WNDENUMPROC, w.LPARAM]),
    'GetWindowTextLengthW': (c.c_int, [w.HWND]),
    'GetWindowTextW': (c.c_int, [w.HWND, w.LPWSTR, c.c_int]),
    'GetClientRect': (w.BOOL, [w.HWND, c.POINTER(w.RECT)]),
    'GetWindowRect': (w.BOOL, [w.HWND, c.POINTER(w.RECT)]),
    'ClientToScreen': (w.BOOL, [w.HWND, c.POINTER(w.POINT)]),
    'SetWindowPos': (w.BOOL, [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]),
    'PostMessageW': (w.BOOL, [w.HWND, w.UINT, w.WPARAM, w.LPARAM]),
    'GetDC': (w.HDC, [w.HWND]),
    'ReleaseDC': (c.c_int, [w.HWND, w.HDC]),
    'PrintWindow': (w.BOOL, [w.HWND, w.HDC, w.UINT]),
    'IsWindow': (w.BOOL, [w.HWND]),
    'IsWindowVisible': (w.BOOL, [w.HWND]),
    'IsIconic': (w.BOOL, [w.HWND]),
    'GetWindow': (w.HWND, [w.HWND, w.UINT]),
    'GetWindowLongPtrW': (c.c_ssize_t, [w.HWND, c.c_int]),
    'WindowFromPoint': (w.HWND, [w.POINT]),
    'GetAncestor': (w.HWND, [w.HWND, w.UINT]),
    'GetDpiForWindow': (w.UINT, [w.HWND]),
    'AdjustWindowRectExForDpi': (w.BOOL, [c.POINTER(w.RECT), w.DWORD, w.BOOL, w.DWORD, w.UINT]),
    'MapVirtualKeyW': (w.UINT, [w.UINT, w.UINT]),
    'GetForegroundWindow': (w.HWND, []),
    'GetWindowThreadProcessId': (w.DWORD, [w.HWND, c.POINTER(w.DWORD)]),
    'VkKeyScanW': (c.c_short, [w.WCHAR]),
}
for _name, (_result, _args) in _signatures.items():
    _function = getattr(user32, _name)
    _function.restype, _function.argtypes = _result, _args
for _name, (_result, _args) in {
        'CreateCompatibleDC': (w.HDC, [w.HDC]),
        'CreateDIBSection': (w.HBITMAP, [w.HDC, c.c_void_p, w.UINT, c.POINTER(c.c_void_p),
                                         w.HANDLE, w.DWORD]),
        'SelectObject': (w.HGDIOBJ, [w.HDC, w.HGDIOBJ]),
        'DeleteObject': (w.BOOL, [w.HGDIOBJ]),
        'DeleteDC': (w.BOOL, [w.HDC]),
        'BitBlt': (w.BOOL, [w.HDC, c.c_int, c.c_int, c.c_int, c.c_int, w.HDC, c.c_int, c.c_int,
                            w.DWORD]),
        'GdiFlush': (w.BOOL, []),
}.items():
    _function = getattr(gdi32, _name)
    _function.restype, _function.argtypes = _result, _args

WM_CLOSE, WM_KEYDOWN, WM_KEYUP, WM_CHAR = 0x0010, 0x0100, 0x0101, 0x0102
WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP = 0x0200, 0x0201, 0x0202
GW_HWNDNEXT, GW_HWNDPREV = 2, 3
GWL_STYLE, GWL_EXSTYLE = -16, -20
WS_EX_TRANSPARENT, WS_EX_LAYERED, WS_EX_TOPMOST = 0x20, 0x80000, 0x8
SWP_NOSIZE, SWP_NOMOVE, SWP_NOZORDER, SWP_NOACTIVATE = 0x1, 0x2, 0x4, 0x10
VK_ESCAPE, VK_RETURN, VK_INSERT, VK_F10 = 0x1B, 0x0D, 0x2D, 0x79


class BitmapInfo(c.Structure):
    _fields_ = [('biSize', w.DWORD), ('biWidth', w.LONG), ('biHeight', w.LONG),
                ('biPlanes', w.WORD), ('biBitCount', w.WORD), ('biCompression', w.DWORD),
                ('biSizeImage', w.DWORD), ('biXPelsPerMeter', w.LONG),
                ('biYPelsPerMeter', w.LONG), ('biClrUsed', w.DWORD), ('biClrImportant', w.DWORD),
                ('colors', w.DWORD * 3)]


def windows(title):
    """Top-level windows whose title is exactly `title`."""
    found = []

    def visit(window, _):
        length = user32.GetWindowTextLengthW(window)
        if length == len(title):
            buffer = c.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(window, buffer, length + 1)
            if buffer.value == title:
                found.append(window)
        return True
    user32.EnumWindows(WNDENUMPROC(visit), 0)
    return found


def wait_window(title, process, log, seconds=20):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        found = windows(title)
        if found:
            return found[0]
        if process.poll() is not None:
            raise RuntimeError(f'{title}: process ended: ' + log.read_text(errors='replace'))
        time.sleep(.05)
    raise RuntimeError(f'{title}: no window: ' + log.read_text(errors='replace'))


def client_size(window):
    rect = w.RECT()
    user32.GetClientRect(window, c.byref(rect))
    return rect.right, rect.bottom


def client_origin(window):
    point = w.POINT(0, 0)
    user32.ClientToScreen(window, c.byref(point))
    return point.x, point.y


def window_rect(window):
    rect = w.RECT()
    user32.GetWindowRect(window, c.byref(rect))
    return rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top


def resize(window, width, height, seconds=5):
    """Sets the client area's size without activating the window; True once it applies."""
    rect = w.RECT(0, 0, width, height)
    user32.AdjustWindowRectExForDpi(c.byref(rect), user32.GetWindowLongPtrW(window, GWL_STYLE) &
                                    0xffffffff, False,
                                    user32.GetWindowLongPtrW(window, GWL_EXSTYLE) & 0xffffffff,
                                    user32.GetDpiForWindow(window))
    user32.SetWindowPos(window, None, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if client_size(window) == (width, height):
            return True
        time.sleep(.02)
    return False


def move(window, x, y):
    user32.SetWindowPos(window, None, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)


def _bitmap(width, height):
    info = BitmapInfo()
    info.biSize = c.sizeof(BitmapInfo) - c.sizeof(w.DWORD * 3)
    info.biWidth, info.biHeight = width, -height  # top-down rows
    info.biPlanes, info.biBitCount = 1, 32
    bits = c.c_void_p()
    screen = user32.GetDC(None)
    memory = gdi32.CreateCompatibleDC(screen)
    bitmap = gdi32.CreateDIBSection(memory, c.byref(info), 0, c.byref(bits), None, 0)
    user32.ReleaseDC(None, screen)
    if not bitmap:
        gdi32.DeleteDC(memory)
        raise RuntimeError('CreateDIBSection failed')
    previous = gdi32.SelectObject(memory, bitmap)
    return memory, bitmap, previous, bits


def _rgb(bits, width, height, memory, bitmap, previous):
    gdi32.GdiFlush()
    raw = c.string_at(bits, width * height * 4)
    gdi32.SelectObject(memory, previous)
    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(memory)
    rgb = bytearray(width * height * 3)
    rgb[0::3], rgb[1::3], rgb[2::3] = raw[2::4], raw[1::4], raw[0::4]
    return bytes(rgb)


def image(window):
    """The window's client area as composed by DWM (Vulkan and DirectX content included)."""
    width, height = client_size(window)
    if not width or not height:
        raise RuntimeError('window has no client area')
    memory, bitmap, previous, bits = _bitmap(width, height)
    # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    ok = user32.PrintWindow(window, memory, 3)
    pixels = _rgb(bits, width, height, memory, bitmap, previous)
    if not ok:
        raise RuntimeError('PrintWindow failed')
    return width, height, pixels


def screen(x, y, width, height):
    """The composed desktop at screen coordinates (what the user sees)."""
    memory, bitmap, previous, bits = _bitmap(width, height)
    desktop = user32.GetDC(None)
    ok = gdi32.BitBlt(memory, 0, 0, width, height, desktop, x, y, 0x00CC0020 | 0x40000000)
    user32.ReleaseDC(None, desktop)
    pixels = _rgb(bits, width, height, memory, bitmap, previous)
    if not ok:
        raise RuntimeError('BitBlt of the screen failed')
    return width, height, pixels


def save(path, picture):
    width, height, pixels = picture
    path.write_bytes(f'P6\n{width} {height}\n255\n'.encode() + pixels)
    return path


def _key_parameter(virtual, release):
    scan = user32.MapVirtualKeyW(virtual, 0)
    value = 1 | (scan << 16)
    if release:
        value |= (1 << 30) | (1 << 31)
    return value


def key(window, virtual, character=None):
    """Posts a key press and release (and its character) to the window's queue,
    paced like the X11 driver, so each lands in its own frame."""
    user32.PostMessageW(window, WM_KEYDOWN, virtual, _key_parameter(virtual, False))
    if character:
        user32.PostMessageW(window, WM_CHAR, ord(character), _key_parameter(virtual, False))
    time.sleep(.05)
    user32.PostMessageW(window, WM_KEYUP, virtual, _key_parameter(virtual, True))
    time.sleep(.12)


def click(window, x, y):
    """Posts a left click at client coordinates, paced like the X11 driver."""
    position = (y << 16) | (x & 0xffff)
    user32.PostMessageW(window, WM_MOUSEMOVE, 0, position)
    user32.PostMessageW(window, WM_LBUTTONDOWN, 1, position)
    time.sleep(.06)
    user32.PostMessageW(window, WM_LBUTTONUP, 0, position)
    time.sleep(.18)


def close(window):
    user32.PostMessageW(window, WM_CLOSE, 0, 0)


def above(window):
    """The window directly above in the z-order, or None."""
    return user32.GetWindow(window, GW_HWNDPREV) or None


def visible_above(window):
    """The nearest visible window above in the z-order, or None."""
    current = user32.GetWindow(window, GW_HWNDPREV)
    while current and not user32.IsWindowVisible(current):
        current = user32.GetWindow(current, GW_HWNDPREV)
    return current or None


def z_order():
    """Visible top-level windows, topmost first."""
    order = []

    def visit(window, _):
        if user32.IsWindowVisible(window):
            order.append(window)
        return True
    user32.EnumWindows(WNDENUMPROC(visit), 0)
    return order


def raise_window(window):
    """Brings a window to the top of its band without activating it."""
    user32.SetWindowPos(window, w.HWND(0), 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE)


SW_HIDE, SW_SHOWNOACTIVATE, SW_SHOWMINNOACTIVE, SW_SHOWNA = 0, 4, 7, 8


def show(window, command):
    """ShowWindow with one of the SW_ commands above (none activates the window)."""
    user32.ShowWindow.argtypes = [w.HWND, c.c_int]
    user32.ShowWindow(window, command)


def extended_style(window):
    return user32.GetWindowLongPtrW(window, GWL_EXSTYLE) & 0xffffffff


def window_at(x, y):
    """The top-level window that receives the pointer at a screen point."""
    found = user32.WindowFromPoint(w.POINT(x, y))
    return user32.GetAncestor(found, 2) if found else None  # GA_ROOT


def exists(window):
    return bool(user32.IsWindow(window))


def visible(window):
    return bool(user32.IsWindowVisible(window)) and not user32.IsIconic(window)


def difference(a, b):
    """Largest channel difference and the share of equal pixels."""
    worst = max(abs(x - y) for x, y in zip(a, b))
    same = sum(1 for i in range(0, len(a), 3) if a[i:i + 3] == b[i:i + 3])
    return worst, same / (len(a) // 3)
