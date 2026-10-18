#pragma once
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include <array>
#include <glyphshift/config/settings.hpp>
#include <memory>

namespace glyphshift::standalone {
// Two-pass glyph rendering for processes that own their GL 3.3 context: the
// preview tool and the capture overlay. The injected GLX renderer keeps its own
// implementation, which must not link libGL and must preserve host state.
class GlyphPasses {
public:
    // Requires a current GL 3.3 context. Throws on shader or allocation failure.
    GlyphPasses();
    ~GlyphPasses();
    GlyphPasses(const GlyphPasses&) = delete;
    GlyphPasses& operator=(const GlyphPasses&) = delete;

    // Renders `source` (a 2D texture of width x height pixels) into the
    // currently bound draw framebuffer, which must be at least that size.
    // `top_down` describes the source's row order. Temporal history is used only
    // when the previous call had identical settings and size.
    void draw(GLuint source, unsigned width, unsigned height, bool top_down, const Settings&);

    void forget_history() noexcept {
        history_ = false;
    }

    // Diagnostic from the last glyph atlas build, empty when none.
    const char* diagnostic() const noexcept;

private:
    struct Program;
    std::unique_ptr<Program> compose_, analysis_;
    GLuint vao_ = 0, atlas_ = 0, ramp_ = 0;
    std::array<GLuint, 2> cells_{}, cell_framebuffers_{};
    unsigned columns_ = 0, rows_ = 0, current_ = 0, count_ = 0;
    bool history_ = false, have_atlas_ = false;
    Settings previous_{};
    CharacterSelection selection_{};
    std::array<char, 256> diagnostic_{};

    void update_atlas(const CharacterSelection& selection);
    void prepare_cells(unsigned columns, unsigned rows);
};
}  // namespace glyphshift::standalone
