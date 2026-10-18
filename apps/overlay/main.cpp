#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <sys/select.h>

#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

#include "../../src/render/opengl/gpu_timer.hpp"
#include "../../src/render/opengl/pipeline.hpp"
#include "daemon.hpp"
#include "tracker.hpp"

// Standalone capture overlay (M6). Never touches the target process: it reads
// the target window's pixels through the X Composite extension, runs the glyph
// pipeline, and presents into its own input-transparent window kept directly
// above the target. The shared editor (Insert/F10) works as in the injected
// mode, with presets kept per game in a profile derived from WM_CLASS.
namespace {
using glyphshift::overlay::Tracker;
volatile std::sig_atomic_t stop = 0;

// The target can disappear between any two requests. Errors are counted and
// the next query reports it gone, instead of Xlib's default exit.
int x_error(Display*, XErrorEvent*) {
    return 0;
}

void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

using BindTexImage = void (*)(Display*, GLXDrawable, int, const int*);
using ReleaseTexImage = void (*)(Display*, GLXDrawable, int);
using SwapInterval = void (*)(Display*, GLXDrawable, int);

struct Options {
    Window target = 0;
    std::string preset;
    std::string hotkey;
    std::string autostart;
    bool log = false;
};

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--window" && i + 1 < argc)
            options.target = static_cast<Window>(std::strtoul(argv[++i], nullptr, 0));
        else if (argument == "--preset" && i + 1 < argc)
            options.preset = argv[++i];
        else if (argument == "--hotkey" && i + 1 < argc)
            options.hotkey = argv[++i];
        else if (argument == "--autostart" && i + 1 < argc)
            options.autostart = argv[++i];
        else if (argument == "--log")
            options.log = true;
        else
            throw std::invalid_argument("unknown argument: " + std::string(argument));
    }
    if (options.target && !options.hotkey.empty())
        throw std::invalid_argument("--hotkey applies only without --window");
    if (!options.autostart.empty() &&
        (options.target || (options.autostart != "on" && options.autostart != "off")))
        throw std::invalid_argument("--autostart takes on or off, without --window");
    return options;
}

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
        glyphshift::overlay::check_hotkey(options.hotkey);
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

// Presets are kept per game: profile "overlay-CLASS" from the target's
// WM_CLASS, unless the user chose GLYPHSHIFT_PROFILE. Profile names allow
// 1-64 ASCII letters, digits, dots, underscores and hyphens.
void choose_profile(Display* display, Window target) {
    if (const char* chosen = std::getenv("GLYPHSHIFT_PROFILE"); chosen && *chosen)
        return;
    XClassHint hint{};
    std::string profile = "overlay";
    if (XGetClassHint(display, target, &hint)) {
        const char* name = hint.res_class ? hint.res_class : hint.res_name;
        std::string suffix;
        for (const char* c = name; c && *c && suffix.size() < 55; ++c) {
            const auto ch = static_cast<unsigned char>(*c);
            suffix += std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-'
                          ? static_cast<char>(std::tolower(ch))
                          : '-';
        }
        if (!suffix.empty())
            profile += "-" + suffix;
        if (hint.res_name)
            XFree(hint.res_name);
        if (hint.res_class)
            XFree(hint.res_class);
    }
    setenv("GLYPHSHIFT_PROFILE", profile.c_str(), 1);
}

