#include "capture.hpp"

#include <X11/extensions/Xcomposite.h>

#include <cstring>
#include <stdexcept>

namespace glyphshift::display::x11 {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
}  // namespace

Capture::Capture(Display* display, Window target) : display_(display), target_(target) {
    int event = 0, error = 0, major = 0, minor = 0;
    require(XCompositeQueryExtension(display_, &event, &error) &&
                XCompositeQueryVersion(display_, &major, &minor) && (major > 0 || minor >= 2),
        "X Composite 0.2 is required");
    require(XDamageQueryExtension(display_, &damage_event_, &error), "XDamage is required");
    const int screen = DefaultScreen(display_);
    const char* extensions = glXQueryExtensionsString(display_, screen);
    require(extensions && std::strstr(extensions, "GLX_EXT_texture_from_pixmap"),
        "GLX_EXT_texture_from_pixmap is required");
    bind_ = reinterpret_cast<BindTexImage>(
        glXGetProcAddress(reinterpret_cast<const GLubyte*>("glXBindTexImageEXT")));
    release_ = reinterpret_cast<ReleaseTexImage>(
        glXGetProcAddress(reinterpret_cast<const GLubyte*>("glXReleaseTexImageEXT")));
    require(bind_ && release_, "texture_from_pixmap entry points unavailable");
    XWindowAttributes attributes{};
    require(XGetWindowAttributes(display_, target_, &attributes), "target window not found");
    const auto depth = static_cast<unsigned>(attributes.depth);
    // A pixmap config able to bind the target's depth as a texture.
    rgba_ = depth == 32;
    const int pixmap_attributes[] = {GLX_DRAWABLE_TYPE,
        GLX_PIXMAP_BIT,
        rgba_ ? GLX_BIND_TO_TEXTURE_RGBA_EXT : GLX_BIND_TO_TEXTURE_RGB_EXT,
        True,
        GLX_BIND_TO_TEXTURE_TARGETS_EXT,
        GLX_TEXTURE_2D_BIT_EXT,
        GLX_DOUBLEBUFFER,
        False,
        None};
    int count = 0;
    GLXFBConfig* configs = glXChooseFBConfig(display_, screen, pixmap_attributes, &count);
    require(configs && count, "no GLX config can bind the target pixmap");
    for (int i = 0; i < count && !config_; ++i) {
        XVisualInfo* visual = glXGetVisualFromFBConfig(display_, configs[i]);
        if (visual && static_cast<unsigned>(visual->depth) == depth)
            config_ = configs[i];
        if (visual)
            XFree(visual);
    }
    XFree(configs);
    require(config_, "no GLX pixmap config matches the target depth");
    int inverted = 0;
    glXGetFBConfigAttrib(display_, config_, GLX_Y_INVERTED_EXT, &inverted);
    top_down_ = inverted != 0;
}

Capture::~Capture() {
    release_pixmap();
}

void Capture::start() {
    // Automatic redirection keeps the window on screen while giving it an
    // off-screen pixmap that can be read; released when we disconnect.
    XCompositeRedirectWindow(display_, target_, CompositeRedirectAutomatic);
    damage_ = XDamageCreate(display_, target_, XDamageReportNonEmpty);
}

bool Capture::handle(const XEvent& event) {
    if (event.type != damage_event_ + XDamageNotify)
        return false;
    XDamageSubtract(display_, damage_, None, None);
    return true;
}

// A resized window gets a new backing pixmap; wrap it again.
bool Capture::rebind() {
    release_pixmap();
    pixmap_ = XCompositeNameWindowPixmap(display_, target_);
    const int attributes[] = {GLX_TEXTURE_TARGET_EXT,
        GLX_TEXTURE_2D_EXT,
        GLX_TEXTURE_FORMAT_EXT,
        rgba_ ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT,
        None};
    glx_pixmap_ = glXCreatePixmap(display_, config_, pixmap_, attributes);
    XSync(display_, False);
    return glx_pixmap_ != 0;
}

void Capture::bind() const {
    bind_(display_, glx_pixmap_, GLX_FRONT_LEFT_EXT, nullptr);
}

void Capture::release() const {
    release_(display_, glx_pixmap_, GLX_FRONT_LEFT_EXT);
}

void Capture::release_pixmap() {
    if (glx_pixmap_) {
        glXDestroyPixmap(display_, glx_pixmap_);
        glx_pixmap_ = 0;
    }
    if (pixmap_) {
        XFreePixmap(display_, pixmap_);
        pixmap_ = 0;
    }
}
}  // namespace glyphshift::display::x11
