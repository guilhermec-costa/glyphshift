#pragma once
#include <cstdint>
#include <glyphshift/config/settings.hpp>
#include <memory>
#include <unordered_map>

#include "gpu_timer.hpp"
#include "pipeline.hpp"

struct ImDrawData;

namespace glyphshift::opengl {
// Glyph rendering inside a host application's own GL context, for one
// context: it captures the default back buffer, runs the pipeline and editor,
// and restores every GL state it touched (GlState). The window system that
// intercepts presentation owns the context bookkeeping and the editor frame.
class InjectedRenderer {
public:
    InjectedRenderer() = default;
    InjectedRenderer(const InjectedRenderer&) = delete;
    InjectedRenderer& operator=(const InjectedRenderer&) = delete;

    bool ready() const noexcept {
        return caps_ready_;
    }

    // Checks the current context's version, limits and optional state paths.
    // Throws when the context cannot be served.
    void probe();

    // Renders into the current context's default back buffer. `drawable`
    // identifies the presentation target, whose capture texture and cell
    // grids are kept between frames.
    void draw(std::uint64_t drawable,
        unsigned width,
        unsigned height,
        const Settings& settings,
        ImDrawData* ui);

    // Frames shown unprocessed break the cells' temporal continuity.
    void forget_history() noexcept;

    // Deletes every GL object; call with the owning context current.
    void release();

private:
    struct Drawable {
        GLuint image = 0, framebuffer = 0;
        unsigned width = 0, height = 0;
        Target cells;
    };

    bool busy() const;
    void capture(Drawable& drawable, unsigned width, unsigned height);

    std::unique_ptr<Pipeline> pipeline_;
    std::unique_ptr<GpuTimer> timer_;
    unsigned clip_count_ = 0, tf_bindings_ = 0, draw_buffers_ = 0;
    bool caps_ready_ = false, viewport_array_ = false, clip_control_ = false;
    bool indexed_blend_ = false, query_buffer_ = false;
    std::unordered_map<std::uint64_t, Drawable> drawables_;
};
}  // namespace glyphshift::opengl
