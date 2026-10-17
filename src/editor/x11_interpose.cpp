#include <X11/Xlib.h>
#include <dlfcn.h>

#include <cerrno>
#include <glyphshift/editor/editor.hpp>
namespace {
template <class T>
T next(const char* name) {
  return reinterpret_cast<T>(dlsym(RTLD_NEXT, name));
}
bool consume(XEvent* event) {
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
      input.kind = Kind::focus_lost;
      break;
    default:
      return false;
  }
  return glyphshift::editor::input(event->xany.display, event->xany.window,
                                   input);
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
  static const auto real = next<decltype(&XPending)>("XPending");
  const int saved = errno;
  const int result = real ? filter(display, real(display)) : 0;
  errno = saved;
  return result;
}
EXPORT int XEventsQueued(Display* display, int mode) {
  static const auto real = next<decltype(&XEventsQueued)>("XEventsQueued");
  const int saved = errno;
  const int result = real ? filter(display, real(display, mode)) : 0;
  errno = saved;
  return result;
}
EXPORT int XNextEvent(Display* display, XEvent* event) {
  static const auto real = next<decltype(&XNextEvent)>("XNextEvent");
  if (!real) return 0;
  int result;
  do {
    result = real(display, event);
  } while (consume(event));
  return result;
}
EXPORT int XPeekEvent(Display* display, XEvent* event) {
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
  static const auto real =
      next<decltype(&XCheckWindowEvent)>("XCheckWindowEvent");
  while (real && real(d, w, mask, e)) {
    if (!consume(e)) return True;
  }
  return False;
}
EXPORT Bool XCheckMaskEvent(Display* d, long mask, XEvent* e) {
  static const auto real = next<decltype(&XCheckMaskEvent)>("XCheckMaskEvent");
  while (real && real(d, mask, e)) {
    if (!consume(e)) return True;
  }
  return False;
}
EXPORT Bool XCheckTypedEvent(Display* d, int type, XEvent* e) {
  static const auto real =
      next<decltype(&XCheckTypedEvent)>("XCheckTypedEvent");
  while (real && real(d, type, e)) {
    if (!consume(e)) return True;
  }
  return False;
}
