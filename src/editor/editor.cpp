#include <imgui.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glyphshift/config/presets.hpp>
#include <glyphshift/editor/editor.hpp>
#include <map>
#include <memory>

namespace glyphshift::editor {
namespace {
struct State {
    Settings settings{}, external{};
    CharacterSelection draft{}, draft_base{};
    bool draft_ready = false;
    std::array<char, 256> character_error{};
    bool ready = false, open = false, available = true;
    std::array<bool, 256> captured{}, pressed{};
    std::array<unsigned long, 256> released{};
    std::array<bool, 8> buttons{};
    std::array<Input, 256> events{};
    unsigned count = 0;
    ImGuiContext* context = nullptr;
    const InputSource* source = nullptr;
    double milliseconds = 0;
    std::chrono::steady_clock::time_point previous{};

    ~State() {
        if (context)
            ImGui::DestroyContext(context);
    }
};

struct Runtime {
    std::recursive_mutex mutex;
    ImFontAtlas atlas;
    std::map<std::pair<void*, std::uint64_t>, State> states;
    std::array<char, 512> error{};
    bool font_ready = false;
    InputAdapter attach = nullptr, detach = nullptr;
    std::unique_ptr<PresetStore> presets;
    std::vector<std::string> preset_names;
    std::string active = "default";
    Settings saved{};
    std::array<char, 65> preset_name{};

    Runtime() {
        try {
            presets = std::make_unique<PresetStore>();
            std::string message;
            active = presets->selected(message);
            if (const char* requested = std::getenv("GLYPHSHIFT_PRESET"); requested && *requested)
                active = requested;
            if (!preset_name_valid(active)) {
                active = "default";
                message = "invalid preset selection; using default";
            }
            std::string load_error;
            if (!presets->load(active, saved, load_error) &&
                (!load_error.empty() || active != "default"))
                message =
                    load_error.empty() ? "selected preset missing; using defaults" : load_error;
            preset_names = presets->names();
            std::snprintf(preset_name.data(), preset_name.size(), "%s", active.c_str());
            if (!message.empty())
                std::snprintf(error.data(), error.size(), "%s", message.c_str());
        } catch (const std::exception& failure) {
            std::snprintf(error.data(), error.size(), "Presets unavailable: %s", failure.what());
            presets.reset();
            preset_names.emplace_back("default");
            for (const auto& preset : builtin_presets())
                preset_names.emplace_back(preset.name);
        }
        if (error[0]) {
            const char prefix[] = "glyphshift: presets: ";
            const auto a = write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
            const auto b = write(STDERR_FILENO, error.data(), std::strlen(error.data()));
            const auto c = write(STDERR_FILENO, "\n", 1);
            (void)a;
            (void)b;
            (void)c;
        }
    }

    void load(const std::string& name) {
        Settings value;
        std::string message;
        bool loaded = false;
        if (presets)
            loaded = presets->load(name, value, message);
        else if (const auto* included = builtin_preset(name)) {
            const auto parsed = parse_settings(included->config);
            value = parsed.value;
            loaded = !parsed.warnings;
        }
        if (!loaded && (name != "default" || !message.empty())) {
            std::snprintf(error.data(),
                error.size(),
                "%s",
                message.empty() ? "preset missing" : message.c_str());
            return;
        }
        active = name;
        saved = value;
        for (auto& [key, state] : states) {
            (void)key;
            state.settings = value;
        }
        // Start edits as a personal copy instead of encouraging replacement of
        // the included style's remembered per-game version.
        std::snprintf(preset_name.data(),
            preset_name.size(),
            "%s%s",
            active.c_str(),
            builtin_preset(active) ? "-copy" : "");
        if (presets && !presets->select(name, message))
            std::snprintf(error.data(), error.size(), "%s", message.c_str());
        else
            error[0] = 0;
    }

    void save(const Settings& settings) {
        if (!presets)
            return;
        std::string message;
        if (presets->save(preset_name.data(), settings, message) &&
            presets->select(preset_name.data(), message)) {
            active = preset_name.data();
            saved = settings;
            preset_names = presets->names();
            error[0] = 0;
        } else
            std::snprintf(error.data(), error.size(), "%s", message.c_str());
    }

