#include "pipeline.hpp"

#include <imgui.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <glyphshift/core/glyph.hpp>
#include <glyphshift/editor/editor.hpp>
#include <stdexcept>

#include "glyph_shaders.hpp"

namespace glyphshift::opengl {
namespace {
void diagnostic(const char* text) noexcept {
    const auto ignored = write(STDERR_FILENO, text, std::strlen(text));
    (void)ignored;
}

void texture_parameters(const GlApi& gl) {
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

GLuint shader(GLenum type, const char* source) {
    const auto& gl = api();
    const auto object = gl.CreateShader(type);
    gl.ShaderSource(object, 1, &source, nullptr);
    gl.CompileShader(object);
    GLint ok = 0;
    gl.GetShaderiv(object, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048]{};
        gl.GetShaderInfoLog(object, sizeof(log), nullptr, log);
        diagnostic(log);
        gl.DeleteShader(object);
        throw std::runtime_error("OpenGL glyph shader failed");
    }
    return object;
}

GLuint link(const char* vertex, const char* fragment) {
    const auto& gl = api();
    const auto result = gl.CreateProgram();
    for (const auto type : {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER}) {
        GLuint object = 0;
        try {
            object =
                shader(static_cast<GLenum>(type), type == GL_VERTEX_SHADER ? vertex : fragment);
        } catch (...) {
            gl.DeleteProgram(result);
            throw;
        }
        gl.AttachShader(result, object);
        gl.DeleteShader(object);
    }
    gl.LinkProgram(result);
    GLint ok = 0;
    gl.GetProgramiv(result, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048]{};
        gl.GetProgramInfoLog(result, sizeof(log), nullptr, log);
        diagnostic(log);
        gl.DeleteProgram(result);
        throw std::runtime_error("OpenGL glyph link failed");
    }
    return result;
}

// NVIDIA compiles a program for the framebuffer bound at link time and
// recompiles it, with a performance warning, when first drawn into a different
// kind. Link the per-cell pass while a temporary integer FBO like its real
// target is bound. Callers that must preserve bindings wrap this in GlState.
GLuint link_offscreen(const char* fragment, bool floating = false) {
    const auto& gl = api();
    GLuint target = 0, framebuffer = 0;
    gl.GenTextures(1, &target);
    gl.GenFramebuffers(1, &framebuffer);
    GLuint result = 0;
    try {
        if (!target || !framebuffer)
            throw std::runtime_error("GL link target allocation failed");
        gl.ActiveTexture(GL_TEXTURE3);
        gl.BindTexture(GL_TEXTURE_2D, target);
        texture_parameters(gl);
        if (floating)
            gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 1, 1, 0, GL_RED, GL_FLOAT, nullptr);
        else
            gl.TexImage2D(GL_TEXTURE_2D,
                0,
                GL_RGBA32UI,
                1,
                1,
                0,
                GL_RGBA_INTEGER,
                GL_UNSIGNED_INT,
                nullptr);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            target,
            0);
        result = link(shaders::vertex, fragment);
    } catch (...) {
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl.DeleteFramebuffers(1, &framebuffer);
        gl.DeleteTextures(1, &target);
        throw;
    }
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    gl.DeleteFramebuffers(1, &framebuffer);
    gl.DeleteTextures(1, &target);
    return result;
}
}  // namespace

struct Pipeline::Program {
    GLuint id = 0;
    GLint size = -1, cell = -1, source_color = -1, ramp_count = -1, strength = -1, art = -1,
          top_down = -1;

