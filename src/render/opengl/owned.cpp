#include "owned.hpp"

#include "gl_api.hpp"

namespace glyphshift::opengl {
OwnedRenderer::OwnedRenderer(const char* timer_label) {
    const auto& gl = api();
    pipeline_ = std::make_unique<Pipeline>(Caps{});
    if (GpuTimer::requested())
        timer_ = std::make_unique<GpuTimer>(timer_label);
    gl.GenTextures(1, &source_);
    gl.BindTexture(GL_TEXTURE_2D, source_);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    const auto* version = reinterpret_cast<const char*>(gl.GetString(GL_VERSION));
    version_ = version ? version : "";
}

OwnedRenderer::~OwnedRenderer() = default;

void OwnedRenderer::begin() {
    const auto& gl = api();
    // The editor pass leaves blending and scissoring on; this process owns
    // the context, so reset them instead of saving host state.
    gl.Disable(GL_BLEND);
    gl.Disable(GL_SCISSOR_TEST);
    if (timer_)
        timer_->begin();
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, source_);
}

void OwnedRenderer::draw(unsigned width,
    unsigned height,
    bool top_down,
    const Settings& settings,
    const ImDrawData* ui) {
    if (settings.enabled)
        pipeline_->draw(cells_, source_, width, height, top_down, settings, 0);
    else {
        // F10 with the editor open: show the original image under the panel.
        auto original = settings;
        original.art.illustrated = false;
        original.source_color = true;
        original.glyph_strength = 0;
        pipeline_->draw(cells_, source_, width, height, top_down, original, 0);
    }
    if (timer_)
        timer_->end();
    if (ui)
        pipeline_->draw_editor(*ui, width, height, 0);
    const auto& gl = api();
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, source_);
}

void OwnedRenderer::release() {
    cells_.release();
    timer_.reset();
    pipeline_.reset();
    if (source_)
        api().DeleteTextures(1, &source_);
    source_ = 0;
}
}  // namespace glyphshift::opengl
