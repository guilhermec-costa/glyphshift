#include "x11_input.hpp"

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <glyphshift/editor/editor.hpp>
#include <map>
#include <memory>

namespace glyphshift::editor::x11 {
namespace {
template <class T>
using Reply = std::unique_ptr<T, decltype(&std::free)>;

template <class T>
Reply<T> reply(T* value) {
    return Reply<T>(value, &std::free);
}

struct Connection {
    xcb_connection_t* connection = nullptr;
    xcb_key_symbols_t* symbols = nullptr;
    xcb_window_t surface = 0, top = 0, overlay = 0, previous_focus = 0;
    std::array<bool, 256> keys{};
    unsigned buttons = 0;
    bool mapped = false, failed = false;

    ~Connection() {
        if (symbols)
            xcb_key_symbols_free(symbols);
        // Disconnecting destroys our input-only child and releases passive grabs.
        // The host window and its event masks belong to another connection.
        if (connection)
            xcb_disconnect(connection);
    }

    void error(const char* message) {
        if (!failed) {
            std::fprintf(stderr, "glyphshift: editor input: %s\n", message);
            report(message);
        }
        failed = true;
        if (connection && top) {
            xcb_ungrab_key(connection, XCB_GRAB_ANY, top, XCB_MOD_MASK_ANY);
            xcb_flush(connection);
        }
    }

    bool checked(xcb_void_cookie_t cookie) {
        auto failure = reply(xcb_request_check(connection, cookie));
        return !failure;
    }

    bool descendant(xcb_window_t window, xcb_window_t ancestor) {
        for (unsigned i = 0; window && i < 32; ++i) {
            if (window == ancestor)
                return true;
            auto tree = reply(
                xcb_query_tree_reply(connection, xcb_query_tree(connection, window), nullptr));
            if (!tree || tree->parent == tree->root)
                return false;
            window = tree->parent;
        }
        return false;
    }

    void initialize(void* display, std::uint64_t window) {
        // Use this application's display name, including non-default X servers.
        connection = xcb_connect(DisplayString(static_cast<Display*>(display)), nullptr);
        if (!connection || xcb_connection_has_error(connection)) {
            error("Cannot connect editor input to the application's X11 display");
            return;
        }
        surface = static_cast<xcb_window_t>(window);
        const char name[] = "WM_STATE";
        auto atom = reply(xcb_intern_atom_reply(connection,
            xcb_intern_atom(connection, 0, sizeof(name) - 1, name),
            nullptr));
        top = surface;
        // Wine presents to an internal child. Stop at the application's managed
        // client, before the window manager's decoration or the root window.
        for (unsigned i = 0; i < 32; ++i) {
            if (atom) {
                auto property = reply(xcb_get_property_reply(connection,
                    xcb_get_property(connection,
                        0,
                        top,
                        atom->atom,
                        XCB_GET_PROPERTY_TYPE_ANY,
                        0,
                        0),
                    nullptr));
                if (property && property->type != XCB_ATOM_NONE)
                    break;
            }
            auto tree =
                reply(xcb_query_tree_reply(connection, xcb_query_tree(connection, top), nullptr));
            if (!tree) {
                error("Editor presentation window no longer exists");
                return;
            }
            if (top == tree->root) {
                error("Editor input requires an application window, not the X11 root");
                return;
            }
            if (tree->parent == tree->root || !tree->parent)
                break;
            top = tree->parent;
        }
        symbols = xcb_key_symbols_alloc(connection);
        if (!symbols) {
            error("Cannot read the X11 keyboard map for the editor");
            return;
        }
        overlay = xcb_generate_id(connection);
        const uint32_t mask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                              XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                              XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_FOCUS_CHANGE;
        if (!checked(xcb_create_window_checked(connection,
                0,
                overlay,
                top,
                0,
                0,
                1,
                1,
                0,
                XCB_WINDOW_CLASS_INPUT_ONLY,
                XCB_COPY_FROM_PARENT,
                XCB_CW_EVENT_MASK,
                &mask))) {
            error("Cannot create the editor's input-only child window");
            return;
        }
        const auto font = xcb_generate_id(connection);
        const auto cursor = xcb_generate_id(connection);
        xcb_open_font(connection, font, 6, "cursor");
        xcb_create_glyph_cursor(connection,
            cursor,
            font,
            font,
            XC_left_ptr,
            XC_left_ptr + 1,
            0,
            0,
            0,
            65535,
            65535,
            65535);
        xcb_change_window_attributes(connection, overlay, XCB_CW_CURSOR, &cursor);
        xcb_close_font(connection, font);
        xcb_free_cursor(connection, cursor);
        const char title[] = "GlyphShift input";
        xcb_change_property(connection,
            XCB_PROP_MODE_REPLACE,
            overlay,
            XCB_ATOM_WM_NAME,
            XCB_ATOM_STRING,
            8,
            sizeof(title) - 1,
            title);
        const uint32_t top_mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
        xcb_change_window_attributes(connection, top, XCB_CW_EVENT_MASK, &top_mask);
        grab_hotkeys();
        if (std::getenv("GLYPHSHIFT_LOG"))
            std::fprintf(stderr,
                "glyphshift: editor X11 input surface=0x%x client=0x%x "
                "input=0x%x\n",
                surface,
                top,
                overlay);
        xcb_flush(connection);
    }

