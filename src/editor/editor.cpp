#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <glyphshift/editor/editor.hpp>
#include <map>
#include <memory>

namespace glyphshift::editor {
namespace {
constexpr unsigned insert = 0xff63, escape = 0xff1b, f10 = 0xffc7;
struct State {
  Settings settings{}, external{};
  bool ready = false, open = false;
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
              const char* version, const char* error) {
  const float w = std::min(348.0F, static_cast<float>(width) - 16.0F);
  const float h = std::min(370.0F, static_cast<float>(height) - 16.0F);
  ImGui::SetNextWindowPos({8, 8}, ImGuiCond_Always);
  ImGui::SetNextWindowSize({std::max(w, 1.0F), std::max(h, 1.0F)},
                           ImGuiCond_Always);
  if (ImGui::Begin("GlyphShift", &state.open,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::Text("%s  |  %s", api, version);
    ImGui::Text("CPU render / submit: %.3f ms", state.milliseconds);
    ImGui::TextUnformatted("Preset: Default (session)");
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
    if (ImGui::Button("Reset to defaults")) state.settings = {};
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
Font font() {
  auto& r = runtime();
  const std::lock_guard lock(r.mutex);
  return r.font();
}
Frame::Frame(void* display, std::uint64_t window, unsigned width,
             unsigned height, const char* backend, const char* version)
    : lock_(runtime().mutex) {
  auto& r = runtime();
  auto& state = r.states[{display, window}];
  state_ = &state;
  const auto external = runtime_settings();
  if (!state.ready || external != state.external) {
    state.settings = external;
    state.external = external;
    state.ready = true;
  }
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
        if (event.kind == Kind::key_down && !event.modified &&
            event.code >= 32 && event.code < 127)
          io.AddInputCharacter(event.code);
      }
      if (event.kind == Kind::focus_lost) io.AddFocusEvent(false);
    }
    state.count = 0;
    ImGui::NewFrame();
    controls(state, width, height, backend, version, r.error.data());
    ImGui::Render();
    draw_data = state.open ? ImGui::GetDrawData() : nullptr;
  } else
    state.count = 0;
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
      found->second.open = false;
      found->second.count = 0;
    }
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
      if (state.open) {
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