    explicit Program(GLuint linked) : id(linked) {
        const auto& gl = api();
        gl.UseProgram(id);
        gl.Uniform1i(gl.GetUniformLocation(id, "source_image"), 0);
        gl.Uniform1i(gl.GetUniformLocation(id, "atlas_image"), 1);
        gl.Uniform1i(gl.GetUniformLocation(id, "ramp_image"), 2);
        gl.Uniform1i(gl.GetUniformLocation(id, "cell_image"), 3);
        gl.Uniform1i(gl.GetUniformLocation(id, "meter_image"), 4);
        gl.Uniform1i(gl.GetUniformLocation(id, "luma_previous"), 5);
        gl.Uniform1i(gl.GetUniformLocation(id, "motion_costs"), 6);
        gl.Uniform1i(gl.GetUniformLocation(id, "camera_image"), 7);
        size = gl.GetUniformLocation(id, "size");
        cell = gl.GetUniformLocation(id, "cell");
        source_color = gl.GetUniformLocation(id, "source_color");
        ramp_count = gl.GetUniformLocation(id, "ramp_count");
        strength = gl.GetUniformLocation(id, "glyph_strength");
        art = gl.GetUniformLocation(id, "art[0]");
        top_down = gl.GetUniformLocation(id, "source_top_down");
    }

    ~Program() {
        api().DeleteProgram(id);
    }

    void use(unsigned width,
        unsigned height,
        bool top,
        const Settings& settings,
        const ArtParameters& values) const {
        const auto& gl = api();
        gl.UseProgram(id);
        gl.Uniform2f(size, static_cast<float>(width), static_cast<float>(height));
        gl.Uniform2f(cell,
            static_cast<float>(settings.cell_width),
            static_cast<float>(settings.cell_height));
        gl.Uniform1ui(source_color, settings.source_color ? 1 : 0);
        gl.Uniform1f(strength, settings.glyph_strength);
        gl.Uniform1ui(top_down, top ? 1 : 0);
        gl.Uniform4fv(art, static_cast<GLsizei>(art_vectors), values.data());
    }
};

void Target::release() {
    const auto& gl = api();
    for (auto& framebuffer : framebuffers)
        if (framebuffer)
            gl.DeleteFramebuffers(1, &framebuffer);
    for (auto& grid : cells)
        if (grid)
            gl.DeleteTextures(1, &grid);
    for (auto& framebuffer : meter_framebuffers)
        if (framebuffer)
            gl.DeleteFramebuffers(1, &framebuffer);
    for (auto& meter : meters)
        if (meter)
            gl.DeleteTextures(1, &meter);
    for (const auto framebuffer :
        {luma_framebuffer, costs_framebuffer, camera_framebuffers[0], camera_framebuffers[1]})
        if (framebuffer)
            gl.DeleteFramebuffers(1, &framebuffer);
    for (const auto texture : {luma, costs, cameras[0], cameras[1]})
        if (texture)
            gl.DeleteTextures(1, &texture);
    *this = {};
}

Pipeline::Pipeline(Caps caps) : caps_(caps) {
    const auto& gl = api();
    if (!gl.available)
        throw std::runtime_error("OpenGL entry points unavailable");
    compose_ = std::make_unique<Program>(link(shaders::vertex, shaders::fragment));
    analysis_ = std::make_unique<Program>(link_offscreen(shaders::analysis));

    gl.GenVertexArrays(1, &vao_);
    gl.GenTextures(1, &atlas_);
    gl.GenTextures(1, &ramp_);
    if (!vao_ || !atlas_ || !ramp_)
        throw std::runtime_error("GL object allocation failed");
}

Pipeline::~Pipeline() {
    const auto& gl = api();
    gl.DeleteTextures(1, &atlas_);
    gl.DeleteTextures(1, &ramp_);
    gl.DeleteVertexArrays(1, &vao_);
    if (ui_program_)
        gl.DeleteProgram(ui_program_);
    if (ui_vao_)
        gl.DeleteVertexArrays(1, &ui_vao_);
    if (ui_font_)
        gl.DeleteTextures(1, &ui_font_);
    if (ui_vertices_)
        gl.DeleteBuffers(1, &ui_vertices_);
    if (ui_indices_)
        gl.DeleteBuffers(1, &ui_indices_);
}

