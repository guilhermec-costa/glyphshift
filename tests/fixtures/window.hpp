#pragma once
#ifdef _WIN32
#include <windows.h>
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#include <chrono>
#include <cstdint>
#include <glyphshift/core/fixture.hpp>
#include <string>
#include <vector>

namespace fixture {
#ifdef _WIN32
// Win32 fixture window: client area of width x height physical pixels (the
// process is per-monitor DPI aware). Escape or closing the window ends it.
class Window {
public:
    Window();
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    void create(const char* title);
    void close();
    bool poll();
    bool resized = false;
    unsigned width = 800, height = 600;
    HWND handle = nullptr;

private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l);
    bool closed_ = false;
};
#else
class Window {
public:
    Window();
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    void create(XVisualInfo* visual, const char* title);
    void close();
    bool poll();
    bool resized = false;
    unsigned width = 800, height = 600;
    Display* display = nullptr;
    ::Window handle = 0;

private:
    Colormap colormap_ = 0;
    Atom close_ = 0;
};
#endif

glyphshift::FixtureOptions options(int argc, char** argv, bool vulkan);
std::string read_text(const std::string& path);
std::vector<std::uint32_t> read_spirv(const std::string& path);
void pace(std::chrono::steady_clock::time_point start, std::uint64_t frame);
}  // namespace fixture