    void grab_hotkeys() {
        // Window-local passive grabs activate only with focus in this client.
        // No root-window grabs, keyboard polling or desktop-wide hotkeys.
        for (xcb_keysym_t symbol : {0xff63U, 0xffc7U}) {
            auto codes = reply(xcb_key_symbols_get_keycode(symbols, symbol));
            if (!codes)
                continue;
            for (auto* code = codes.get(); *code; ++code)
                for (uint16_t modifiers : {uint16_t{0},
                         uint16_t{XCB_MOD_MASK_LOCK},
                         uint16_t{XCB_MOD_MASK_2},
                         uint16_t{XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2}}) {
                    if (!checked(xcb_grab_key_checked(connection,
                            0,
                            top,
                            modifiers,
                            *code,
                            XCB_GRAB_MODE_ASYNC,
                            XCB_GRAB_MODE_ASYNC))) {
                        error("Editor hotkey conflicts with another X11 grab");
                        return;
                    }
                }
        }
    }

    void hide() {
        if (!mapped)
            return;
        auto focus =
            reply(xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr));
        // Never take focus back from another application after Alt-Tab/click.
        if (focus && focus->focus == overlay && previous_focus)
            xcb_set_input_focus(connection,
                XCB_INPUT_FOCUS_PARENT,
                previous_focus,
                XCB_CURRENT_TIME);
        xcb_unmap_window(connection, overlay);
        mapped = false;
        keys.fill(false);
        buttons = 0;
        xcb_flush(connection);
    }

    void sync(bool& open) {
        if (!failed && xcb_connection_has_error(connection))
            error("The editor's X11 input connection was disconnected");
        if (failed) {
            open = false;
            hide();
            return;
        }
        if (!open) {
            // Retain the input child until UI-owned releases arrive, so Escape or
            // a title-bar close click cannot leak its release into the application.
            bool held = buttons != 0;
            for (bool pressed : keys)
                held |= pressed;
            if (mapped && held) {
                auto focus = reply(xcb_get_input_focus_reply(connection,
                    xcb_get_input_focus(connection),
                    nullptr));
                if (focus && descendant(focus->focus, top)) {
                    if (focus->focus != overlay) {
                        xcb_set_input_focus(connection,
                            XCB_INPUT_FOCUS_PARENT,
                            overlay,
                            XCB_CURRENT_TIME);
                        xcb_flush(connection);
                    }
                    return;
                }
            }
            hide();
            return;
        }
        auto focus =
            reply(xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr));
        if (!focus || (!descendant(focus->focus, top))) {
            open = false;
            hide();
            return;
        }
        auto geometry =
            reply(xcb_get_geometry_reply(connection, xcb_get_geometry(connection, top), nullptr));
        if (!geometry) {
            open = false;
            hide();
            return;
        }
        const uint32_t dimensions[] = {geometry->width, geometry->height, XCB_STACK_MODE_ABOVE};
        xcb_configure_window(connection,
            overlay,
            XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_STACK_MODE,
            dimensions);
        if (!mapped) {
            previous_focus = focus->focus;
            xcb_map_window(connection, overlay);
            xcb_set_input_focus(connection, XCB_INPUT_FOCUS_PARENT, overlay, XCB_CURRENT_TIME);
            mapped = true;
        } else if (focus->focus != overlay) {
            // A window manager may focus the managed client after a pointer move
            // or resize. Keep editor focus inside that same application while open.
            xcb_set_input_focus(connection, XCB_INPUT_FOCUS_PARENT, overlay, XCB_CURRENT_TIME);
        }
        xcb_flush(connection);
    }