void Pipeline::update_atlas(const CharacterSelection& selection) {
    if (have_atlas_ && selection == selection_)
        return;
    const auto& gl = api();
    // Large CPU arrays: built on the heap, at selection changes only.
    const auto data = std::make_unique<CharacterAtlas>(build_character_atlas(selection));
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.ActiveTexture(GL_TEXTURE1);
    gl.BindTexture(GL_TEXTURE_2D, atlas_);
    texture_parameters(gl);
    gl.TexImage2D(GL_TEXTURE_2D,
        0,
        GL_R32F,
        integral_width,
        integral_height,
        0,
        GL_RED,
        GL_FLOAT,
        data->integral.data());
    gl.ActiveTexture(GL_TEXTURE2);
    gl.BindTexture(GL_TEXTURE_2D, ramp_);
    texture_parameters(gl);
    gl.TexImage2D(GL_TEXTURE_2D,
        0,
        GL_R8UI,
        256,
        ramp_height,
        0,
        GL_RED_INTEGER,
        GL_UNSIGNED_BYTE,
        data->ramp.data());
    count_ = data->count;
    for (const auto* program :
        {compose_.get(), analysis_.get(), meter_.get(), motion_.get(), resolve_.get(), luma_.get()})
        if (program) {
            gl.UseProgram(program->id);
            gl.Uniform1ui(program->ramp_count, count_);
        }
    editor::character_report(data->diagnostic.data());
    if (data->diagnostic[0]) {
        diagnostic(data->diagnostic.data());
        diagnostic("\n");
    }
    selection_ = selection;
    have_atlas_ = true;
}

// Allocation happens only when the window or cell size changes the grid, which
// also discards the history.
void Pipeline::prepare(Target& target, unsigned columns, unsigned rows) {
    const auto& gl = api();
    if (!target.cells[0]) {
        gl.GenTextures(2, target.cells.data());
        gl.GenFramebuffers(2, target.framebuffers.data());
        if (!target.cells[0] || !target.cells[1] || !target.framebuffers[0] ||
            !target.framebuffers[1])
            throw std::runtime_error("GL cell object allocation failed");
    }
    if (target.columns == columns && target.rows == rows)
        return;
    target.history = false;
    gl.ActiveTexture(GL_TEXTURE3);
    for (unsigned i = 0; i < 2; ++i) {
        gl.BindTexture(GL_TEXTURE_2D, target.cells[i]);
        texture_parameters(gl);
        gl.TexImage2D(GL_TEXTURE_2D,
            0,
            GL_RGBA32UI,
            static_cast<GLsizei>(columns),
            static_cast<GLsizei>(rows),
            0,
            GL_RGBA_INTEGER,
            GL_UNSIGNED_INT,
            nullptr);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, target.framebuffers[i]);
        gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            target.cells[i],
            0);
        if (gl.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("OpenGL glyph cell framebuffer incomplete");
    }
    target.columns = columns;
    target.rows = rows;
}

// Programs of optional passes are linked on first use: the injected renderer
// rebuilds the pipeline after context switches, so unused passes cost nothing.
void Pipeline::require(std::unique_ptr<Program>& program, const char* fragment) {
    if (program)
        return;
    program = std::make_unique<Program>(link_offscreen(fragment, true));
    api().Uniform1ui(program->ramp_count, count_);
}

void Pipeline::prepare_meter(Target& target) {
    require(meter_, shaders::meter);
    if (target.meters[0])
        return;
    const auto& gl = api();
    gl.GenTextures(2, target.meters.data());
    gl.GenFramebuffers(2, target.meter_framebuffers.data());
    if (!target.meters[0] || !target.meters[1] || !target.meter_framebuffers[0] ||
        !target.meter_framebuffers[1])
        throw std::runtime_error("GL exposure meter allocation failed");
    gl.ActiveTexture(GL_TEXTURE4);
    for (unsigned i = 0; i < 2; ++i) {
        gl.BindTexture(GL_TEXTURE_2D, target.meters[i]);
        texture_parameters(gl);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 1, 1, 0, GL_RED, GL_FLOAT, nullptr);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, target.meter_framebuffers[i]);
        gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            target.meters[i],
            0);
        if (gl.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("OpenGL exposure meter framebuffer incomplete");
    }
    target.metered = false;
}

