#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glyphshift/config/presets.hpp>
#include <glyphshift/config/settings.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "../../src/render/standalone/glyph_passes.hpp"

// Standalone capture overlay (M6, step 1). Never touches the target process:
// it reads the target window's pixels through the X Composite extension, runs
// the glyph passes, and presents into its own input-transparent window kept
// above the target.
namespace {
volatile std::sig_atomic_t stop = 0;

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
        else if (argument == "--log")
            options.log = true;
        else
            throw std::invalid_argument("unknown argument: " + std::string(argument));
    }
    if (!options.target)
        throw std::invalid_argument("missing --window ID");
    return options;
}

glyphshift::Settings settings_for(const Options& options) {
    if (options.preset.empty())
        return glyphshift::runtime_settings();
    const auto* included = glyphshift::builtin_preset(options.preset);
    if (!included)
        throw std::invalid_argument("unknown included style: " + options.preset);
    return glyphshift::parse_settings(included->config).value;
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
        // Automatic redirection keeps the window on screen while giving it an
        // off-screen pixmap that can be read; released when we disconnect.
        XCompositeRedirectWindow(display_, options.target, CompositeRedirectAutomatic);
        XSelectInput(display_, options.target, StructureNotifyMask);
        damage_ = XDamageCreate(display_, options.target, XDamageReportNonEmpty);

        create_window(screen, target);
        create_context(screen);
        passes_ = std::make_unique<glyphshift::standalone::GlyphPasses>();
        glGenTextures(1, &source_);
        glBindTexture(GL_TEXTURE_2D, source_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        follow(target.width, target.height);
        XSync(display_, False);
    }

    ~Overlay() {
        if (display_ && context_)
            glXMakeCurrent(display_, window_, context_);
        release_pixmap();
        passes_.reset();
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
        bool dirty = true;
        while (!stop && alive_) {
            while (XPending(display_)) {
                XEvent event;
                XNextEvent(display_, &event);
                if (event.type == damage_event_ + XDamageNotify) {
                    XDamageSubtract(display_, damage_, None, None);
                    dirty = true;
                } else if (event.type == ConfigureNotify &&
                           event.xconfigure.window == options_.target) {
                    follow(event.xconfigure.width, event.xconfigure.height);
                    dirty = true;
                } else if (event.type == DestroyNotify &&
                           event.xdestroywindow.window == options_.target) {
                    alive_ = false;
                } else if (event.type == UnmapNotify && event.xunmap.window == options_.target) {
                    XUnmapWindow(display_, window_);
                } else if (event.type == MapNotify && event.xmap.window == options_.target) {
                    XMapRaised(display_, window_);
                    dirty = true;
                }
            }
            if (!alive_)
                break;
            if (dirty) {
                render();
                dirty = false;
                continue;
            }
            wait();
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
    std::unique_ptr<glyphshift::standalone::GlyphPasses> passes_;
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
        XMapRaised(display_, window_);
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

    // Keep the overlay over the target and rebind its pixmap after a resize.
    void follow(int width, int height) {
        Window child = 0;
        int x = 0, y = 0;
        XTranslateCoordinates(display_,
            options_.target,
            DefaultRootWindow(display_),
            0,
            0,
            &x,
            &y,
            &child);
        XMoveResizeWindow(display_,
            window_,
            x,
            y,
            static_cast<unsigned>(width),
            static_cast<unsigned>(height));
        XRaiseWindow(display_, window_);
        if (static_cast<unsigned>(width) == width_ && static_cast<unsigned>(height) == height_ &&
            glx_pixmap_)
            return;
        width_ = static_cast<unsigned>(width);
        height_ = static_cast<unsigned>(height);
        release_pixmap();
        pixmap_ = XCompositeNameWindowPixmap(display_, options_.target);
        const int attributes[] = {GLX_TEXTURE_TARGET_EXT,
            GLX_TEXTURE_2D_EXT,
            GLX_TEXTURE_FORMAT_EXT,
            rgba_ ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT,
            None};
        glx_pixmap_ = glXCreatePixmap(display_, pixmap_config_, pixmap_, attributes);
        require(glx_pixmap_, "cannot wrap the target pixmap for GLX");
    }

    void render() {
        const auto start = std::chrono::steady_clock::now();
        const auto settings = settings_for(options_);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, source_);
        bind_(display_, glx_pixmap_, GLX_FRONT_LEFT_EXT, nullptr);
        if (settings.enabled)
            passes_->draw(source_, width_, height_, top_down_, settings);
        else {
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
        }
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

    // Sleep until an X event arrives (damage, configure, destroy).
    void wait() {
        XEvent event;
        XPeekEvent(display_, &event);
    }
};
}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse(argc, argv);
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
        std::cerr << "glyphshift-overlay: " << error.what()
                  << "\nUsage: glyphshift-overlay --window ID [--preset NAME] [--log]\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "glyphshift-overlay: " << error.what() << '\n';
        return 1;
    }
}
