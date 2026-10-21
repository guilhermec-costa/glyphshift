#include <windows.h>
// After windows.h:
#include <crtdbg.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glyphshift/config/presets.hpp>
#include <glyphshift/config/settings.hpp>
#include <glyphshift/editor/editor.hpp>
#include <glyphshift/platform/process.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "../../src/display/win32/capture.hpp"
#include "../../src/display/win32/overlay_window.hpp"
#include "../../src/display/win32/tracker.hpp"
#include "../../src/render/vulkan/owned.hpp"
#include "../../src/render/vulkan/win32_import.hpp"
#include "options.hpp"

// Standalone capture overlay on Windows (M7). Never touches the target
// process: Windows.Graphics.Capture reads the target window (display::Capture),
// the Vulkan renderer runs the glyph pipeline on the imported frame, and the
// result is presented in an input-transparent window over the target's client
// area (display::Overlay). Presets are kept per game, in a profile derived from
// the target's executable.
namespace {
namespace win32 = glyphshift::display::win32;
using glyphshift::overlay::Options;

class Overlay {
public:
    explicit Overlay(const Options& options)
        : options_(options),
          target_(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(options.target))),
          tracker_(target_) {
        if (!IsWindow(target_))
            throw std::runtime_error("no window " + std::to_string(options.target));
        glyphshift::overlay::choose_profile(tracker_.application());
        if (!options.preset.empty()) {
            if (!glyphshift::builtin_preset(options.preset))
                throw std::invalid_argument("unknown included style: " + options.preset);
            // The starting style; the editor and saved presets take over from here.
            glyphshift::platform::set_environment("GLYPHSHIFT_PRESET", options.preset);
        }
        glyphshift::vulkan::OwnedRenderer::Options vulkan;
        vulkan.instance_extensions = {VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        vulkan.device_extensions.assign(std::begin(glyphshift::vulkan::Win32Import::extensions),
            std::end(glyphshift::vulkan::Win32Import::extensions));
        vulkan.timeline_semaphores = true;
        const HWND window = window_.handle();
        renderer_ = std::make_unique<glyphshift::vulkan::OwnedRenderer>(vulkan,
            [window](VkInstance instance) {
                auto surface = glyphshift::vulkan::info<VkWin32SurfaceCreateInfoKHR>(
                    VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR);
                surface.hinstance = GetModuleHandleW(nullptr);
                surface.hwnd = window;
                VkSurfaceKHR result = VK_NULL_HANDLE;
                glyphshift::vulkan::check(
                    vkCreateWin32SurfaceKHR(instance, &surface, nullptr, &result),
                    "create the overlay's Vulkan surface");
                return result;
            });
        LUID adapter{};
        std::memcpy(&adapter, renderer_->luid().data(), sizeof adapter);
        capture_ = std::make_unique<win32::Capture>(target_, adapter);
        source_ =
            std::make_unique<glyphshift::vulkan::Win32Import>(*renderer_, capture_->fence_value());
        source_->import_fence(capture_->fence());
        if (options.log)
            std::fprintf(stderr,
                "glyphshift-overlay: attached to window 0x%llx (%s), capture border %s\n",
                static_cast<unsigned long long>(options.target),
                renderer_->version(),
                capture_->borderless() ? "off" : "on");
        update();
    }

    ~Overlay() {
        glyphshift::editor::forget(display(), options_.target);
        // The renderer finishes its frames before the capture's texture goes.
        if (renderer_)
            renderer_->wait_idle();
        source_.reset();
        renderer_.reset();
        capture_.reset();
    }

    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    void run() {
        bool poll = true;
        while (!glyphshift::platform::stop_requested()) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (capture_->closed() || !tracker_.query().exists)
                return;
            if (capture_->update())
                dirty_ = true;
            update();
            // A new game frame, an open editor (continuous, vsync-paced UI) or
            // the idle polling interval runs one editor frame: it reads the
            // Insert/F10 hotkeys and UI input even while the game is paused.
            if (dirty_ || editor_open_ || poll) {
                present();
                poll = false;
                continue;
            }
            const HANDLE event = capture_->event();
            poll = MsgWaitForMultipleObjects(1, &event, FALSE, 50, QS_ALLINPUT) == WAIT_TIMEOUT;
        }
    }

private:
    const Options& options_;
    HWND target_;
    win32::Tracker tracker_;
    win32::OverlayWindow window_;
    std::unique_ptr<glyphshift::vulkan::OwnedRenderer> renderer_;
    std::unique_ptr<win32::Capture> capture_;
    std::unique_ptr<glyphshift::vulkan::Win32Import> source_;
    win32::Tracker::State state_{};
    uint64_t imported_ = 0;
    bool enabled_ = true, dirty_ = true, editor_open_ = false;
    unsigned frames_ = 0;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    double render_ms_ = 0;