// Single-attachment float targets of the anchored grid, allocated once per
// size; a new size starts the camera state over.
void Pipeline::prepare_anchor(Target& target, unsigned width, unsigned height) {
    require(motion_, shaders::motion);
    require(resolve_, shaders::resolve);
    require(luma_, shaders::luma);
    const auto& gl = api();
    const auto attach = [&](GLuint& texture,
                            GLuint& framebuffer,
                            GLenum format,
                            GLenum layout,
                            GLsizei columns,
                            GLsizei rows) {
        if (!texture)
            gl.GenTextures(1, &texture);
        if (!framebuffer)
            gl.GenFramebuffers(1, &framebuffer);
        if (!texture || !framebuffer)
            throw std::runtime_error("GL anchored grid allocation failed");
        gl.BindTexture(GL_TEXTURE_2D, texture);
        texture_parameters(gl);
        gl.TexImage2D(GL_TEXTURE_2D,
            0,
            static_cast<GLint>(format),
            columns,
            rows,
            0,
            layout,
            GL_FLOAT,
            nullptr);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            texture,
            0);
        if (gl.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("OpenGL anchored grid framebuffer incomplete");
    };
    gl.ActiveTexture(GL_TEXTURE5);
    if (!target.costs) {
        constexpr GLsizei candidates = 2 * 16 + 1;
        attach(target.costs, target.costs_framebuffer, GL_R32F, GL_RED, candidates, candidates);
        for (unsigned i = 0; i < 2; ++i)
            attach(target.cameras[i], target.camera_framebuffers[i], GL_RGBA32F, GL_RGBA, 2, 1);
    }
    if (target.luma && target.anchor_width == width && target.anchor_height == height)
        return;
    attach(target.luma,
        target.luma_framebuffer,
        GL_R8,
        GL_RED,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height));
    target.anchored = false;
}

void Pipeline::pass(GLuint framebuffer,
    unsigned width,
    unsigned height,
    const Program& program) const {
    const auto& gl = api();
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
    viewport(width, height);
    gl.UseProgram(program.id);
    gl.DrawArrays(GL_TRIANGLES, 0, 3);
}

// Share of the way toward this frame's measurement: 1 without a previous value.
float Pipeline::adaptation(Target& target) {
    const auto now = std::chrono::steady_clock::now();
    const double seconds = frame_interval_ > 0
                               ? frame_interval_
                               : std::chrono::duration<double>(now - target.meter_time).count();
    target.meter_time = now;
    return target.metered ? exposure_adaptation(static_cast<float>(seconds)) : 1.0F;
}

void Pipeline::viewport(unsigned width, unsigned height) const {
    const auto& gl = api();
    if (caps_.viewport_array)
        gl.ViewportIndexedf(0, 0, 0, static_cast<float>(width), static_cast<float>(height));
    else
        gl.Viewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

void Pipeline::bind_output(GLuint output) const {
    const auto& gl = api();
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, output);
    gl.DrawBuffer(output ? GL_COLOR_ATTACHMENT0 : GL_BACK);
}

