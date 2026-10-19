#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>

#include <fstream>
#include <glyphshift/config/settings.hpp>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/display/x11/glx_loader.hpp"
#include "../src/render/opengl/pipeline.hpp"
#include "ppm.hpp"

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

struct Context {
    Display* display = XOpenDisplay(nullptr);
    GLXContext context = nullptr;
    GLXPbuffer drawable = 0;

    Context(unsigned width, unsigned height) {
        require(display, "Cannot open X11 display");
        const int attributes[] = {GLX_DRAWABLE_TYPE,
            GLX_PBUFFER_BIT,
            GLX_RENDER_TYPE,
            GLX_RGBA_BIT,
            None};
        int count = 0;
        auto* configs = glXChooseFBConfig(display, DefaultScreen(display), attributes, &count);
        require(configs && count, "No GLX pbuffer config");
        context = glXCreateNewContext(display, configs[0], GLX_RGBA_TYPE, nullptr, True);
        const int size[] = {GLX_PBUFFER_WIDTH,
            static_cast<int>(width),
            GLX_PBUFFER_HEIGHT,
            static_cast<int>(height),
            None};
        drawable = glXCreatePbuffer(display, configs[0], size);
        XFree(configs);
        require(context && drawable && glXMakeContextCurrent(display, drawable, drawable, context),
            "Cannot create offscreen GLX context");
    }

    ~Context() {
        if (context) {
            glXMakeContextCurrent(display, None, None, nullptr);
            glXDestroyContext(display, context);
        }
        if (drawable)
            glXDestroyPbuffer(display, drawable);
        if (display)
            XCloseDisplay(display);
    }
};

void flip(std::vector<unsigned char>& pixels, unsigned width, unsigned height) {
    const auto row = static_cast<std::size_t>(width) * 3;
    for (unsigned y = 0; y < height / 2; ++y)
        for (std::size_t x = 0; x < row; ++x)
            std::swap(pixels[y * row + x], pixels[(height - 1 - y) * row + x]);
}
}  // namespace

// Developer-only, real GPU preview on fixed images. Readback belongs here,
// never in the injected render loop. PPM input is deliberately minimal.
// Additional INPUT/OUTPUT pairs render as consecutive frames, so temporal
// stability sees the previous frame's cells exactly as the renderers do, and
// automatic exposure adapts as at 60 frames per second.
// Rendering uses the same glyph pipeline as the injected renderer and overlay.
int main(int argc, char** argv) {
    if (argc < 4 || argc % 2) {
        std::cerr << "Usage: glyphshift-preview INPUT.ppm SETTINGS.conf OUTPUT.ppm "
                     "[NEXT_INPUT.ppm NEXT_OUTPUT.ppm ...]\n";
        return 2;
    }
    try {
        unsigned width = 0, height = 0;
        auto pixels = glyphshift::tools::read_ppm(argv[1], width, height);
        std::ifstream config(argv[2]);
        require(static_cast<bool>(config), "Cannot read settings");
        const auto parsed = glyphshift::parse_settings(
            std::string(std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()));
        require(!parsed.warnings, "Invalid settings");
        const auto& settings = parsed.value;
        const Context context(width, height);
        glyphshift::opengl::load_api(glyphshift::display::x11::glx_proc);
        glyphshift::opengl::Pipeline pipeline(glyphshift::opengl::Caps{});
        // Frames of a sequence are 1/60 s apart, so exposure adaptation is
        // reproducible.
        pipeline.set_frame_interval(1.0 / 60);
        glyphshift::opengl::Target target;
        GLuint source = 0, output = 0, framebuffer = 0;
        glGenTextures(1, &source);
        glGenTextures(1, &output);
        glGenFramebuffers(1, &framebuffer);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        for (const auto texture : {source, output}) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D,
                0,
                GL_RGB8,
                static_cast<GLsizei>(width),
                static_cast<GLsizei>(height),
                0,
                GL_RGB,
                GL_UNSIGNED_BYTE,
                nullptr);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "Incomplete output framebuffer");
        glDisable(GL_DITHER);
        for (int frame = 0; 2 * frame + 3 < argc; ++frame) {
            if (frame) {
                unsigned next_width = 0, next_height = 0;
                pixels = glyphshift::tools::read_ppm(argv[2 * frame + 2], next_width, next_height);
                require(next_width == width && next_height == height,
                    "Sequence frames must share dimensions");
            }
            if (settings.enabled) {
                // Upload bottom-up, like a captured GL back buffer.
                flip(pixels, width, height);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, source);
                glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    0,
                    0,
                    static_cast<GLsizei>(width),
                    static_cast<GLsizei>(height),
                    GL_RGB,
                    GL_UNSIGNED_BYTE,
                    pixels.data());
                pipeline.draw(target, source, width, height, false, settings, framebuffer);
                glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
                glReadPixels(0,
                    0,
                    static_cast<GLsizei>(width),
                    static_cast<GLsizei>(height),
                    GL_RGB,
                    GL_UNSIGNED_BYTE,
                    pixels.data());
                flip(pixels, width, height);
            }
            require(glGetError() == GL_NO_ERROR, "OpenGL preview error");
            glyphshift::tools::write_ppm(argv[2 * frame + 3], pixels, width, height);
        }
        target.release();
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &output);
        glDeleteTextures(1, &source);
        std::cout << "Preview " << width << 'x' << height << " strength=" << settings.glyph_strength
                  << " frames=" << (argc - 2) / 2 << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
