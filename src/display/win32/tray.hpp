#pragma once
#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// The daemon's notification-area icon for Win32 (window-system layer,
// Shell_NotifyIcon): a popup menu and the daemon's notifications, which
// Windows shows as toasts. The icon is added again whenever Explorer
// (re)starts its taskbar (TaskbarCreated). Without a taskbar nothing is shown;
// the hotkey works as before.
namespace glyphshift::display::win32 {
struct MenuItem {
    std::string label;
    int id = -1;  // Chosen rows report their id; negative ids are not selectable.
    bool checkable = false, checked = false, separator = false;
};

class Tray {
public:
    // `tip` is the icon's tooltip. Opening the menu makes the daemon the
    // foreground application, as Windows needs for the menu to close on a
    // click elsewhere, unless `take_foreground` is false (tests).
    Tray(const std::string& tip, bool take_foreground);
    ~Tray();
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    // Processes one message of the caller's queue before it is dispatched.
    // Opens the menu (rows built by `menu`) after a click on the icon; returns
    // the id of the row chosen, or -1.
    int handle(const MSG& message, const std::function<std::vector<MenuItem>()>& menu);

    // Shows a notification from the icon; false when there is no icon.
    bool notify(const std::string& summary, const std::string& body);

    // True while a notification shown by notify() is on screen.
    bool notifying() const;

    // The hidden window that owns the icon and receives its messages.
    HWND window() const;
    bool shown() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
}  // namespace glyphshift::display::win32
