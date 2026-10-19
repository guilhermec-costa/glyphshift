#include <X11/Xlib.h>
#include <sys/select.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <glyphshift/config/presets.hpp>
#include <glyphshift/config/settings.hpp>
#include <glyphshift/editor/editor.hpp>
#include <glyphshift/platform/desktop.hpp>
#include <glyphshift/platform/process.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../../src/display/x11/capture.hpp"
#include "../../src/display/x11/editor_input.hpp"
#include "../../src/display/x11/global_hotkey.hpp"
#include "../../src/display/x11/glx_loader.hpp"
#include "../../src/display/x11/overlay_window.hpp"
#include "../../src/display/x11/tracker.hpp"
#include "../../src/render/opengl/owned.hpp"
#include "daemon.hpp"
#include "options.hpp"

// Standalone capture overlay (M6). Never touches the target process: the X11
// window system captures the target window's pixels (display::Capture), the
// OpenGL renderer runs the glyph pipeline, and the result is presented in an
// input-transparent window kept directly above the target (display::Overlay).
// The shared editor (Insert/F10) works as in the injected mode, with presets
// kept per game in a profile derived from the application's class.
namespace {
namespace x11 = glyphshift::display::x11;

using glyphshift::overlay::Options;

// Start-on-login for the background mode, with the same hotkey and style.
int autostart(const Options& options) {
    if (options.autostart == "off") {
        const bool removed = glyphshift::platform::disable_autostart();
        std::cout << (removed ? "GlyphShift will no longer start at login\n"
                              : "GlyphShift was not set to start at login\n");
        return 0;
    }
    std::vector<std::string> command{glyphshift::platform::executable_path().string()};
    if (!options.hotkey.empty()) {
        x11::GlobalHotkey::check(options.hotkey);
        command.insert(command.end(), {"--hotkey", options.hotkey});
    }
    if (!options.preset.empty()) {
        if (!glyphshift::builtin_preset(options.preset))
            throw std::invalid_argument("unknown included style: " + options.preset);
        command.insert(command.end(), {"--preset", options.preset});
    }
    glyphshift::platform::enable_autostart(command);
    std::cout << "GlyphShift will start in the background at login ("
              << glyphshift::platform::autostart_entry().string() << ")\n";
    return 0;
}

class Overlay {
public:
    explicit Overlay(const Options& options) : options_(options) {
        display_ = XOpenDisplay(nullptr);
        if (!display_)
            throw std::runtime_error("cannot open X11 display");
        try {
            capture_ = std::make_unique<x11::Capture>(display_, options.target);
            tracker_ = std::make_unique<x11::Tracker>(display_, options.target);
            glyphshift::overlay::choose_profile(tracker_->application_class());
            if (!options.preset.empty()) {
                if (!glyphshift::builtin_preset(options.preset))
                    throw std::invalid_argument("unknown included style: " + options.preset);
                // The starting style; the editor and saved presets take over from here.
                glyphshift::platform::set_environment("GLYPHSHIFT_PRESET", options.preset);
            }
            capture_->start();
            const auto initial = tracker_->query();
            surface_ = std::make_unique<x11::OverlayWindow>(display_,
                initial.width ? initial.width : 1,
                initial.height ? initial.height : 1);
            glyphshift::opengl::load_api(x11::glx_proc);
            renderer_ = std::make_unique<glyphshift::opengl::OwnedRenderer>("overlay");
            update();
            XSync(display_, False);
        } catch (...) {
            close();
            throw;
        }
    }

    ~Overlay() {
        close();
    }