    Font font() {
        if (!font_ready) {
            atlas.AddFontDefault();
            unsigned char* pixels;
            int width, height;
            atlas.GetTexDataAsAlpha8(&pixels, &width, &height);
            atlas.SetTexID(1);
            font_ready = true;
        }
        return {atlas.TexPixelsAlpha8, atlas.TexWidth, atlas.TexHeight};
    }
};

Runtime& runtime() {
    static Runtime value;
    return value;
}

ImGuiKey key(const Input& event) {
    const char32_t c = event.character;
    if (c >= 'a' && c <= 'z')
        return static_cast<ImGuiKey>(ImGuiKey_A + (c - 'a'));
    if (c >= 'A' && c <= 'Z')
        return static_cast<ImGuiKey>(ImGuiKey_A + (c - 'A'));
    if (c >= '0' && c <= '9')
        return static_cast<ImGuiKey>(ImGuiKey_0 + (c - '0'));
    if (c == ' ')
        return ImGuiKey_Space;
    switch (event.key) {
        case Key::tab:
            return ImGuiKey_Tab;
        case Key::enter:
            return ImGuiKey_Enter;
        case Key::backspace:
            return ImGuiKey_Backspace;
        case Key::del:
            return ImGuiKey_Delete;
        case Key::home:
            return ImGuiKey_Home;
        case Key::end:
            return ImGuiKey_End;
        case Key::left:
            return ImGuiKey_LeftArrow;
        case Key::right:
            return ImGuiKey_RightArrow;
        case Key::up:
            return ImGuiKey_UpArrow;
        case Key::down:
            return ImGuiKey_DownArrow;
        case Key::page_up:
            return ImGuiKey_PageUp;
        case Key::page_down:
            return ImGuiKey_PageDown;
        case Key::shift:
            return ImGuiMod_Shift;
        case Key::control:
            return ImGuiMod_Ctrl;
        case Key::alt:
            return ImGuiMod_Alt;
        case Key::super:
            return ImGuiMod_Super;
        default:
            return ImGuiKey_None;
    }
}

void queue(State& state, const Input& event) {
    if (state.count < state.events.size())
        state.events[state.count++] = event;
    else {  // Bounded input; release all held UI state rather than stick a key.
        state.count = 0;
        if (state.context) {
            auto* old = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(state.context);
            ImGui::GetIO().AddFocusEvent(false);
            ImGui::GetIO().AddFocusEvent(true);
            ImGui::SetCurrentContext(old);
        }
    }
}

void controls(State& state,
    unsigned width,
    unsigned height,
    const char* api,
    const char* version,
    const char* error,
    Runtime& runtime) {
    const float w = std::min(348.0F, static_cast<float>(width) - 16.0F);
    const float h = std::min(620.0F, static_cast<float>(height) - 16.0F);
    ImGui::SetNextWindowPos({8, 8}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({std::max(w, 1.0F), std::max(h, 1.0F)}, ImGuiCond_Always);
    if (ImGui::Begin("GlyphShift",
            &state.open,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("%s  |  %s", api, version);
        ImGui::Text("CPU render / submit: %.3f ms", state.milliseconds);
        {
            ImGui::SetNextItemWidth(-70);
            const auto* included = builtin_preset(runtime.active);
            const char* title = included ? included->title.data() : runtime.active.c_str();
            if (ImGui::BeginCombo("Preset", title, ImGuiComboFlags_HeightLargest)) {
                // Only styles that redraw the scene are offered; F10 shows the
                // original image and Reset to defaults restores the base look.
                ImGui::SeparatorText("Included styles");
                for (const auto& preset : builtin_presets()) {
                    const bool selected = preset.name == runtime.active;
                    if (ImGui::Selectable(preset.title.data(), selected))
                        runtime.load(std::string(preset.name));
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", preset.description.data());
                }
                bool heading = false;
                for (const auto& name : runtime.preset_names)
                    if (name != "default" && !builtin_preset(name)) {
                        if (!heading) {
                            ImGui::SeparatorText("Saved presets");
                            heading = true;
                        }
                        if (ImGui::Selectable(name.c_str(), name == runtime.active))
                            runtime.load(name);
                        if (name == runtime.active)
                            ImGui::SetItemDefaultFocus();
                    }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Choose a style to apply it immediately to the running scene.");
            ImGui::BeginDisabled(!runtime.presets);
            ImGui::SetNextItemWidth(-105);
            ImGui::InputText("##preset-name",
                runtime.preset_name.data(),
                runtime.preset_name.size());
            ImGui::SameLine();
            if (ImGui::Button("Save preset"))
                runtime.save(state.settings);
            ImGui::EndDisabled();
            ImGui::TextUnformatted(state.settings != runtime.saved ? "Unsaved changes"
                                   : builtin_preset(runtime.active)
                                       ? "Included style - save a personal copy"
                                       : "Saved settings");
        }
        ImGui::Separator();
        ImGui::Checkbox("Glyph effect (F10)", &state.settings.enabled);
        int cell_width = static_cast<int>(state.settings.cell_width);
        int cell_height = static_cast<int>(state.settings.cell_height);
        ImGui::SetNextItemWidth(-110);
        if (ImGui::SliderInt("Cell width",
                &cell_width,
                2,
                128,
                "%d px",
                ImGuiSliderFlags_AlwaysClamp))
            state.settings.cell_width = static_cast<unsigned>(cell_width);
        ImGui::SetNextItemWidth(-110);
        if (ImGui::SliderInt("Cell height",
                &cell_height,
                2,
                128,
                "%d px",
                ImGuiSliderFlags_AlwaysClamp))
            state.settings.cell_height = static_cast<unsigned>(cell_height);
        bool mono = !state.settings.source_color;
        if (ImGui::Checkbox("Monochrome", &mono))
            state.settings.source_color = !mono;
        ImGui::SetNextItemWidth(-110);
        ImGui::SliderFloat("Glyph strength",
            &state.settings.glyph_strength,
            0,
            1,
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0: scene detail  |  1: pure glyphs");
        if (!state.draft_ready || state.draft_base != state.settings.glyphs) {
            state.draft = state.draft_base = state.settings.glyphs;
            state.draft_ready = true;
        }
        if (ImGui::CollapsingHeader("Characters and font")) {
            if (ImGui::BeginCombo("Character set", character_sets[state.draft.set].data())) {
                for (unsigned i = 0; i < character_sets.size(); ++i)
                    if (ImGui::Selectable(character_sets[i].data(), i == state.draft.set)) {
                        state.draft.set = i;
                        state.draft.density_order = (i == 1 || i == 2 || i == 5);
                    }
                ImGui::EndCombo();
            }
            if (state.draft.set == 6)
                ImGui::InputText("Sequence (UTF-8)",
                    state.draft.text.data(),
                    state.draft.text.size());
            ImGui::InputText("Font file", state.draft.font.data(), state.draft.font.size());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Absolute TTF/OTF path; empty uses system monospace. "
                    "Blocks/Braille are built in.");
            ImGui::Checkbox("Sort by ink density", &state.draft.density_order);
            ImGui::Checkbox("Reverse dark / light", &state.draft.reverse);
            ImGui::TextWrapped(
                "Up to 256 Unicode characters. Manual order: dark to light. Changes "
                "apply on confirmation.");
            if (ImGui::Button("Apply characters")) {
                if (valid_characters(state.draft)) {
                    state.settings.glyphs = state.draft;
                    state.draft_base = state.draft;
                    state.character_error[0] = 0;
                } else
                    std::snprintf(state.character_error.data(),
                        state.character_error.size(),
                        "Use 1..256 valid UTF-8 characters and an absolute "
                        "font path (or empty).");
            }
            if (state.character_error[0])
                ImGui::TextWrapped("%s", state.character_error.data());
            // Rebuilding the atlas is a selection change, so commit on release
            // rather than on every dragged frame.
            ImGui::SetNextItemWidth(150);
            ImGui::SliderFloat("Glyph weight",
                &state.draft.weight,
                0,
                1,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                state.settings.glyphs.weight = state.draft.weight;
                state.draft_base.weight = state.draft.weight;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Thicker strokes for font characters; applies on release.");
        }
        if (ImGui::Button("Reset to defaults"))
            state.settings = {};
        if (ImGui::CollapsingHeader("Illustration")) {
            auto& art = state.settings.art;
            ImGui::Checkbox("Illustrated rendering", &art.illustrated);
            ImGui::Checkbox("Fill surfaces", &art.fill);
            ImGui::Checkbox("Contours over fill", &art.contour_overlay);
            ImGui::Checkbox("Match local shapes", &art.shape_matching);
            ImGui::SetNextItemWidth(150);
            ImGui::SliderFloat("Shape threshold",
                &art.shape_threshold,
                0,
                1,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetNextItemWidth(150);
            ImGui::SliderFloat("Scene-colored paper",
                &art.scene_paper,
                0,
                1,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::Checkbox("Smooth paper wash", &art.paper_wash);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Blend scene-colored paper smoothly between cells, like a "
                    "watercolor wash under the glyphs.");
            ImGui::Checkbox("Crisp pixel glyphs", &art.crisp);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Every pixel is ink or paper: solid strokes with no gray "
                    "antialiasing, like pixel-art characters.");
            ImGui::TextWrapped(
                "Shape matching uses your selected glyphs to preserve small silhouettes. "
                "Include quadrant blocks for finer shapes. Scene-colored paper affects source "
                "color only.");
            ImGui::SliderFloat("Exposure",
                &art.exposure,
                .25F,
                4,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Contrast",
                &art.contrast,
                .25F,
                4,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Gamma", &art.gamma, .25F, 4, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Contour strength",
                &art.edge_strength,
                0,
                1,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Contour threshold",
                &art.edge_threshold,
                0,
                1,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cell lattice detector only.");
            ImGui::Checkbox("Pixel-level contours (DoG)", &art.dog_edges);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Trace outlines from every pixel with a difference of Gaussians: thin "
                    "lines become contours, smooth gradients and fine texture do not.");
            if (art.dog_edges) {
                ImGui::SliderFloat("DoG blur",
                    &art.dog_sigma,
                    .3F,
                    4,
                    "%.2f px",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("DoG threshold",
                    &art.dog_threshold,
                    0,
                    .2F,
                    "%.3f",
                    ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Higher keeps only stronger outlines.");
                ImGui::SliderFloat("Contour votes",
                    &art.edge_votes,
                    0,
                    1,
                    "%.2f",
                    ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Share of a cell's pixels that must agree on a direction.");
            }
            ImGui::ColorEdit3("Ink / contours", art.foreground.data(), ImGuiColorEditFlags_Float);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Mono: ink color. Source color: caps ink brightness; paper "
                    "lighter than ink prints dark scene-colored ink.");
            ImGui::ColorEdit3("Paper", art.background.data(), ImGuiColorEditFlags_Float);
            ImGui::SliderFloat("Saturation",
                &art.saturation,
                0,
                2,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Source color only: 0 gray, 1 scene, 2 vivid.");
            ImGui::SliderFloat("Glow / ink bleed",
                &art.glow,
                0,
                1,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Spreads glyph ink into nearby paper: a phosphor glow on dark "
                    "paper, ink bleed on light paper.");
            ImGui::SliderFloat("Temporal stability",
                &art.stability,
                0,
                1,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Keeps each cell's previous glyph while it still fits, so motion and "
                    "noise do not make glyphs flicker.");
            ImGui::InputText("Contour glyphs", state.draft.edges.data(), state.draft.edges.size());
            ImGui::InputText("Shape glyphs", state.draft.shapes.data(), state.draft.shapes.size());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Optional, for example /\\|_()^: drawn only where a glyph "
                    "fits a silhouette, never as tonal fill.");
            ImGui::TextWrapped(
                "Contours: four glyphs, vertical, horizontal, / and \\. Shape glyphs "
                "need Match local shapes. Apply to update the atlas.");
            if (ImGui::Button("Apply contour / shape glyphs")) {
                auto selection = state.settings.glyphs;
                selection.edges = state.draft.edges;
                selection.shapes = state.draft.shapes;
                if (valid_characters(selection)) {
                    state.settings.glyphs = selection;
                    state.draft = state.draft_base = selection;
                    state.character_error[0] = 0;
                } else
                    std::snprintf(state.character_error.data(),
                        state.character_error.size(),
                        "Use exactly four contour characters and at most 256 fill plus "
                        "shape characters (valid UTF-8).");
            }
            if (state.character_error[0])
                ImGui::TextWrapped("%s", state.character_error.data());
            if (art.illustrated &&
                (state.settings.cell_width < 8 || state.settings.cell_height < 12))
                ImGui::TextWrapped(
                    "Small cells merge letter strokes. Try 8 x 12 or larger for "
                    "readable characters.");
            ImGui::TextWrapped(
                "Use glyph strength 1 for an image drawn entirely by glyphs. Ink "
                "colors contours and monochrome fill; source fill uses normalized "
                "scene color.");
        }
        ImGui::Separator();
        ImGui::TextWrapped("Insert: editor  |  Escape: close\nF10: effect on / off");
        if (error && *error) {
            ImGui::Spacing();
            ImGui::TextWrapped("Renderer: %s", error);
        } else
            ImGui::TextUnformatted("Renderer ready");
    }
    ImGui::End();
}
}  // namespace

void input_adapter(InputAdapter attach, InputAdapter detach) {
    auto& r = runtime();
    const std::lock_guard lock(r.mutex);
    r.attach = attach;
    r.detach = detach;
    for (const auto& [key, state] : r.states) {
        (void)state;
        if (attach)
            attach(key.first, key.second);
    }
}

Font font() {
    auto& r = runtime();
    const std::lock_guard lock(r.mutex);
    return r.font();
}

Frame::Frame(void* display,
    std::uint64_t window,
    unsigned width,
    unsigned height,
    const char* backend,
    const char* version,
    const InputSource* source)
    : lock_(runtime().mutex) {
    auto& r = runtime();
    auto& state = r.states[{display, window}];
    state_ = &state;
    state.available = true;
    if (r.attach)
        r.attach(display, window);
    const auto external = runtime_settings();
    if (!state.ready || external != state.external) {
        const char* config = std::getenv("GLYPHSHIFT_CONFIG");
        state.settings = (!state.ready && (!config || !*config)) ? r.saved : external;
        state.external = external;
        state.ready = true;
    }
    if (!state.source)
        state.source = source;
    if (state.source)
        state.source->poll(display, window, state.open);
    const bool initially_open = state.open;
    previous_ = ImGui::GetCurrentContext();
    if (state.open && width > 16 && height > 16) {
        if (!state.context) {
            r.font();
            state.context = ImGui::CreateContext(&r.atlas);
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
            ImGui::StyleColorsDark();
            auto& style = ImGui::GetStyle();
            style.WindowRounding = 4;
            style.FrameRounding = 3;
            style.WindowPadding = {12, 12};
            style.ItemSpacing = {8, 10};
            style.Colors[ImGuiCol_WindowBg] = {.065F, .065F, .065F, 1};
            style.Colors[ImGuiCol_Text] = {.94F, .94F, .94F, 1};
            style.Colors[ImGuiCol_CheckMark] = {.55F, .82F, .50F, 1};
            style.Colors[ImGuiCol_SliderGrab] = {.40F, .65F, .36F, 1};
            style.Colors[ImGuiCol_SliderGrabActive] = {.55F, .82F, .50F, 1};
        }
        ImGui::SetCurrentContext(state.context);
        auto& io = ImGui::GetIO();
        io.DisplaySize = {static_cast<float>(width), static_cast<float>(height)};
        const auto now = std::chrono::steady_clock::now();
        io.DeltaTime =
            state.previous.time_since_epoch().count()
                ? std::clamp(std::chrono::duration<float>(now - state.previous).count(), .001F, .1F)
                : 1.0F / 60;
        state.previous = now;
        for (unsigned i = 0; i < state.count; ++i) {
            const auto& event = state.events[i];
            if (event.kind == Kind::motion || event.kind == Kind::button_down ||
                event.kind == Kind::button_up)
                io.AddMousePosEvent(event.x, event.y);
            if (event.kind == Kind::button_down || event.kind == Kind::button_up) {
                if (event.button == 4 || event.button == 5) {
                    if (event.kind == Kind::button_down)
                        io.AddMouseWheelEvent(0, event.button == 4 ? 1.0F : -1.0F);
                } else if (event.button >= 1 && event.button <= 3)
                    io.AddMouseButtonEvent(event.button == 1 ? 0 : (event.button == 3 ? 1 : 2),
                        event.kind == Kind::button_down);
            }
            if (event.kind == Kind::key_down || event.kind == Kind::key_up) {
                const auto mapped = key(event);
                if (mapped != ImGuiKey_None)
                    io.AddKeyEvent(mapped, event.kind == Kind::key_down);
                const char32_t text = event.character;
                if (event.kind == Kind::key_down && !event.modified && text >= 32 && text != 0x7f &&
                    text <= 0x10ffff && !(text >= 0xd800 && text <= 0xdfff))
                    io.AddInputCharacter(static_cast<unsigned>(text));
            }
            if (event.kind == Kind::focus_lost)
                io.AddFocusEvent(false);
        }
        state.count = 0;
        ImGui::NewFrame();
        controls(state, width, height, backend, version, r.error.data(), r);
        ImGui::Render();
        draw_data = state.open ? ImGui::GetDrawData() : nullptr;
    } else
        state.count = 0;
    if (state.source && initially_open != state.open)
        state.source->sync(display, window, state.open);
    settings = state.settings;
}

Frame::~Frame() {
    if (lock_.owns_lock())
        complete(0);
}

void Frame::complete(double milliseconds) {
    if (!lock_.owns_lock())
        return;
    static_cast<State*>(state_)->milliseconds = milliseconds;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(previous_));
    lock_.unlock();
}

void deactivate(void* display, std::uint64_t window) noexcept {
    try {
        auto& r = runtime();
        const std::lock_guard lock(r.mutex);
        const auto found = r.states.find({display, window});
        if (found != r.states.end()) {
            if (found->second.source)
                found->second.source->forget(display, window);
            found->second.source = nullptr;
            if (r.detach)
                r.detach(display, window);
            found->second.available = false;
            found->second.open = false;
            found->second.count = 0;
        }
    } catch (...) {
    }
}

void character_report(const char* error) noexcept {
    try {
        auto& r = runtime();
        const std::lock_guard lock(r.mutex);
        if ((error && *error) || std::strncmp(r.error.data(), "Characters:", 11) == 0)
            std::snprintf(r.error.data(), r.error.size(), "%s", error ? error : "");
    } catch (...) {
    }
}

void report(const char* error) noexcept {
    try {
        auto& r = runtime();
        const std::lock_guard lock(r.mutex);
        std::snprintf(r.error.data(), r.error.size(), "%s", error);
    } catch (...) {
    }
}

void forget(void* display, std::uint64_t window) noexcept {
    try {
        auto& r = runtime();
        const std::lock_guard lock(r.mutex);
        const auto found = r.states.find({display, window});
        if (found != r.states.end() && found->second.source)
            found->second.source->forget(display, window);
        if (r.detach)
            r.detach(display, window);
        r.states.erase({display, window});
    } catch (...) {
    }
}

bool input(void* display, std::uint64_t window, const Input& event) noexcept {
    try {
        auto& r = runtime();
        const std::lock_guard lock(r.mutex);
        const auto found = r.states.find({display, window});
        if (found == r.states.end())
            return false;
        auto& state = found->second;
        if (event.kind == Kind::focus_lost) {
            state.open = false;
            state.pressed.fill(false);
            state.captured.fill(false);
            state.buttons.fill(false);
            queue(state, event);
            return false;
        }
        if (event.kind == Kind::key_down || event.kind == Kind::key_up) {
            const auto physical = event.physical % 256;
            if (event.kind == Kind::key_up) {
                state.pressed[physical] = false;
                state.released[physical] = event.time;
                const bool consume = state.captured[physical];
                state.captured[physical] = false;
                if (state.open)
                    queue(state, event);
                return consume;
            }
            if (!state.available)
                return false;
            const bool repeat =
                state.pressed[physical] || (event.time && event.time == state.released[physical]);
            state.pressed[physical] = true;
            const bool hotkey =
                !event.modified && (event.key == Key::insert || event.key == Key::f10 ||
                                       (event.key == Key::escape && state.open));
            const bool consume = state.open || hotkey;
            if (consume)
                state.captured[physical] = true;
            if (hotkey && !repeat) {
                if (event.key == Key::f10)
                    state.settings.enabled = !state.settings.enabled;
                else {
                    state.open = event.key == Key::insert ? !state.open : false;
                    state.count = 0;
                    if (state.context) {
                        auto* old = ImGui::GetCurrentContext();
                        ImGui::SetCurrentContext(state.context);
                        ImGui::GetIO().AddFocusEvent(false);
                        ImGui::GetIO().AddFocusEvent(true);
                        ImGui::SetCurrentContext(old);
                    }
                }
            } else if (state.open && !hotkey)
                queue(state, event);
            return consume;
        }
        if (event.kind == Kind::button_down) {
            if (state.available && state.open) {
                state.buttons[event.button % 8] = true;
                queue(state, event);
                return true;
            }
        } else if (event.kind == Kind::button_up) {
            const bool consume = state.buttons[event.button % 8];
            state.buttons[event.button % 8] = false;
            if (state.open)
                queue(state, event);
            return consume;
        } else if (event.kind == Kind::motion && state.open) {
            queue(state, event);
            return true;
        }
    } catch (...) {
    }
    return false;
}
}  // namespace glyphshift::editor
