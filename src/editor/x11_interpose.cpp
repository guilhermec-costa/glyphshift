#include <X11/Xlib.h>
#include <dlfcn.h>

#include <cerrno>
#include <cstdlib>
#include <glyphshift/editor/editor.hpp>
#include <map>
#include <mutex>
namespace {
template <class T>
T next(const char* name) {
  return reinterpret_cast<T>(dlsym(RTLD_NEXT, name));
}
constexpr long editor_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask |
                             ButtonReleaseMask | PointerMotionMask |
                             FocusChangeMask;
struct Masks {
  std::mutex mutex;
  std::map<std::pair<Display*, Window>, long> windows;
};
Masks& masks() {
  static Masks data;
  return data;
}
void attach(void* pointer, std::uint64_t window) {
  auto* display = static_cast<Display*>(pointer);
  if (!display || !window) return;
  auto& data = masks();
  const std::lock_guard lock(data.mutex);
  if (data.windows.contains({display, window})) return;
  static const auto attributes =
      next<decltype(&XGetWindowAttributes)>("XGetWindowAttributes");
  static const auto select = next<decltype(&XSelectInput)>("XSelectInput");
  XWindowAttributes value{};
  if (attributes && select && attributes(display, window, &value)) {
    data.windows.emplace(std::pair{display, window}, value.your_event_mask);
    select(display, window, value.your_event_mask | editor_mask);
  }
}
void detach(void* pointer, std::uint64_t window) {
  auto* display = static_cast<Display*>(pointer);
  auto& data = masks();
  const std::lock_guard lock(data.mutex);
  const auto found = data.windows.find({display, window});
  if (found == data.windows.end()) return;
  static const auto select = next<decltype(&XSelectInput)>("XSelectInput");
  if (select) select(display, window, found->second);
  data.windows.erase(found);
}
void install() {
  static const bool done = [] {
    glyphshift::editor::input_adapter(attach, detach);
    return true;
  }();
  (void)done;
}
bool consume(XEvent* event) {
  // Focused regressions deliberately bypass every legacy event filter, even
  // if a driver happens to call an interposed Xlib function internally.
  if (std::getenv("GLYPHSHIFT_FIXTURE_PRIVATE_XLIB")) return false;
  using namespace glyphshift::editor;
  Input input{};
  switch (event->type) {
    case KeyPress:
    case KeyRelease: {
      static const auto lookup =
          next<decltype(&XLookupKeysym)>("XLookupKeysym");
      if (!lookup) return false;
      input.kind = event->type == KeyPress ? Kind::key_down : Kind::key_up;
      input.code = static_cast<unsigned>(
          lookup(&event->xkey, (event->xkey.state & ShiftMask) ? 1 : 0));
      input.physical = event->xkey.keycode;
      input.time = event->xkey.time;
      input.modified =
          (event->xkey.state & (ControlMask | Mod1Mask | Mod4Mask)) != 0;
      break;
    }
    case ButtonPress:
    case ButtonRelease:
      input.kind =
          event->type == ButtonPress ? Kind::button_down : Kind::button_up;
      input.code = event->xbutton.button;
      input.x = static_cast<float>(event->xbutton.x);
      input.y = static_cast<float>(event->xbutton.y);
      break;
    case MotionNotify:
      input.kind = Kind::motion;
      input.x = static_cast<float>(event->xmotion.x);
      input.y = static_cast<float>(event->xmotion.y);
      break;
    case FocusOut:
      if (event->xfocus.detail == NotifyInferior ||
          event->xfocus.mode != NotifyNormal)
        return false;
      input.kind = Kind::focus_lost;
      break;
    default:
      return false;
  }
  if (glyphshift::editor::input(event->xany.display, event->xany.window, input))
    return true;
  // Receive missing key-up/pointer events for the UI without exposing events
  // the host never selected when normal application input is restored.
  long mask = 0;
  switch (event->type) {
    case KeyPress:
      mask = KeyPressMask;
      break;
    case KeyRelease:
      mask = KeyReleaseMask;
      break;
    case ButtonPress:
      mask = ButtonPressMask;
      break;
    case ButtonRelease:
      mask = ButtonReleaseMask;
      break;
    case MotionNotify:
      mask = PointerMotionMask | PointerMotionHintMask;
      for (unsigned i = 0; i < 5; ++i)
        if (event->xmotion.state & (Button1Mask << i))
          mask |= ButtonMotionMask | (Button1MotionMask << i);
      break;
    case FocusOut:
      mask = FocusChangeMask;
      break;
  }
  auto& data = masks();
  const std::lock_guard lock(data.mutex);
  const auto found =
      data.windows.find({event->xany.display, event->xany.window});
  return found != data.windows.end() && !(found->second & mask);
}
int filter(Display* display, int count) {
  static const auto peek = next<decltype(&XPeekEvent)>("XPeekEvent");
  static const auto take = next<decltype(&XNextEvent)>("XNextEvent");
  static const auto queued = next<decltype(&XEventsQueued)>("XEventsQueued");
  if (!peek || !take || !queued) return count;
  // Process only the head: preserve ordering and never call a blocking
  // XNextEvent after reporting a queue containing only captured UI events.
  while (count > 0) {
    XEvent event{};
    peek(display, &event);
    if (!consume(&event)) break;
    take(display, &event);
    count = queued(display, QueuedAlready);
  }
  return count;
}
}  // namespace
#define EXPORT extern "C" __attribute__((visibility("default")))
EXPORT int XPending(Display* display) {
  install();
  static const auto real = next<decltype(&XPending)>("XPending");
  const int saved = errno;
  const int result = real ? filter(display, real(display)) : 0;
  errno = saved;
  return result;
}
EXPORT int XEventsQueued(Display* display, int mode) {
  install();
  static const auto real = next<decltype(&XEventsQueued)>("XEventsQueued");
  const int saved = errno;
  const int result = real ? filter(display, real(display, mode)) : 0;
  errno = saved;
  return result;
}
EXPORT int XNextEvent(Display* display, XEvent* event) {
  install();
  static const auto real = next<decltype(&XNextEvent)>("XNextEvent");
  if (!real) return 0;
  int result;
  do {
    result = real(display, event);
  } while (consume(event));
  return result;
}
EXPORT int XPeekEvent(Display* display, XEvent* event) {
  install();
  static const auto real = next<decltype(&XPeekEvent)>("XPeekEvent");
  static const auto take = next<decltype(&XNextEvent)>("XNextEvent");
  if (!real || !take) return 0;
  while (true) {
    const int result = real(display, event);
    if (!consume(event)) return result;
    take(display, event);
  }
}
EXPORT int XDestroyWindow(Display* display, Window window) {
  static const auto real = next<decltype(&XDestroyWindow)>("XDestroyWindow");
  glyphshift::editor::forget(display, window);
  return real ? real(display, window) : 0;
}
// Nonblocking selection APIs must continue searching after a captured event.
EXPORT Bool XCheckWindowEvent(Display* d, Window w, long mask, XEvent* e) {
  install();
  static const auto real =
      next<decltype(&XCheckWindowEvent)>("XCheckWindowEvent");
  while (real && real(d, w, mask, e)) {
    if (!consume(e)) return True;
  }
  return False;
}
EXPORT Bool XCheckMaskEvent(Display* d, long mask, XEvent* e) {
  install();
  static const auto real = next<decltype(&XCheckMaskEvent)>("XCheckMaskEvent");
  while (real && real(d, mask, e)) {
    if (!consume(e)) return True;
  }
  return False;
}
EXPORT Bool XCheckTypedEvent(Display* d, int type, XEvent* e) {
  install();
  static const auto real =
      next<decltype(&XCheckTypedEvent)>("XCheckTypedEvent");
  while (real && real(d, type, e)) {
    if (!consume(e)) return True;
  }
  return False;
}

EXPORT int XSelectInput(Display* display, Window window, long mask) {
  static const auto real = next<decltype(&XSelectInput)>("XSelectInput");
  auto& data = masks();
  const std::lock_guard lock(data.mutex);
  const auto found = data.windows.find({display, window});
  if (found != data.windows.end()) {
    found->second = mask;
    mask |= editor_mask;
  }
  return real ? real(display, window, mask) : 0;
}
EXPORT int XCloseDisplay(Display* display) {
  // No X requests during connection teardown: some windows may already have
  // been destroyed by the server. Retire CPU editor state before the display.
  auto& data = masks();
  while (true) {
    Window window = 0;
    {
      const std::lock_guard lock(data.mutex);
      auto it = data.windows.lower_bound({display, 0});
      if (it == data.windows.end() || it->first.first != display) break;
      window = it->first.second;
      data.windows.erase(it);
    }
    glyphshift::editor::forget(display, window);
  }
  static const auto real = next<decltype(&XCloseDisplay)>("XCloseDisplay");
  return real ? real(display) : 0;
}