void Pipeline::draw(Target& target,
    GLuint source,
    unsigned width,
    unsigned height,
    bool top_down,
    const Settings& settings,
    GLuint output) {
    const auto& gl = api();
    update_atlas(settings.glyphs);
    gl.BindVertexArray(vao_);
    const bool illustrated = settings.art.illustrated;
    const bool anchoring = illustrated && settings.art.anchor_grid;
    bool history = false;
    if (illustrated) {
        // An anchored grid adds a column and a row for the partial edge cells.
        const unsigned extra = anchoring ? 1 : 0;
        prepare(target,
            (width + settings.cell_width - 1) / settings.cell_width + extra,
            (height + settings.cell_height - 1) / settings.cell_height + extra);
        history = target.history && target.previous == settings;
        target.current ^= 1U;
    }
    auto values = art_parameters(settings);
    if (!history)
        values[art_stability] = 0;
    const bool metering = illustrated && settings.art.auto_exposure > 0;
    if (metering) {
        prepare_meter(target);
        values[art_adaptation] = adaptation(target);
    }
    bool anchor_history = false;
    if (anchoring) {
        prepare_anchor(target, width, height);
        // The camera state continues while the size and cells stay the same.
        anchor_history = target.anchored && target.anchor_width == width &&
                         target.anchor_height == height &&
                         target.anchor_settings.cell_width == settings.cell_width &&
                         target.anchor_settings.cell_height == settings.cell_height;
        values[art_anchor_history] = anchor_history ? 1.0F : 0.0F;
    }
    // During analysis, unit 3 holds the previous grid; composition then reads
    // the grid just written. Unit 4 holds the meter's previous value, then the
    // value it wrote for analysis.
    const GLuint units[] = {source,
        atlas_,
        ramp_,
        target.cells[target.current ^ 1U],
        target.meters[target.current ^ 1U],
        target.luma,
        target.costs,
        target.cameras[target.current ^ 1U]};
    for (unsigned i = 0; i < 8; ++i) {
        gl.ActiveTexture(GL_TEXTURE0 + i);
        gl.BindTexture(GL_TEXTURE_2D, units[i]);
        gl.BindSampler(i, 0);
    }
    if (anchoring) {
        // Motion search against the previous luminance, then this frame's
        // luminance for the next search, then the camera state (unit 7: the
        // previous one for resolve, this frame's for analysis and composition).
        if (anchor_history) {
            motion_->use(width, height, top_down, settings, values);
            pass(target.costs_framebuffer, 33, 33, *motion_);
        }
        luma_->use(width, height, top_down, settings, values);
        pass(target.luma_framebuffer, width, height, *luma_);
        resolve_->use(width, height, top_down, settings, values);
        pass(target.camera_framebuffers[target.current], 2, 1, *resolve_);
        gl.ActiveTexture(GL_TEXTURE7);
        gl.BindTexture(GL_TEXTURE_2D, target.cameras[target.current]);
        target.anchor_width = width;
        target.anchor_height = height;
        target.anchor_settings = settings;
    }
    target.anchored = anchoring;
    if (metering) {
        gl.ActiveTexture(GL_TEXTURE4);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, target.meter_framebuffers[target.current]);
        gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
        viewport(1, 1);
        meter_->use(width, height, top_down, settings, values);
        gl.DrawArrays(GL_TRIANGLES, 0, 3);
        gl.BindTexture(GL_TEXTURE_2D, target.meters[target.current]);
    }
    target.metered = metering;
    gl.ActiveTexture(GL_TEXTURE3);
    if (illustrated) {
        // Integer target: host blend state cannot alter packed cell data.
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, target.framebuffers[target.current]);
        gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
        viewport(target.columns, target.rows);
        analysis_->use(width, height, top_down, settings, values);
        gl.DrawArrays(GL_TRIANGLES, 0, 3);
        gl.BindTexture(GL_TEXTURE_2D, target.cells[target.current]);
    }
    target.history = illustrated;
    target.previous = settings;
    bind_output(output);
    viewport(width, height);
    compose_->use(width, height, top_down, settings, values);
    gl.DrawArrays(GL_TRIANGLES, 0, 3);
}

