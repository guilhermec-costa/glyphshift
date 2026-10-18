#pragma once
#include <X11/Xlib.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// System tray icon with a popup menu for X11 (window-system layer). It follows
// the freedesktop System Tray protocol (XEmbed): the icon docks into the tray
// manager when one exists, and again whenever one appears or restarts.
// Without a tray manager nothing is shown.
namespace glyphshift::display::x11 {
struct MenuItem {
    std::string label;
    int id = -1;  // Chosen rows report their id; negative ids are not selectable.
    bool checkable = false, checked = false, separator = false;
};

class Tray {
public:
    // Uses `display`, the caller's connection, for all requests; the caller
    // passes every event of that connection to handle().
    explicit Tray(Display* display);
    ~Tray();
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    // Processes one event. Opens the menu (rows built by `menu`) on a click on
    // the icon; returns the id of the row chosen, or -1.
    int handle(const XEvent& event, const std::function<std::vector<MenuItem>()>& menu);

    // The icon window, or 0; docked() tells whether a manager embeds it.
    Window icon() const;
    bool docked() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
}  // namespace glyphshift::display::x11