    void poll(void* display, std::uint64_t window, bool& open) {
        if (failed) {
            open = false;
            return;
        }
        // Bounded work even if an application floods its X server connection.
        for (unsigned i = 0; i < 256; ++i) {
            auto event = reply(xcb_poll_for_event(connection));
            if (!event)
                break;
            const unsigned type = event->response_type & 0x7f;
            if (type == XCB_DESTROY_NOTIFY || type == XCB_UNMAP_NOTIFY) {
                const auto& e = *reinterpret_cast<xcb_destroy_notify_event_t*>(event.get());
                if (e.window == top) {
                    open = false;
                    hide();
                }
            }
            if (type == XCB_FOCUS_OUT) {
                const auto& focus = *reinterpret_cast<xcb_focus_out_event_t*>(event.get());
                // Retain a historical focus loss even if a paused game presents no
                // frames until the user returns. Ancestor focus changes by the WM
                // remain inside this client and are corrected in sync().
                if (focus.event == overlay && focus.mode == XCB_NOTIFY_MODE_NORMAL &&
                    (focus.detail == XCB_NOTIFY_DETAIL_NONLINEAR ||
                        focus.detail == XCB_NOTIFY_DETAIL_NONLINEAR_VIRTUAL)) {
                    input(display, window, {Kind::focus_lost});
                    hide();
                }
            }
            if (type == XCB_MAPPING_NOTIFY) {
                xcb_ungrab_key(connection, XCB_GRAB_ANY, top, XCB_MOD_MASK_ANY);
                xcb_refresh_keyboard_mapping(symbols,
                    reinterpret_cast<xcb_mapping_notify_event_t*>(event.get()));
                grab_hotkeys();
            }
            Input value{};
            if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE) {
                const auto& e = *reinterpret_cast<xcb_key_press_event_t*>(event.get());
                value.kind = type == XCB_KEY_PRESS ? Kind::key_down : Kind::key_up;
                value.physical = e.detail;
                value.time = e.time;
                value.code = xcb_key_symbols_get_keysym(symbols,
                    e.detail,
                    (e.state & XCB_MOD_MASK_SHIFT) ? 1 : 0);
                if ((e.state & XCB_MOD_MASK_LOCK) && value.code >= 'a' && value.code <= 'z')
                    value.code -= 'a' - 'A';
                value.modified = e.state & (XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1 | XCB_MOD_MASK_4);
                keys[e.detail] = type == XCB_KEY_PRESS;
            } else if (type == XCB_BUTTON_PRESS || type == XCB_BUTTON_RELEASE ||
                       type == XCB_MOTION_NOTIFY) {
                const auto& e = *reinterpret_cast<xcb_button_press_event_t*>(event.get());
                value.kind = type == XCB_MOTION_NOTIFY  ? Kind::motion
                             : type == XCB_BUTTON_PRESS ? Kind::button_down
                                                        : Kind::button_up;
                value.code = e.detail;
                if (type == XCB_BUTTON_PRESS && e.detail < 32)
                    buttons |= 1U << e.detail;
                if (type == XCB_BUTTON_RELEASE && e.detail < 32)
                    buttons &= ~(1U << e.detail);
                // Translate managed-client coordinates into the presentation child.
                auto at = reply(xcb_translate_coordinates_reply(connection,
                    xcb_translate_coordinates(connection, top, surface, e.event_x, e.event_y),
                    nullptr));
                if (!at)
                    continue;
                value.x = at->dst_x;
                value.y = at->dst_y;
            } else {
                continue;
            }
            input(display, window, value);
        }
        const bool was_open = open;
        sync(open);
        if (was_open && !open)
            input(display, window, {Kind::focus_lost});
    }
};

using Key = std::pair<void*, std::uint64_t>;

std::map<Key, Connection>& connections() {
    static std::map<Key, Connection> value;
    return value;
}
}  // namespace

void poll(void* display, std::uint64_t window, bool& open) {
    if (!display || !window)
        return;
    auto [it, inserted] = connections().try_emplace({display, window});
    if (inserted)
        it->second.initialize(display, window);
    it->second.poll(display, window, open);
}

void sync(void* display, std::uint64_t window, bool& open) {
    const auto it = connections().find({display, window});
    if (it != connections().end()) {
        const bool was_open = open;
        it->second.sync(open);
        if (was_open && !open)
            input(display, window, {Kind::focus_lost});
    }
}

void forget(void* display, std::uint64_t window) {
    const auto it = connections().find({display, window});
    if (it != connections().end()) {
        it->second.hide();
        connections().erase(it);
    }
}
}  // namespace glyphshift::editor::x11