    void run() {
        bool poll = true;
        while (!glyphshift::platform::stop_requested() && alive_) {
            bool changed = false;
            while (XPending(display_)) {
                XEvent event;
                XNextEvent(display_, &event);
                if (capture_->handle(event)) {
                    dirty_ = true;
                } else if (tracker_->destroyed(event)) {
                    alive_ = false;
                } else if (tracker_->affects(event, surface_->window())) {
                    changed = true;
                }
            }
            if (!alive_)
                break;
            if (changed)
                update();
            // A new game frame, an open editor (continuous, vsync-paced UI) or
            // the idle polling interval runs one editor frame: it reads the
            // Insert/F10 hotkeys and UI input even while the game is paused.
            if (dirty_ || editor_open_ || poll) {
                present();
                poll = false;
                continue;
            }
            poll = !wait(50);
        }
    }

private:
    const Options& options_;
    Display* display_ = nullptr;
    // Destroyed in close(): the capture and renderer with the context current,
    // then the surface (which owns the context), then the connection.
    std::unique_ptr<x11::Capture> capture_;
    std::unique_ptr<x11::Tracker> tracker_;
    std::unique_ptr<x11::OverlayWindow> surface_;
    std::unique_ptr<glyphshift::opengl::OwnedRenderer> renderer_;
    x11::Tracker::State state_{};
    bool enabled_ = true, dirty_ = true, editor_open_ = false, alive_ = true;
    unsigned width_ = 0, height_ = 0, frames_ = 0;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    double render_ms_ = 0;

    void close() {
        if (!display_)
            return;
        glyphshift::editor::forget(display_, options_.target);
        if (surface_)
            surface_->make_current();
        capture_.reset();
        if (renderer_)
            renderer_->release();
        renderer_.reset();
        surface_.reset();
        tracker_.reset();
        XCloseDisplay(display_);
        display_ = nullptr;
    }

    // Applies the target's current state: hide the overlay while the target
    // is not viewable (hidden workspace, minimized, withdrawn) or the effect is
    // disabled; otherwise follow its geometry, rebind its pixmap after a resize
    // and stay directly above its frame in the stacking order.
    void update() {
        const auto next = tracker_->query();
        if (!next.exists) {
            alive_ = false;
            return;
        }
        // Shown while the effect is on, or while the editor is open over the
        // original image after F10.
        const bool show = next.viewable && (enabled_ || editor_open_) && next.width && next.height;
        if (!show) {
            surface_->hide();
            state_ = next;
            return;
        }
        const bool moved = next.x != state_.x || next.y != state_.y || next.width != state_.width ||
                           next.height != state_.height;
        if (next.width != width_ || next.height != height_ || !capture_->bound()) {
            // A resized window gets a new backing pixmap.
            width_ = next.width;
            height_ = next.height;
            dirty_ = true;
            if (!capture_->rebind())
                alive_ = false;
        }
        state_ = next;
        if (surface_->show(next, moved, *tracker_))
            dirty_ = true;
    }

    // One editor frame: settings, hotkeys and UI for this target. Changes to
    // the effect or editor state update visibility before rendering.
    void present() {
        const auto start = std::chrono::steady_clock::now();
        glyphshift::editor::Frame frame(display_,
            options_.target,
            state_.width,
            state_.height,
            "Overlay (OpenGL)",
            renderer_->version(),
            x11::editor_input());
        const bool open = frame.draw_data != nullptr;
        if (frame.settings.enabled != enabled_ || open != editor_open_) {
            enabled_ = frame.settings.enabled;
            editor_open_ = open;
            update();
        }
        if (surface_->shown() && (dirty_ || open))
            render(frame.settings, frame.draw_data);
        dirty_ = false;
        frame.complete(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
    }

    void render(const glyphshift::Settings& settings, const ImDrawData* ui) {
        const auto start = std::chrono::steady_clock::now();
        renderer_->begin();
        capture_->bind();
        renderer_->draw(width_, height_, capture_->top_down(), settings, ui);
        capture_->release();
        surface_->present();
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
    }

    // Waits up to `milliseconds` for an X event; false on timeout.
    bool wait(int milliseconds) {
        if (XPending(display_))
            return true;
        const int fd = ConnectionNumber(display_);
        fd_set read{};
        FD_ZERO(&read);
        FD_SET(fd, &read);
        timeval timeout{0, milliseconds * 1000};
        return select(fd + 1, &read, nullptr, nullptr, &timeout) > 0;
    }
};
}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = glyphshift::overlay::parse(argc, argv);
        if (!options.autostart.empty())
            return autostart(options);
        // Without a window, run in the background and attach with the hotkey.
        if (!options.target) {
            glyphshift::overlay::DaemonOptions daemon;
            if (!options.hotkey.empty())
                daemon.hotkey = options.hotkey;
            daemon.preset = options.preset;
            daemon.log = options.log;
            return glyphshift::overlay::run_daemon(daemon);
        }
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
