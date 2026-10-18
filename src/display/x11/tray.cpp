#include "tray.hpp"

#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <algorithm>
#include <string>

namespace glyphshift::display::x11 {
namespace {
constexpr long request_dock = 0;
constexpr int padding = 6, check_column = 24, separator_height = 9;
// The application icon (data/glyphshift.svg): a "G" of glyph cells.
constexpr const char* glyph_rows[] =
    {".####.", "#....#", "#.....", "#..###", "#....#", "#....#", ".####."};
}  // namespace

struct Tray::State {
    Display* display;
    Window root, icon = 0, manager = 0, menu = 0;
    Atom selection, opcode, manager_message, xembed_info;
    unsigned icon_width = 22, icon_height = 22, menu_width = 0, menu_height = 0;
    XftFont* font = nullptr;
    XftDraw* draw = nullptr;
    XftColor background{}, highlight{}, border{}, text{}, dim{}, accent{};
    GC gc = nullptr;
    std::vector<MenuItem> rows;
    std::vector<int> tops;
    int row_height = 0, hover = -1;
    bool open = false;

    explicit State(Display* connection) : display(connection) {
        const int screen = DefaultScreen(display);
        root = RootWindow(display, screen);
        selection =
            XInternAtom(display, ("_NET_SYSTEM_TRAY_S" + std::to_string(screen)).c_str(), False);
        opcode = XInternAtom(display, "_NET_SYSTEM_TRAY_OPCODE", False);
        manager_message = XInternAtom(display, "MANAGER", False);
        xembed_info = XInternAtom(display, "_XEMBED_INFO", False);
        Visual* visual = DefaultVisual(display, screen);
        const Colormap colormap = DefaultColormap(display, screen);
        const auto color = [&](XftColor& target, const char* name) {
            XftColorAllocName(display, visual, colormap, name, &target);
        };
        color(background, "#151824");
        color(highlight, "#2c3349");
        color(border, "#3a4260");
        color(text, "#e6e8ef");
        color(dim, "#8a8fa3");
        color(accent, "#ffd166");
        font = XftFontOpenName(display, screen, "sans-serif:size=10");
        gc = XCreateGC(display, root, 0, nullptr);
        // A tray manager announces itself on the root window (MANAGER).
        XWindowAttributes attributes{};
        XGetWindowAttributes(display, root, &attributes);
        XSelectInput(display, root, attributes.your_event_mask | StructureNotifyMask);
        create_icon();
        XSetWindowAttributes values{};
        values.background_pixel = background.pixel;
        values.override_redirect = True;
        values.save_under = True;
        values.event_mask =
            ExposureMask | ButtonPressMask | PointerMotionMask | KeyPressMask | LeaveWindowMask;
        menu = XCreateWindow(display,
            root,
            0,
            0,
            1,
            1,
            0,
            CopyFromParent,
            InputOutput,
            CopyFromParent,
            CWBackPixel | CWEventMask | CWOverrideRedirect | CWSaveUnder,
            &values);
        XStoreName(display, menu, "GlyphShift menu");
        const Atom type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
        const Atom popup = XInternAtom(display, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
        XChangeProperty(display,
            menu,
            type,
            XA_ATOM,
            32,
            PropModeReplace,
            reinterpret_cast<const unsigned char*>(&popup),
            1);
        draw = XftDrawCreate(display, menu, visual, colormap);
        dock();
    }

    ~State() {
        close();
        if (draw)
            XftDrawDestroy(draw);
        if (font)
            XftFontClose(display, font);
        if (gc)
            XFreeGC(display, gc);
        if (menu)
            XDestroyWindow(display, menu);
        if (icon)
            XDestroyWindow(display, icon);
        XFlush(display);
    }

    // The icon window, offered to tray managers through XEmbed.
    void create_icon() {
        XSetWindowAttributes values{};
        values.background_pixel = background.pixel;
        values.event_mask = ExposureMask | ButtonPressMask | StructureNotifyMask;
        icon = XCreateWindow(display,
            root,
            0,
            0,
            icon_width,
            icon_height,
            0,
            CopyFromParent,
            InputOutput,
            CopyFromParent,
            CWBackPixel | CWEventMask,
            &values);
        XStoreName(display, icon, "GlyphShift");
        XClassHint hint{const_cast<char*>("glyphshift"), const_cast<char*>("GlyphShift")};
        XSetClassHint(display, icon, &hint);
        // XEmbed version 0, mapped once embedded.
        const unsigned long info[] = {0, 1};
        XChangeProperty(display,
            icon,
            xembed_info,
            xembed_info,
            32,
            PropModeReplace,
            reinterpret_cast<const unsigned char*>(info),
            2);
    }

    // Asks the current tray manager, if any, to embed the icon.
    void dock() {
        manager = XGetSelectionOwner(display, selection);
        if (!manager)
            return;
        XSelectInput(display, manager, StructureNotifyMask);
        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.window = manager;
        event.xclient.message_type = opcode;
        event.xclient.format = 32;
        event.xclient.data.l[0] = CurrentTime;
        event.xclient.data.l[1] = request_dock;
        event.xclient.data.l[2] = static_cast<long>(icon);
        XSendEvent(display, manager, False, NoEventMask, &event);
        XFlush(display);
    }

    void draw_icon() {
        XSetForeground(display, gc, background.pixel);
        XFillRectangle(display, icon, gc, 0, 0, icon_width, icon_height);
        const unsigned side = std::min(icon_width, icon_height);
        const unsigned cell = std::max(2U, side * 4 / 5 / 7);
        const int left = static_cast<int>(icon_width - cell * 6) / 2;
        const int top = static_cast<int>(icon_height - cell * 7) / 2;
        const unsigned gap = cell >= 4 ? 1 : 0;
        XSetForeground(display, gc, accent.pixel);
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 6; ++x)
                if (glyph_rows[y][x] == '#')
                    XFillRectangle(display,
                        icon,
                        gc,
                        left + x * static_cast<int>(cell),
                        top + y * static_cast<int>(cell),
                        cell - gap,
                        cell - gap);
    }