    // The editor session's key for this overlay (the target's display has no
    // handle on Windows; any stable non-null pointer serves).
    static void* display() {
        static int key;
        return &key;
    }

    // Shown while the target is viewable and the effect is on, or the editor
    // is open over the original image after F10; follows its client area.
    void update() {
        state_ = tracker_.query();
        const auto& area = state_.client;
        const bool show = state_.viewable && (enabled_ || editor_open_) && area.right > area.left &&
                          area.bottom > area.top;
        if (!show) {
            window_.hide();
            return;
        }
        if (window_.show(area, target_))
            dirty_ = true;
    }

    unsigned width() const {
        return static_cast<unsigned>(state_.client.right - state_.client.left);
    }

    unsigned height() const {
        return static_cast<unsigned>(state_.client.bottom - state_.client.top);
    }

    void present() {
        const auto start = std::chrono::steady_clock::now();
        glyphshift::editor::Frame frame(display(),
            options_.target,
            width(),
            height(),
            "Overlay (Vulkan)",
            renderer_->version());
        const bool open = frame.draw_data != nullptr;
        if (frame.settings.enabled != enabled_ || open != editor_open_) {
            enabled_ = frame.settings.enabled;
            editor_open_ = open;
            update();
        }
        if (window_.shown() && (dirty_ || open) && render(frame.settings, frame.draw_data))
            dirty_ = false;
        frame.complete(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
    }

    bool render(const glyphshift::Settings& settings, const ImDrawData* ui) {
        if (!capture_->generation())
            return false;  // No frame captured yet.
        const auto start = std::chrono::steady_clock::now();
        if (capture_->generation() != imported_) {
            // A new size: the capture made a new shared texture.
            renderer_->wait_idle();
            source_->import_texture(capture_->texture(), {capture_->width(), capture_->height()});
            imported_ = capture_->generation();
        }
        if (!renderer_->draw(*source_, {width(), height()}, settings, ui))
            return false;
        render_ms_ +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        ++frames_;
        if (options_.log && frames_ % 120 == 0) {
            const double seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
            std::fprintf(stderr,
                "glyphshift-overlay: %u frames, %.1f fps, %.2f ms CPU per frame (incl. vsync)\n",
                frames_,
                frames_ / seconds,
                render_ms_ / frames_);
        }
        return true;
    }
};
}  // namespace

int main(int argc, char** argv) {
#ifdef _DEBUG
    // A failed debug check reports on stderr and ends the process instead of a
    // modal dialog that would freeze the overlay over the game.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    // Physical pixels: the capture, the target's geometry and the overlay agree.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        const auto options = glyphshift::overlay::parse(argc, argv);
        if (!options.autostart.empty() || !options.target)
            throw std::runtime_error(
                "the background mode and --autostart are not available on Windows yet; use "
                "--window HANDLE");
        glyphshift::platform::catch_stop_requests();
        Overlay overlay(options);
        overlay.run();
        return 0;
    } catch (const std::invalid_argument& error) {
        std::cerr << "glyphshift-overlay: " << error.what() << '\n' << glyphshift::overlay::usage;
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "glyphshift-overlay: " << error.what() << '\n';
        return 1;
    }
}
