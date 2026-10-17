#include "window.hpp"

#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace fixture {
Window::Window() : display(XOpenDisplay(nullptr)) {
  if (!display)
    throw std::runtime_error(
        "Cannot open X11 display; check DISPLAY and Xauthority");
}
Window::~Window() { close(); }
void Window::close() {
  if (!display) return;
  if (handle) XDestroyWindow(display, handle);
  if (colormap_) XFreeColormap(display, colormap_);
  XCloseDisplay(display);
  handle = 0;
  colormap_ = 0;
  display = nullptr;
}
void Window::create(XVisualInfo* visual, const char* title) {
  const auto root = RootWindow(display, visual->screen);
  colormap_ = XCreateColormap(display, root, visual->visual, AllocNone);
  XSetWindowAttributes attrs{};
  attrs.colormap = colormap_;
  attrs.event_mask = StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                     ExposureMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | FocusChangeMask;
  if (const char* mode = std::getenv("GLYPHSHIFT_FIXTURE_INPUT_MODE"))
    attrs.event_mask =
        StructureNotifyMask | ExposureMask |
        (std::string_view(mode) == "press-only" ? KeyPressMask
                                                : KeyReleaseMask);
  handle = XCreateWindow(display, root, 0, 0, width, height, 0, visual->depth,
                         InputOutput, visual->visual, CWColormap | CWEventMask,
                         &attrs);
  if (!handle) throw std::runtime_error("XCreateWindow failed");
  const char* suffix = std::getenv("GLYPHSHIFT_FIXTURE_TITLE_SUFFIX");
  const std::string name = std::string(title) + (suffix ? suffix : "");
  XStoreName(display, handle, name.c_str());
  if (std::getenv("GLYPHSHIFT_FIXTURE_FLOATING")) {
    const auto type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    const auto dialog =
        XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(display, handle, type, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&dialog), 1);
  }
  close_ = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(display, handle, &close_, 1);
  // Automated input checks send events directly; leave desktop keyboard
  // focus with the user's application during those runs.
  if (std::getenv("GLYPHSHIFT_FIXTURE_NO_FOCUS")) {
    XWMHints hints{};
    hints.flags = InputHint;
    hints.input = False;
    XSetWMHints(display, handle, &hints);
  }
  XMapWindow(display, handle);
  XFlush(display);
}
bool Window::poll() {
  resized = false;
  while (XPending(display)) {
    XEvent event{};
    XNextEvent(display, &event);
    if (event.type == ClientMessage &&
        event.xclient.message_type ==
            XInternAtom(display, "WM_PROTOCOLS", False) &&
        static_cast<Atom>(event.xclient.data.l[0]) == close_)
      return false;
    if (std::getenv("GLYPHSHIFT_FIXTURE_INPUT_LOG")) {
      if (event.type == KeyPress || event.type == KeyRelease)
        std::cerr << "Fixture input: "
                  << (event.type == KeyPress ? "key down " : "key up ")
                  << XLookupKeysym(&event.xkey, 0) << '\n';
      if (event.type == ButtonPress || event.type == ButtonRelease)
        std::cerr << "Fixture input: "
                  << (event.type == ButtonPress ? "button down " : "button up ")
                  << event.xbutton.button << '\n';
    }
    if (event.type == KeyPress && XLookupKeysym(&event.xkey, 0) == XK_Escape)
      return false;
    if (event.type == ConfigureNotify) {
      const auto w = static_cast<unsigned>(event.xconfigure.width);
      const auto h = static_cast<unsigned>(event.xconfigure.height);
      resized = resized || w != width || h != height;
      width = w;
      height = h;
    }
  }
  return true;
}
glyphshift::FixtureOptions options(int argc, char** argv, bool vulkan) {
  std::vector<std::string_view> args;
  for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
  const auto result = glyphshift::parse_fixture_options(args, vulkan);
  if (result.help) {
    std::cout << "Usage: " << argv[0] << " [--frames N] [--fixed-time SECONDS]"
              << (vulkan ? " [--validation]" : "")
              << "\nEscape or the window close button exits. Animation "
                 "advances at 1/60 s per frame.\n";
  }
  return result;
}
std::string read_text(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot open shader: " + path);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}
std::vector<std::uint32_t> read_spirv(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("Cannot open SPIR-V: " + path);
  const auto size = input.tellg();
  if (size <= 0 || size % 4 != 0)
    throw std::runtime_error("Invalid SPIR-V size: " + path);
  std::vector<std::uint32_t> code(static_cast<std::size_t>(size) / 4);
  input.seekg(0);
  if (!input.read(reinterpret_cast<char*>(code.data()), size))
    throw std::runtime_error("Cannot read SPIR-V: " + path);
  return code;
}
void pace(std::chrono::steady_clock::time_point start, std::uint64_t frame) {
  std::this_thread::sleep_until(
      start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                  std::chrono::duration<double>(static_cast<double>(frame) /
                                                glyphshift::fixture_rate)));
}
}  // namespace fixture