    int text_width(const std::string& label) const {
        XGlyphInfo extents{};
        XftTextExtentsUtf8(display,
            font,
            reinterpret_cast<const FcChar8*>(label.data()),
            static_cast<int>(label.size()),
            &extents);
        return extents.xOff;
    }

    void show(int x, int y) {
        if (!font)
            return;
        row_height = font->ascent + font->descent + 8;
        tops.clear();
        int height = padding, width = 0;
        for (const auto& row : rows) {
            tops.push_back(height);
            height += row.separator ? separator_height : row_height;
            if (!row.separator)
                width = std::max(width, text_width(row.label));
        }
        menu_width = static_cast<unsigned>(width + check_column + 2 * padding + 12);
        menu_height = static_cast<unsigned>(height + padding);
        // Keep the menu on screen; above the pointer near the bottom edge.
        const int screen = DefaultScreen(display);
        const int screen_width = DisplayWidth(display, screen);
        const int screen_height = DisplayHeight(display, screen);
        x = std::clamp(x, 0, std::max(0, screen_width - static_cast<int>(menu_width)));
        if (y + static_cast<int>(menu_height) > screen_height)
            y = std::max(0, y - static_cast<int>(menu_height));
        hover = -1;
        XMoveResizeWindow(display, menu, x, y, menu_width, menu_height);
        XMapRaised(display, menu);
        // Clicks outside close the menu; Escape closes it.
        XGrabPointer(display,
            menu,
            False,
            ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
            GrabModeAsync,
            GrabModeAsync,
            None,
            None,
            CurrentTime);
        XGrabKeyboard(display, menu, False, GrabModeAsync, GrabModeAsync, CurrentTime);
        open = true;
        XFlush(display);
    }

    void close() {
        if (!open)
            return;
        XUngrabPointer(display, CurrentTime);
        XUngrabKeyboard(display, CurrentTime);
        XUnmapWindow(display, menu);
        XFlush(display);
        open = false;
    }

    void draw_menu() {
        XftDrawRect(draw, &background, 0, 0, menu_width, menu_height);
        XSetForeground(display, gc, border.pixel);
        XDrawRectangle(display, menu, gc, 0, 0, menu_width - 1, menu_height - 1);
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            const int top = tops[i];
            if (row.separator) {
                XftDrawRect(draw,
                    &highlight,
                    padding,
                    top + separator_height / 2,
                    menu_width - 2 * padding,
                    1);
                continue;
            }
            if (static_cast<int>(i) == hover)
                XftDrawRect(draw,
                    &highlight,
                    2,
                    top,
                    menu_width - 4,
                    static_cast<unsigned>(row_height));
            if (row.checkable) {
                const int box = 8, left = padding + 6, middle = top + (row_height - box) / 2;
                if (row.checked)
                    XftDrawRect(draw, &accent, left, middle, box, box);
                else {
                    XSetForeground(display, gc, dim.pixel);
                    XDrawRectangle(display, menu, gc, left, middle, box - 1, box - 1);
                }
            }
            XftDrawStringUtf8(draw,
                row.id >= 0 ? &text : &dim,
                font,
                padding + check_column,
                top + 4 + font->ascent,
                reinterpret_cast<const FcChar8*>(row.label.data()),
                static_cast<int>(row.label.size()));
        }
        XFlush(display);
    }

