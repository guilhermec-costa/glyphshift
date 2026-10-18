#include "glyph_passes.hpp"

#include <cstring>
#include <glyphshift/config/characters.hpp>
#include <stdexcept>
#include <string>

#include "glyph_shaders.hpp"

namespace glyphshift::standalone {
namespace {
GLuint compile(GLenum type, const char* source) {
    const auto shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("glyph shader failed: ") + log);
    }
    return shader;
}

void parameters() {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
}  // namespace

struct GlyphPasses::Program {
    GLuint id = 0;
    GLint size = -1, cell = -1, source_color = -1, ramp_count = -1, strength = -1, art = -1,
          top_down = -1;

    explicit Program(const char* fragment) {
        id = glCreateProgram();
        const auto vertex = compile(GL_VERTEX_SHADER, shaders::vertex);
        GLuint pixels = 0;
        try {
            pixels = compile(GL_FRAGMENT_SHADER, fragment);
        } catch (...) {
            glDeleteShader(vertex);
            glDeleteProgram(id);
            throw;
        }
        glAttachShader(id, vertex);
        glAttachShader(id, pixels);
        glLinkProgram(id);
        glDeleteShader(vertex);
        glDeleteShader(pixels);
        GLint linked = 0;
        glGetProgramiv(id, GL_LINK_STATUS, &linked);
        if (!linked) {
            glDeleteProgram(id);
            throw std::runtime_error("glyph program link failed");
        }
        glUseProgram(id);
        glUniform1i(glGetUniformLocation(id, "source_image"), 0);
        glUniform1i(glGetUniformLocation(id, "atlas_image"), 1);
        glUniform1i(glGetUniformLocation(id, "ramp_image"), 2);
        glUniform1i(glGetUniformLocation(id, "cell_image"), 3);
        size = glGetUniformLocation(id, "size");
        cell = glGetUniformLocation(id, "cell");
        source_color = glGetUniformLocation(id, "source_color");
        ramp_count = glGetUniformLocation(id, "ramp_count");
        strength = glGetUniformLocation(id, "glyph_strength");
        art = glGetUniformLocation(id, "art[0]");
        top_down = glGetUniformLocation(id, "source_top_down");
    }

    ~Program() {
        if (id)
            glDeleteProgram(id);
    }

    void use(unsigned width,
        unsigned height,
        bool top,
        unsigned count,
        const Settings& settings,
        const std::array<float, 24>& values) const {
        glUseProgram(id);
        glUniform2f(size, static_cast<float>(width), static_cast<float>(height));
        glUniform2f(cell,
            static_cast<float>(settings.cell_width),
            static_cast<float>(settings.cell_height));
        glUniform1ui(source_color, settings.source_color ? 1 : 0);
        glUniform1ui(ramp_count, count);
        glUniform1f(strength, settings.glyph_strength);
        glUniform1ui(top_down, top ? 1 : 0);
        glUniform4fv(art, 6, values.data());
    }
};

GlyphPasses::GlyphPasses() {
    compose_ = std::make_unique<Program>(shaders::fragment);
    analysis_ = std::make_unique<Program>(shaders::analysis);
    glGenVertexArrays(1, &vao_);
    glGenTextures(1, &atlas_);
    glGenTextures(1, &ramp_);
    glGenTextures(2, cells_.data());
    glGenFramebuffers(2, cell_framebuffers_.data());
    if (!vao_ || !atlas_ || !ramp_ || !cells_[0] || !cells_[1] || !cell_framebuffers_[0] ||
        !cell_framebuffers_[1])
        throw std::runtime_error("glyph GL object allocation failed");
}

GlyphPasses::~GlyphPasses() {
    glDeleteFramebuffers(2, cell_framebuffers_.data());
    glDeleteTextures(2, cells_.data());
    glDeleteTextures(1, &ramp_);
    glDeleteTextures(1, &atlas_);
    glDeleteVertexArrays(1, &vao_);
}

const char* GlyphPasses::diagnostic() const noexcept {
    return diagnostic_.data();
}

void GlyphPasses::update_atlas(const CharacterSelection& selection) {
    if (have_atlas_ && selection == selection_)
        return;
    // Large CPU arrays: build on the heap, at selection changes only.
    const auto data = std::make_unique<CharacterAtlas>(build_character_atlas(selection));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    parameters();
    glTexImage2D(GL_TEXTURE_2D,
        0,
        GL_R32F,
        integral_width,
        integral_height,
        0,
        GL_RED,
        GL_FLOAT,
        data->integral.data());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ramp_);
    parameters();
    glTexImage2D(GL_TEXTURE_2D,
        0,
        GL_R8UI,
        256,
        ramp_height,
        0,
        GL_RED_INTEGER,
        GL_UNSIGNED_BYTE,
        data->ramp.data());
    count_ = data->count;
    diagnostic_ = data->diagnostic;
    selection_ = selection;
    have_atlas_ = true;
    history_ = false;  // Cell history stores glyph indices of the old atlas.
}

void GlyphPasses::prepare_cells(unsigned columns, unsigned rows) {
    if (columns == columns_ && rows == rows_)
        return;
    glActiveTexture(GL_TEXTURE3);
    for (unsigned i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, cells_[i]);
        parameters();
        glTexImage2D(GL_TEXTURE_2D,
            0,
            GL_RGBA32UI,
            static_cast<GLsizei>(columns),
            static_cast<GLsizei>(rows),
            0,
            GL_RGBA_INTEGER,
            GL_UNSIGNED_INT,
            nullptr);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, cell_framebuffers_[i]);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            cells_[i],
            0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("glyph cell framebuffer incomplete");
    }
    columns_ = columns;
    rows_ = rows;
    history_ = false;
}

void GlyphPasses::draw(GLuint source,
    unsigned width,
    unsigned height,
    bool top_down,
    const Settings& settings) {
    GLint target = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
    update_atlas(settings.glyphs);
    const bool illustrated = settings.art.illustrated;
    bool history = false;
    if (illustrated) {
        prepare_cells((width + settings.cell_width - 1) / settings.cell_width,
            (height + settings.cell_height - 1) / settings.cell_height);
        history = history_ && previous_ == settings;
        current_ ^= 1U;
    }
    auto values = art_parameters(settings);
    if (!history)
        values[art_stability] = 0;
    glBindVertexArray(vao_);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ramp_);
    glActiveTexture(GL_TEXTURE3);
    if (illustrated) {
        // Analysis reads the previous grid and writes the other one.
        glBindTexture(GL_TEXTURE_2D, cells_[current_ ^ 1U]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, cell_framebuffers_[current_]);
        glViewport(0, 0, static_cast<GLsizei>(columns_), static_cast<GLsizei>(rows_));
        analysis_->use(width, height, top_down, count_, settings, values);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindTexture(GL_TEXTURE_2D, cells_[current_]);
    } else {
        glBindTexture(GL_TEXTURE_2D, cells_[current_]);
    }
    history_ = illustrated;
    previous_ = settings;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(target));
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    compose_->use(width, height, top_down, count_, settings, values);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}
}  // namespace glyphshift::standalone