void Pipeline::draw_editor(const ImDrawData& data, unsigned width, unsigned height, GLuint output) {
    const auto& gl = api();
    if (!ui_program_) {
        ui_program_ = link(shaders::editor_vertex, shaders::editor_fragment);
        ui_scale_ = gl.GetUniformLocation(ui_program_, "scale");
        ui_translate_ = gl.GetUniformLocation(ui_program_, "translate");
        gl.GenVertexArrays(1, &ui_vao_);
        gl.GenBuffers(1, &ui_vertices_);
        gl.GenBuffers(1, &ui_indices_);
        gl.GenTextures(1, &ui_font_);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, ui_font_);
        texture_parameters(gl);
        const auto font = editor::font();
        gl.TexImage2D(GL_TEXTURE_2D,
            0,
            GL_R8,
            font.width,
            font.height,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            font.pixels);
        if (!ui_vao_ || !ui_vertices_ || !ui_indices_ || !ui_font_)
            throw std::runtime_error("OpenGL editor allocation failed");
    }
    bind_output(output);
    viewport(width, height);
    gl.Enablei(GL_BLEND, 0);
    if (caps_.indexed_blend) {
        gl.BlendEquationSeparatei(0, GL_FUNC_ADD, GL_FUNC_ADD);
        gl.BlendFuncSeparatei(0,
            GL_SRC_ALPHA,
            GL_ONE_MINUS_SRC_ALPHA,
            GL_ONE,
            GL_ONE_MINUS_SRC_ALPHA);
    } else {
        gl.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    if (caps_.viewport_array)
        gl.Enablei(GL_SCISSOR_TEST, 0);
    else
        gl.Enable(GL_SCISSOR_TEST);
    gl.UseProgram(ui_program_);
    gl.Uniform2f(ui_scale_, 2.0F / static_cast<float>(width), -2.0F / static_cast<float>(height));
    gl.Uniform2f(ui_translate_, -1, 1);
    gl.Uniform1i(gl.GetUniformLocation(ui_program_, "font_image"), 0);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, ui_font_);
    gl.BindSampler(0, 0);
    gl.BindVertexArray(ui_vao_);
    gl.BindBuffer(GL_ARRAY_BUFFER, ui_vertices_);
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui_indices_);
    for (GLuint i = 0; i < 3; ++i)
        gl.EnableVertexAttribArray(i);
    gl.VertexAttribPointer(0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(ImDrawVert),
        reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
    gl.VertexAttribPointer(1,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(ImDrawVert),
        reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
    gl.VertexAttribPointer(2,
        4,
        GL_UNSIGNED_BYTE,
        GL_TRUE,
        sizeof(ImDrawVert),
        reinterpret_cast<void*>(offsetof(ImDrawVert, col)));
    for (const auto* list : data.CmdLists) {
        gl.BufferData(GL_ARRAY_BUFFER,
            list->VtxBuffer.Size * static_cast<GLsizeiptr>(sizeof(ImDrawVert)),
            list->VtxBuffer.Data,
            GL_STREAM_DRAW);
        gl.BufferData(GL_ELEMENT_ARRAY_BUFFER,
            list->IdxBuffer.Size * static_cast<GLsizeiptr>(sizeof(ImDrawIdx)),
            list->IdxBuffer.Data,
            GL_STREAM_DRAW);
        for (const auto& cmd : list->CmdBuffer) {
            if (cmd.UserCallback)
                continue;  // The shared editor never installs callbacks or custom textures.
            const int x = std::max(0, static_cast<int>(cmd.ClipRect.x));
            const int y = std::max(0, static_cast<int>(cmd.ClipRect.y));
            const int right = std::min(static_cast<int>(width), static_cast<int>(cmd.ClipRect.z));
            const int bottom = std::min(static_cast<int>(height), static_cast<int>(cmd.ClipRect.w));
            if (right <= x || bottom <= y)
                continue;
            if (caps_.viewport_array)
                gl.ScissorIndexed(0, x, static_cast<int>(height) - bottom, right - x, bottom - y);
            else
                gl.Scissor(x, static_cast<int>(height) - bottom, right - x, bottom - y);
            gl.DrawElementsBaseVertex(GL_TRIANGLES,
                static_cast<GLsizei>(cmd.ElemCount),
                sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                reinterpret_cast<void*>(cmd.IdxOffset * sizeof(ImDrawIdx)),
                static_cast<GLint>(cmd.VtxOffset));
        }
    }
}
}  // namespace glyphshift::opengl
