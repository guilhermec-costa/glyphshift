#pragma once
#include <glyphshift/config/settings.hpp>
#include <memory>
#include <string>

#include "gpu_timer.hpp"
#include "pipeline.hpp"

struct ImDrawData;

namespace glyphshift::opengl {
// Glyph rendering in a context this process owns (the capture overlay): no
// host state to preserve. The source image is a texture the window system's
// capture binds between begin() and draw().
class OwnedRenderer {
public:
    // Requires a current GL 3.3 core context and a loaded GL table.
    explicit OwnedRenderer(const char* timer_label);
    ~OwnedRenderer();
    OwnedRenderer(const OwnedRenderer&) = delete;
    OwnedRenderer& operator=(const OwnedRenderer&) = delete;

    // The context's GL version string.
    const char* version() const noexcept {
        return version_.c_str();
    }

    // Starts a frame: resets the state the editor pass leaves on, starts GPU
    // timing and binds the source texture on unit 0 for the capture to fill.
    void begin();

    // Renders the bound source into the default framebuffer, then the editor.
    // With the effect disabled (F10 with the editor open) it shows the
    // original image under the panel. Leaves the source texture bound on
    // unit 0, so the capture can release it.
    void draw(unsigned width,
        unsigned height,
        bool top_down,
        const Settings& settings,
        const ImDrawData* ui);

    // Deletes every GL object; call with the context current.
    void release();

private:
    std::unique_ptr<Pipeline> pipeline_;
    std::unique_ptr<GpuTimer> timer_;
    Target cells_;
    GLuint source_ = 0;
    std::string version_;
};
}  // namespace glyphshift::opengl
