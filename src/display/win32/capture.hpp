#pragma once
#include <windows.h>

#include <cstdint>
#include <memory>

// display::Capture for Win32: Windows.Graphics.Capture of one window (the
// official capture API; nothing enters the target process), on a D3D11 device
// of the renderer's adapter. Each new frame's client area is copied into a
// texture shared through an NT handle, ordered with the renderer by a shared
// fence: every use of the texture, a copy here or a read by the renderer,
// waits for the fence's last reserved value and signals the next one.
namespace glyphshift::display::win32 {
class Capture {
public:
    // Captures `target` on the adapter with `adapter`'s LUID, without the
    // capture border where Windows allows it and without the cursor. Throws
    // std::runtime_error when the window cannot be captured.
    Capture(HWND target, LUID adapter);
    ~Capture();
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;

    // Signaled when a frame arrives or the target closes.
    HANDLE event() const noexcept;

    // Copies the newest frame's client area into the shared texture; true when
    // a frame was copied. A new size recreates the texture (see generation()).
    bool update();

    // True once the target window is gone.
    bool closed() const noexcept;

    // The shared texture (B8G8R8A8_UNORM), its size and the shared fence.
    HANDLE texture() const noexcept;
    HANDLE fence() const noexcept;
    unsigned width() const noexcept;
    unsigned height() const noexcept;

    // Changes whenever the texture and its handle are recreated.
    uint64_t generation() const noexcept;

    // The fence's last reserved value; the renderer's uses advance it too.
    uint64_t& fence_value() noexcept;

    // Whether Windows removed the yellow capture border.
    bool borderless() const noexcept;

private:
    struct State;
    void start(HWND target, LUID adapter);
    bool copy();

    std::unique_ptr<State> state_;
};
}  // namespace glyphshift::display::win32