    int row_at(int x, int y) const {
        if (x < 0 || y < 0 || x >= static_cast<int>(menu_width))
            return -1;
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (!rows[i].separator && y >= tops[i] && y < tops[i] + row_height)
                return rows[i].id >= 0 ? static_cast<int>(i) : -1;
        return -1;
    }

    // Selectable row after `from` in `step` direction, wrapping around.
    int next(int from, int step) const {
        const int count = static_cast<int>(rows.size());
        for (int i = 1; i <= count; ++i) {
            const int at = ((from < 0 && step < 0 ? count : from) + step * i + count * 2) % count;
            if (!rows[at].separator && rows[at].id >= 0)
                return at;
        }
        return -1;
    }
};

Tray::Tray(Display* display) : state_(std::make_unique<State>(display)) {}

Tray::~Tray() = default;

Window Tray::icon() const {
    return state_->icon;
}

bool Tray::docked() const {
    return state_->manager != 0;
}

int Tray::handle(const XEvent& event, const std::function<std::vector<MenuItem>()>& menu) {
    auto& s = *state_;
    const Window window = event.xany.window;
    switch (event.type) {
        case ClientMessage:
            // A tray manager started or restarted: dock into it.
            if (window == s.root && event.xclient.message_type == s.manager_message &&
                static_cast<Atom>(event.xclient.data.l[1]) == s.selection)
                s.dock();
            break;
        case DestroyNotify:
            if (window == s.icon && event.xdestroywindow.window == s.icon) {
                // A manager destroyed the icon together with itself: offer a new one.
                s.create_icon();
                s.dock();
            } else if (window == s.manager && event.xdestroywindow.window == s.manager) {
                // The manager is gone and the server returned the icon to the
                // root, where a window manager may take it. Offer a fresh icon
                // to the next manager instead.
                s.manager = 0;
                XDestroyWindow(s.display, s.icon);
                s.create_icon();
                XFlush(s.display);
            }
            break;
        case ConfigureNotify:
            if (window == s.icon) {
                s.icon_width = static_cast<unsigned>(std::max(1, event.xconfigure.width));
                s.icon_height = static_cast<unsigned>(std::max(1, event.xconfigure.height));
                s.draw_icon();
            }
            break;
        case Expose:
            if (window == s.icon && event.xexpose.count == 0)
                s.draw_icon();
            else if (window == s.menu && s.open && event.xexpose.count == 0)
                s.draw_menu();
            break;
        case ButtonPress:
            if (window == s.icon && !s.open) {
                s.rows = menu();
                s.show(event.xbutton.x_root, event.xbutton.y_root);
            } else if (window == s.menu && s.open) {
                const int row = s.row_at(event.xbutton.x, event.xbutton.y);
                s.close();
                if (row >= 0)
                    return s.rows[static_cast<std::size_t>(row)].id;
            }
            break;
        case MotionNotify:
            if (window == s.menu && s.open) {
                const int row = s.row_at(event.xmotion.x, event.xmotion.y);
                if (row != s.hover) {
                    s.hover = row;
                    s.draw_menu();
                }
            }
            break;
        case KeyPress:
            if (window == s.menu && s.open) {
                const KeySym key = XLookupKeysym(const_cast<XKeyEvent*>(&event.xkey), 0);
                if (key == XK_Escape)
                    s.close();
                else if (key == XK_Down || key == XK_Up) {
                    s.hover = s.next(s.hover, key == XK_Down ? 1 : -1);
                    s.draw_menu();
                } else if ((key == XK_Return || key == XK_KP_Enter || key == XK_space) &&
                           s.hover >= 0) {
                    const int id = s.rows[static_cast<std::size_t>(s.hover)].id;
                    s.close();
                    return id;
                }
            }
            break;
        default:
            break;
    }
    return -1;
}
}  // namespace glyphshift::display::x11
