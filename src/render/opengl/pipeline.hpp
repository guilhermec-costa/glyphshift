#pragma once
#include <array>
#include <glyphshift/config/characters.hpp>
#include <glyphshift/config/settings.hpp>
#include <memory>

#include "gl_api.hpp"

struct ImDrawData;

namespace glyphshift::opengl {
// Optional GL state paths the caller detected. The injected renderer touches
// only index 0 of indexed viewport/scissor/blend state, which it preserves;
// processes that own their context may leave both false.
struct Caps {
    bool viewport_array = false;
    bool indexed_blend = false;
};

// Per-presentation-target state: the two alternating cell grids (one holds the
// previous frame's decisions) and whether they are usable history.
struct Target {
    std::array<GLuint, 2> cells{}, framebuffers{};
    unsigned columns = 0, rows = 0, current = 0;
    bool history = false;
    Settings previous{};

    // Deletes the grids; call with the owning context current.
    void release();
};

// The glyph pipeline shared by every GL path: the injected renderer, the
// capture overlay and the preview tool. It uses the run-time GL table, owns
// programs, the glyph atlas and editor resources, and leaves GL state changed;
// callers that must preserve host state wrap it (GlState).
class Pipeline {
public:
    // Requires a current GL 3.3 context. Throws on shader or allocation failure.
    explicit Pipeline(Caps caps);
    ~Pipeline();
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    // Renders `source` (width x height pixels; `top_down` gives its row order)
    // into `output` (0: the default framebuffer's back buffer).
    void draw(Target& target,
        GLuint source,
        unsigned width,
        unsigned height,
        bool top_down,
        const Settings& settings,
        GLuint output);

    // Draws editor geometry over `output` with alpha blending.
    void draw_editor(const ImDrawData& data, unsigned width, unsigned height, GLuint output);

    const CharacterSelection& selection() const noexcept {
        return selection_;
    }

private:
    struct Program;
    Caps caps_;
    std::unique_ptr<Program> compose_, analysis_;
    GLuint vao_ = 0, atlas_ = 0, ramp_ = 0;
    GLuint ui_program_ = 0, ui_vao_ = 0, ui_vertices_ = 0, ui_indices_ = 0, ui_font_ = 0;
    GLint ui_scale_ = -1, ui_translate_ = -1;
    unsigned count_ = 0;
    bool have_atlas_ = false;
    CharacterSelection selection_{};

    void update_atlas(const CharacterSelection& selection);
    void prepare(Target& target, unsigned columns, unsigned rows);
    void viewport(unsigned width, unsigned height) const;
    void bind_output(GLuint output) const;
};
}  // namespace glyphshift::opengl
