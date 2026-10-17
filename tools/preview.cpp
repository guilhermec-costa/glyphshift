#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>

#include <algorithm>
#include <fstream>
#include <glyphshift/config/settings.hpp>
#include <glyphshift/core/glyph.hpp>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

#include "glyph_shaders.hpp"

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

GLuint texture(unsigned unit,
    GLint internal,
    unsigned width,
    unsigned height,
    GLenum format,
    const void* data,
    GLenum type = GL_UNSIGNED_BYTE) {
    GLuint name = 0;
    glGenTextures(1, &name);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,
        0,
        internal,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height),
        0,
        format,
        type,
        data);
    return name;
}
}  // namespace

// Developer-only, real GPU preview on a fixed image. Readback belongs here,
// never in the injected render loop. PPM input is deliberately minimal.
int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: glyphshift-preview INPUT.ppm SETTINGS.conf OUTPUT.ppm\n";
        return 2;
    }
    try {
        std::ifstream input(argv[1], std::ios::binary);
        std::string magic;
        unsigned width = 0, height = 0, maximum = 0;
        input >> magic >> width >> height >> maximum;
        require(
            magic == "P6" && maximum == 255 && width && height && width <= 8192 && height <= 8192,
            "Expected P6 RGB PPM, no comments, dimensions 1..8192");
        require(input.get() == '\n', "Expected newline before PPM pixels");
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
        require(static_cast<bool>(input.read(reinterpret_cast<char*>(pixels.data()),
                    static_cast<std::streamsize>(pixels.size()))),
            "Cannot read input pixels");
        std::ifstream config(argv[2]);
        require(static_cast<bool>(config), "Cannot read settings");
        const auto parsed = glyphshift::parse_settings(
            std::string(std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()));
        require(!parsed.warnings, "Invalid settings");
        const auto& settings = parsed.value;
        const auto characters = glyphshift::build_character_atlas(settings.glyphs);
        if (characters.diagnostic[0])
            std::cerr << characters.diagnostic.data() << '\n';
        const Context context(width, height);
        const auto program = glCreateProgram();
        for (const auto type : {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER}) {
            const auto shader = glCreateShader(static_cast<GLenum>(type));
            const char* source = type == GL_VERTEX_SHADER ? glyphshift::shaders::vertex
                                                          : glyphshift::shaders::fragment;
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint ok = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[4096]{};
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                throw std::runtime_error(log);
            }
            glAttachShader(program, shader);
            glDeleteShader(shader);
        }
        glLinkProgram(program);
        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        require(linked, "Shader link failed");
        glUseProgram(program);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        flip(pixels, width, height);
        const auto source = texture(0, GL_RGB8, width, height, GL_RGB, pixels.data());
        const auto atlas = texture(1,
            GL_R32F,
            glyphshift::integral_width,
            glyphshift::integral_height,
            GL_RED,
            characters.integral.data(),
            GL_FLOAT);
        const auto ramp = texture(2, GL_R8UI, 256, 2, GL_RED_INTEGER, characters.ramp.data());
        const auto output = texture(3, GL_RGB8, width, height, GL_RGB, nullptr);
        GLuint fbo = 0, vao = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "Incomplete output framebuffer");
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glUniform1i(glGetUniformLocation(program, "source_image"), 0);
        glUniform1i(glGetUniformLocation(program, "atlas_image"), 1);
        glUniform1i(glGetUniformLocation(program, "ramp_image"), 2);
        glUniform2f(glGetUniformLocation(program, "size"),
            static_cast<float>(width),
            static_cast<float>(height));
        glUniform2f(glGetUniformLocation(program, "cell"),
            static_cast<float>(settings.cell_width),
            static_cast<float>(settings.cell_height));
        glUniform1ui(glGetUniformLocation(program, "source_color"), settings.source_color);
        glUniform1ui(glGetUniformLocation(program, "ramp_count"), characters.count);
        glUniform1f(glGetUniformLocation(program, "glyph_strength"), settings.glyph_strength);
        const auto art = glyphshift::art_parameters(settings);
        glUniform4fv(glGetUniformLocation(program, "art[0]"), 4, art.data());
        glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        glDisable(GL_DITHER);
        if (!settings.enabled) {
            flip(pixels, width, height);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
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
        std::ofstream destination(argv[3], std::ios::binary);
        destination << "P6\n" << width << ' ' << height << "\n255\n";
        destination.write(reinterpret_cast<const char*>(pixels.data()),
            static_cast<std::streamsize>(pixels.size()));
        require(static_cast<bool>(destination), "Cannot write output");
        const GLuint textures[] = {source, atlas, ramp, output};
        glDeleteTextures(4, textures);
        glDeleteFramebuffers(1, &fbo);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(program);
        std::cout << "Preview " << width << 'x' << height << " strength=" << settings.glyph_strength
                  << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
