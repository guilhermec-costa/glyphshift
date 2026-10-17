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

#include "x11_input.hpp"

namespace glyphshift::editor {
namespace {
constexpr unsigned insert = 0xff63, escape = 0xff1b, f10 = 0xffc7;
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
  double milliseconds = 0;
  std::chrono::steady_clock::time_point previous{};
  ~State() {
    if (context) ImGui::DestroyContext(context);
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
      if (const char* requested = std::getenv("GLYPHSHIFT_PRESET");
          requested && *requested)
        active = requested;
      if (!preset_name_valid(active)) {
        active = "default";
        message = "invalid preset selection; using default";
      }
      std::string load_error;
      if (!presets->load(active, saved, load_error) &&
          (!load_error.empty() || active != "default"))
        message = load_error.empty() ? "selected preset missing; using defaults"
                                     : load_error;
      preset_names = presets->names();
      std::snprintf(preset_name.data(), preset_name.size(), "%s",
                    active.c_str());
      if (!message.empty())
        std::snprintf(error.data(), error.size(), "%s", message.c_str());
    } catch (const std::exception& failure) {
      std::snprintf(error.data(), error.size(), "Presets unavailable: %s",
                    failure.what());
      presets.reset();
    }
    if (error[0]) {
      const char prefix[] = "glyphshift: presets: ";
      const auto a = write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
      const auto b =
          write(STDERR_FILENO, error.data(), std::strlen(error.data()));
      const auto c = write(STDERR_FILENO, "\n", 1);
      (void)a;
      (void)b;
      (void)c;
    }
  }
  void load(const std::string& name) {
    if (!presets) return;
    Settings value;
    std::string message;
    if (!presets->load(name, value, message) &&
        (name != "default" || !message.empty())) {
      std::snprintf(error.data(), error.size(), "%s",
                    message.empty() ? "preset missing" : message.c_str());
      return;
    }
    active = name;
    saved = value;
    for (auto& [key, state] : states) {
      (void)key;
      state.settings = value;
    }
    std::snprintf(preset_name.data(), preset_name.size(), "%s", active.c_str());
    if (!presets->select(name, message))
      std::snprintf(error.data(), error.size(), "%s", message.c_str());
    else
      error[0] = 0;
  }
  void save(const Settings& settings) {
    if (!presets) return;
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
ImGuiKey key(unsigned code) {
  if (code >= 'a' && code <= 'z')
    return static_cast<ImGuiKey>(ImGuiKey_A + code - 'a');
  if (code >= 'A' && code <= 'Z')
    return static_cast<ImGuiKey>(ImGuiKey_A + code - 'A');
  if (code >= '0' && code <= '9')
    return static_cast<ImGuiKey>(ImGuiKey_0 + code - '0');
  switch (code) {
    case 0xff09:
      return ImGuiKey_Tab;
    case 0xff0d:
      return ImGuiKey_Enter;
    case 0xff08:
      return ImGuiKey_Backspace;
    case 0xffff:
      return ImGuiKey_Delete;
    case 0xff50:
      return ImGuiKey_Home;
    case 0xff57:
      return ImGuiKey_End;
    case 0xff51:
      return ImGuiKey_LeftArrow;
    case 0xff53:
      return ImGuiKey_RightArrow;
    case 0xff52:
      return ImGuiKey_UpArrow;
    case 0xff54:
      return ImGuiKey_DownArrow;
    case 0xff55:
      return ImGuiKey_PageUp;
    case 0xff56:
      return ImGuiKey_PageDown;
    case 0xffe1:
    case 0xffe2:
      return ImGuiMod_Shift;
    case 0xffe3:
    case 0xffe4:
      return ImGuiMod_Ctrl;
    case 0xffe9:
    case 0xffea:
      return ImGuiMod_Alt;
    case 0xffeb:
    case 0xffec:
      return ImGuiMod_Super;
    case ' ':
      return ImGuiKey_Space;
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
void controls(State& state, unsigned width, unsigned height, const char* api,
              const char* version, const char* error, Runtime& runtime) {
  const float w = std::min(348.0F, static_cast<float>(width) - 16.0F);
  const float h = std::min(620.0F, static_cast<float>(height) - 16.0F);
  ImGui::SetNextWindowPos({8, 8}, ImGuiCond_Always);
  ImGui::SetNextWindowSize({std::max(w, 1.0F), std::max(h, 1.0F)},
                           ImGuiCond_Always);
  if (ImGui::Begin("GlyphShift", &state.open,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::Text("%s  |  %s", api, version);
    ImGui::Text("CPU render / submit: %.3f ms", state.milliseconds);
    if (runtime.presets) {
      ImGui::SetNextItemWidth(-70);
      if (ImGui::BeginCombo("Preset", runtime.active.c_str())) {
        for (const auto& name : runtime.preset_names)
          if (ImGui::Selectable(name.c_str(), name == runtime.active))
            runtime.load(name);
        ImGui::EndCombo();
      }
      ImGui::SetNextItemWidth(-105);
      ImGui::InputText("##preset-name", runtime.preset_name.data(),
                       runtime.preset_name.size());
      ImGui::SameLine();
      if (ImGui::Button("Save preset")) runtime.save(state.settings);
      ImGui::TextUnformatted(state.settings != runtime.saved
                                 ? "Unsaved changes"
                                 : "Saved settings");
    } else
      ImGui::TextUnformatted("Presets unavailable (session only)");
    ImGui::Separator();
    ImGui::Checkbox("Glyph effect (F10)", &state.settings.enabled);
    int cell_width = static_cast<int>(state.settings.cell_width);
    int cell_height = static_cast<int>(state.settings.cell_height);
    ImGui::SetNextItemWidth(-110);
    if (ImGui::SliderInt("Cell width", &cell_width, 2, 128, "%d px",
                         ImGuiSliderFlags_AlwaysClamp))
      state.settings.cell_width = static_cast<unsigned>(cell_width);
    ImGui::SetNextItemWidth(-110);
    if (ImGui::SliderInt("Cell height", &cell_height, 2, 128, "%d px",
                         ImGuiSliderFlags_AlwaysClamp))
      state.settings.cell_height = static_cast<unsigned>(cell_height);
    bool mono = !state.settings.source_color;
    if (ImGui::Checkbox("Monochrome", &mono))
      state.settings.source_color = !mono;
    ImGui::SetNextItemWidth(-110);
    ImGui::SliderFloat("Glyph strength", &state.settings.glyph_strength, 0, 1,
                       "%.2f", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("0: scene detail  |  1: pure glyphs");
    if (!state.draft_ready || state.draft_base != state.settings.glyphs) {
      state.draft = state.draft_base = state.settings.glyphs;
      state.draft_ready = true;
    }
    if (ImGui::CollapsingHeader("Characters and font")) {
      if (ImGui::BeginCombo("Character set",
                            character_sets[state.draft.set].data())) {
        for (unsigned i = 0; i < character_sets.size(); ++i)
          if (ImGui::Selectable(character_sets[i].data(),
                                i == state.draft.set)) {
            state.draft.set = i;
            state.draft.density_order = (i == 1 || i == 2 || i == 5);
          }
        ImGui::EndCombo();
      }
      if (state.draft.set == 6)
        ImGui::InputText("Sequence (UTF-8)", state.draft.text.data(),
                         state.draft.text.size());
      ImGui::InputText("Font file", state.draft.font.data(),
                       state.draft.font.size());
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
    }
    if (ImGui::Button("Reset to defaults")) state.settings = {};
    if (ImGui::CollapsingHeader("Illustration")) {
      auto& art = state.settings.art;
      ImGui::Checkbox("Illustrated rendering", &art.illustrated);
      ImGui::Checkbox("Fill surfaces", &art.fill);
      ImGui::Checkbox("Contours over fill", &art.contour_overlay);
      ImGui::SliderFloat("Exposure", &art.exposure, .25F, 4, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
      ImGui::SliderFloat("Contrast", &art.contrast, .25F, 4, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
      ImGui::SliderFloat("Gamma", &art.gamma, .25F, 4, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
      ImGui::SliderFloat("Contour strength", &art.edge_strength, 0, 1, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
      ImGui::SliderFloat("Contour threshold", &art.edge_threshold, 0, 1, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
      ImGui::ColorEdit3("Ink / contours", art.foreground.data(),
                        ImGuiColorEditFlags_Float);
      ImGui::ColorEdit3("Paper", art.background.data(),
                        ImGuiColorEditFlags_Float);
      ImGui::InputText("Contour glyphs", state.draft.edges.data(),
                       state.draft.edges.size());
      ImGui::TextWrapped(
          "Four glyphs: vertical, horizontal, / and \\. Apply to update the "
          "atlas.");
      if (ImGui::Button("Apply contours")) {
        auto selection = state.settings.glyphs;
        selection.edges = state.draft.edges;
        if (valid_characters(selection)) {
          state.settings.glyphs = selection;
          state.draft = state.draft_base = selection;
          state.character_error[0] = 0;
        } else
          std::snprintf(state.character_error.data(),
                        state.character_error.size(),
                        "Use exactly four valid UTF-8 contour characters.");
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
    ImGui::TextWrapped(
        "Insert: editor  |  Escape: close\nF10: effect on / off");
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
    if (attach) attach(key.first, key.second);
  }
}
Font font() {
  auto& r = runtime();
  const std::lock_guard lock(r.mutex);
  return r.font();
}
Frame::Frame(void* display, std::uint64_t window, unsigned width,
             unsigned height, const char* backend, const char* version,
             bool x11_input)
    : lock_(runtime().mutex) {
  auto& r = runtime();
  auto& state = r.states[{display, window}];
  state_ = &state;
  state.available = true;
  if (r.attach) r.attach(display, window);
  const auto external = runtime_settings();
  if (!state.ready || external != state.external) {
    const char* config = std::getenv("GLYPHSHIFT_CONFIG");
    state.settings =
        (!state.ready && (!config || !*config)) ? r.saved : external;
    state.external = external;
    state.ready = true;
  }
  // Targeted synthetic fixture tests retain the original Xlib queue adapter.
  const bool platform =
      x11_input && !std::getenv("GLYPHSHIFT_FIXTURE_NO_FOCUS");
  if (platform) x11::poll(display, window, state.open);
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
            ? std::clamp(
                  std::chrono::duration<float>(now - state.previous).count(),
                  .001F, .1F)
            : 1.0F / 60;
    state.previous = now;
    for (unsigned i = 0; i < state.count; ++i) {
      const auto& event = state.events[i];
      if (event.kind == Kind::motion || event.kind == Kind::button_down ||
          event.kind == Kind::button_up)
        io.AddMousePosEvent(event.x, event.y);
      if (event.kind == Kind::button_down || event.kind == Kind::button_up) {
        if (event.code == 4 || event.code == 5) {
          if (event.kind == Kind::button_down)
            io.AddMouseWheelEvent(0, event.code == 4 ? 1.0F : -1.0F);
        } else if (event.code >= 1 && event.code <= 3)
          io.AddMouseButtonEvent(
              event.code == 1 ? 0 : (event.code == 3 ? 1 : 2),
              event.kind == Kind::button_down);
      }
      if (event.kind == Kind::key_down || event.kind == Kind::key_up) {
        const auto mapped = key(event.code);
        if (mapped != ImGuiKey_None)
          io.AddKeyEvent(mapped, event.kind == Kind::key_down);
        const unsigned text_code =
            event.code >= 0x01000000 ? event.code & 0xffffff : event.code;
        if (event.kind == Kind::key_down && !event.modified &&
            text_code >= 32 && text_code <= 0x10ffff &&
            (event.code < 256 || event.code >= 0x01000000) &&
            !(text_code >= 0xd800 && text_code <= 0xdfff))
          io.AddInputCharacter(text_code);
      }
      if (event.kind == Kind::focus_lost) io.AddFocusEvent(false);
    }
    state.count = 0;
    ImGui::NewFrame();
    controls(state, width, height, backend, version, r.error.data(), r);
    ImGui::Render();
    draw_data = state.open ? ImGui::GetDrawData() : nullptr;
  } else
    state.count = 0;
  if (platform && initially_open != state.open)
    x11::sync(display, window, state.open);
  settings = state.settings;
}
Frame::~Frame() {
  if (lock_.owns_lock()) complete(0);
}
void Frame::complete(double milliseconds) {
  if (!lock_.owns_lock()) return;
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
      x11::forget(display, window);
      if (r.detach) r.detach(display, window);
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
    if ((error && *error) ||
        std::strncmp(r.error.data(), "Characters:", 11) == 0)
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
    x11::forget(display, window);
    if (r.detach) r.detach(display, window);
    r.states.erase({display, window});
  } catch (...) {
  }
}
bool input(void* display, std::uint64_t window, const Input& event) noexcept {
  try {
    auto& r = runtime();
    const std::lock_guard lock(r.mutex);
    const auto found = r.states.find({display, window});
    if (found == r.states.end()) return false;
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
        if (state.open) queue(state, event);
        return consume;
      }
      if (!state.available) return false;
      const bool repeat =
          state.pressed[physical] ||
          (event.time && event.time == state.released[physical]);
      state.pressed[physical] = true;
      const bool hotkey =
          !event.modified && (event.code == insert || event.code == f10 ||
                              (event.code == escape && state.open));
      const bool consume = state.open || hotkey;
      if (consume) state.captured[physical] = true;
      if (hotkey && !repeat) {
        if (event.code == f10)
          state.settings.enabled = !state.settings.enabled;
        else {
          state.open = event.code == insert ? !state.open : false;
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
        state.buttons[event.code % 8] = true;
        queue(state, event);
        return true;
      }
    } else if (event.kind == Kind::button_up) {
      const bool consume = state.buttons[event.code % 8];
      state.buttons[event.code % 8] = false;
      if (state.open) queue(state, event);
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