class Overlay {
public:
    explicit Overlay(const Options& options) : options_(options) {
        display_ = XOpenDisplay(nullptr);
        require(display_, "cannot open X11 display");
        int event = 0, error = 0, major = 0, minor = 0;
        require(XCompositeQueryExtension(display_, &event, &error) &&
                    XCompositeQueryVersion(display_, &major, &minor) && (major > 0 || minor >= 2),
            "X Composite 0.2 is required");
        require(XDamageQueryExtension(display_, &damage_event_, &error), "XDamage is required");
        require(XFixesQueryExtension(display_, &event, &error), "XFixes is required");
        const int screen = DefaultScreen(display_);
        const char* extensions = glXQueryExtensionsString(display_, screen);
        require(extensions && std::strstr(extensions, "GLX_EXT_texture_from_pixmap"),
            "GLX_EXT_texture_from_pixmap is required");
        bind_ = reinterpret_cast<BindTexImage>(
            glXGetProcAddress(reinterpret_cast<const GLubyte*>("glXBindTexImageEXT")));
        release_ = reinterpret_cast<ReleaseTexImage>(
            glXGetProcAddress(reinterpret_cast<const GLubyte*>("glXReleaseTexImageEXT")));
        require(bind_ && release_, "texture_from_pixmap entry points unavailable");

        XWindowAttributes target{};
        require(XGetWindowAttributes(display_, options.target, &target), "target window not found");
        depth_ = static_cast<unsigned>(target.depth);
        choose_profile(display_, options.target);
        if (!options.preset.empty()) {
            if (!glyphshift::builtin_preset(options.preset))
                throw std::invalid_argument("unknown included style: " + options.preset);
            // The starting style; the editor and saved presets take over from here.
            setenv("GLYPHSHIFT_PRESET", options.preset.c_str(), 1);
        }
        XSetErrorHandler(x_error);
        tracker_ = std::make_unique<Tracker>(display_, options.target);
        // Automatic redirection keeps the window on screen while giving it an
        // off-screen pixmap that can be read; released when we disconnect.
        XCompositeRedirectWindow(display_, options.target, CompositeRedirectAutomatic);
        damage_ = XDamageCreate(display_, options.target, XDamageReportNonEmpty);

        create_window(screen, target);
        create_context(screen);
        pipeline_ = std::make_unique<glyphshift::opengl::Pipeline>(glyphshift::opengl::Caps{});
        if (glyphshift::opengl::GpuTimer::requested())
            timer_ = std::make_unique<glyphshift::opengl::GpuTimer>("overlay");
        glGenTextures(1, &source_);
        glBindTexture(GL_TEXTURE_2D, source_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        version_ = version ? version : "";
        update();
        XSync(display_, False);
    }

    ~Overlay() {
        if (display_)
            glyphshift::editor::forget(display_, options_.target);
        if (display_ && context_)
            glXMakeCurrent(display_, window_, context_);
        release_pixmap();
        cells_.release();
        timer_.reset();
        pipeline_.reset();
        if (source_)
            glDeleteTextures(1, &source_);
        if (context_) {
            glXMakeCurrent(display_, None, nullptr);
            glXDestroyContext(display_, context_);
        }
        if (window_)
            XDestroyWindow(display_, window_);
        if (display_)
            XCloseDisplay(display_);
    }

    void run() {
        bool poll = true;
        while (!stop && alive_) {
            bool changed = false;
            while (XPending(display_)) {
                XEvent event;
                XNextEvent(display_, &event);
                if (event.type == damage_event_ + XDamageNotify) {
                    XDamageSubtract(display_, damage_, None, None);
                    dirty_ = true;
                } else if (tracker_->destroyed(event)) {
                    alive_ = false;
                } else if (tracker_->affects(event, window_)) {
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
    Window window_ = 0;
    GLXContext context_ = nullptr;
    GLXFBConfig pixmap_config_ = nullptr;
    Pixmap pixmap_ = 0;
    GLXPixmap glx_pixmap_ = 0;
    Damage damage_ = 0;
    GLuint source_ = 0;
    BindTexImage bind_ = nullptr;
    ReleaseTexImage release_ = nullptr;
    std::unique_ptr<glyphshift::opengl::Pipeline> pipeline_;
    std::unique_ptr<glyphshift::opengl::GpuTimer> timer_;
    glyphshift::opengl::Target cells_;
    std::unique_ptr<Tracker> tracker_;
    Tracker::State state_{};
    bool mapped_ = false, enabled_ = true, dirty_ = true, editor_open_ = false;
    std::string version_;
    unsigned depth_ = 24, width_ = 0, height_ = 0, frames_ = 0;
    int damage_event_ = 0;
    bool alive_ = true, top_down_ = true, rgba_ = false;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    double render_ms_ = 0;

    void create_window(int screen, const XWindowAttributes& target) {
        const int attributes[] = {GLX_X_RENDERABLE,
            True,
            GLX_DRAWABLE_TYPE,
            GLX_WINDOW_BIT,
            GLX_RENDER_TYPE,
            GLX_RGBA_BIT,
            GLX_DOUBLEBUFFER,
            True,
            GLX_RED_SIZE,
            8,
            GLX_GREEN_SIZE,
            8,
            GLX_BLUE_SIZE,
            8,
            None};
        int count = 0;
        GLXFBConfig* configs = glXChooseFBConfig(display_, screen, attributes, &count);
        require(configs && count, "no double-buffered GLX window config");
        window_config_ = configs[0];
        XFree(configs);
        XVisualInfo* visual = glXGetVisualFromFBConfig(display_, window_config_);
        require(visual, "no visual for GLX window config");
        XSetWindowAttributes values{};
        values.override_redirect = True;
        values.colormap =
            XCreateColormap(display_, RootWindow(display_, screen), visual->visual, AllocNone);
        values.border_pixel = 0;
        values.background_pixmap = None;
        window_ = XCreateWindow(display_,
            RootWindow(display_, screen),
            0,
            0,
            static_cast<unsigned>(target.width),
            static_cast<unsigned>(target.height),
            0,
            visual->depth,
            InputOutput,
            visual->visual,
            CWOverrideRedirect | CWColormap | CWBorderPixel | CWBackPixmap,
            &values);
        XFree(visual);
        require(window_, "cannot create overlay window");
        XStoreName(display_, window_, "GlyphShift overlay");
        // Empty input shape: every click and key goes to the window below.
        const XserverRegion empty = XFixesCreateRegion(display_, nullptr, 0);
        XFixesSetWindowShapeRegion(display_, window_, ShapeInput, 0, 0, empty);
        XFixesDestroyRegion(display_, empty);
        // Ask compositors (picom/compton) not to shadow or fade the overlay.
        const unsigned long no_shadow = 0;
        XChangeProperty(display_,
            window_,
            XInternAtom(display_, "_COMPTON_SHADOW", False),
            XA_CARDINAL,
            32,
            PropModeReplace,
            reinterpret_cast<const unsigned char*>(&no_shadow),
            1);
    }

    GLXFBConfig window_config_ = nullptr;

    void create_context(int screen) {
        using CreateContext = GLXContext (*)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
        const auto create = reinterpret_cast<CreateContext>(
            glXGetProcAddress(reinterpret_cast<const GLubyte*>("glXCreateContextAttribsARB")));
        require(create, "GLX_ARB_create_context is required");
        const int attributes[] = {GLX_CONTEXT_MAJOR_VERSION_ARB,
            3,
            GLX_CONTEXT_MINOR_VERSION_ARB,
            3,
            GLX_CONTEXT_PROFILE_MASK_ARB,
            GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
            None};
        context_ = create(display_, window_config_, nullptr, True, attributes);
        require(context_ && glXMakeCurrent(display_, window_, context_),
            "cannot create OpenGL 3.3 core context");
        // Present on vblank to avoid tearing; damage drives when frames start.
        const auto interval = reinterpret_cast<SwapInterval>(
            glXGetProcAddress(reinterpret_cast<const GLubyte*>("glXSwapIntervalEXT")));
        if (interval)
            interval(display_, window_, 1);
        // A pixmap config able to bind the target's depth as a texture.
        const bool alpha = depth_ == 32;
        const int pixmap_attributes[] = {GLX_DRAWABLE_TYPE,
            GLX_PIXMAP_BIT,
            alpha ? GLX_BIND_TO_TEXTURE_RGBA_EXT : GLX_BIND_TO_TEXTURE_RGB_EXT,
            True,
            GLX_BIND_TO_TEXTURE_TARGETS_EXT,
            GLX_TEXTURE_2D_BIT_EXT,
            GLX_DOUBLEBUFFER,
            False,
            None};
        int count = 0;
        GLXFBConfig* configs = glXChooseFBConfig(display_, screen, pixmap_attributes, &count);
        require(configs && count, "no GLX config can bind the target pixmap");
        for (int i = 0; i < count && !pixmap_config_; ++i) {
            XVisualInfo* visual = glXGetVisualFromFBConfig(display_, configs[i]);
            if (visual && static_cast<unsigned>(visual->depth) == depth_)
                pixmap_config_ = configs[i];
            if (visual)
                XFree(visual);
        }
        XFree(configs);
        require(pixmap_config_, "no GLX pixmap config matches the target depth");
        int inverted = 0;
        glXGetFBConfigAttrib(display_, pixmap_config_, GLX_Y_INVERTED_EXT, &inverted);
        top_down_ = inverted != 0;
        rgba_ = alpha;
        (void)screen;
    }

    void release_pixmap() {
        if (glx_pixmap_) {
            glXDestroyPixmap(display_, glx_pixmap_);
            glx_pixmap_ = 0;
        }
        if (pixmap_) {
            XFreePixmap(display_, pixmap_);
            pixmap_ = 0;
        }
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
            if (mapped_) {
                XUnmapWindow(display_, window_);
                mapped_ = false;
            }
            state_ = next;
            return;
        }
        if (next.x != state_.x || next.y != state_.y || next.width != state_.width ||
            next.height != state_.height || !mapped_)
            XMoveResizeWindow(display_, window_, next.x, next.y, next.width, next.height);
        if (next.width != width_ || next.height != height_ || !glx_pixmap_)
            rebind(next.width, next.height);
        state_ = next;
        if (!mapped_) {
            XMapWindow(display_, window_);
            mapped_ = true;
            dirty_ = true;
        }
        tracker_->stack_above(window_);
    }

    // A resized window gets a new backing pixmap; wrap it again.
    void rebind(unsigned width, unsigned height) {
        release_pixmap();
        width_ = width;
        height_ = height;
        dirty_ = true;
        pixmap_ = XCompositeNameWindowPixmap(display_, options_.target);
        const int attributes[] = {GLX_TEXTURE_TARGET_EXT,
            GLX_TEXTURE_2D_EXT,
            GLX_TEXTURE_FORMAT_EXT,
            rgba_ ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT,
            None};
        glx_pixmap_ = glXCreatePixmap(display_, pixmap_config_, pixmap_, attributes);
        XSync(display_, False);
        if (!glx_pixmap_)
            alive_ = false;
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
            version_.c_str(),
            true);
        const bool open = frame.draw_data != nullptr;
        if (frame.settings.enabled != enabled_ || open != editor_open_) {
            enabled_ = frame.settings.enabled;
            editor_open_ = open;
            update();
        }
        if (mapped_ && (dirty_ || open))
            render(frame.settings, frame.draw_data);
        dirty_ = false;
        frame.complete(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
    }

    void render(const glyphshift::Settings& settings, const ImDrawData* ui) {
        const auto start = std::chrono::steady_clock::now();
        // The editor pass leaves blending and scissoring on; this process owns
        // the context, so reset them instead of saving host state.
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        if (timer_)
            timer_->begin();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, source_);
        bind_(display_, glx_pixmap_, GLX_FRONT_LEFT_EXT, nullptr);
        if (settings.enabled)
            pipeline_->draw(cells_, source_, width_, height_, top_down_, settings, 0);
        else {
            // F10 with the editor open: show the original image under the panel.
            auto original = settings;
            original.art.illustrated = false;
            original.source_color = true;
            original.glyph_strength = 0;
            pipeline_->draw(cells_, source_, width_, height_, top_down_, original, 0);
        }
        if (timer_)
            timer_->end();
        if (ui)
            pipeline_->draw_editor(*ui, width_, height_, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, source_);
        release_(display_, glx_pixmap_, GLX_FRONT_LEFT_EXT);
        glXSwapBuffers(display_, window_);
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
        const auto options = parse(argc, argv);
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
        std::signal(SIGINT, [](int) {
            stop = 1;
        });
        std::signal(SIGTERM, [](int) {
            stop = 1;
        });
        Overlay overlay(options);
        overlay.run();
        return 0;
    } catch (const std::invalid_argument& error) {
        std::cerr
            << "glyphshift-overlay: " << error.what()
            << "\nUsage: glyphshift-overlay [--hotkey KEYS] [--preset NAME] [--log]\n"
               "       glyphshift-overlay --window ID [--preset NAME] [--log]\n"
               "       glyphshift-overlay --autostart on|off [--hotkey KEYS] [--preset NAME]\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "glyphshift-overlay: " << error.what() << '\n';
        return 1;
    }
}
