#pragma once
#include <cstdint>
#include <glyphshift/config/settings.hpp>
#include <mutex>
struct ImDrawData;
namespace glyphshift::editor {
#define GS_EDITOR_API __attribute__((visibility("default")))
struct Font {
  const unsigned char* pixels;
  int width, height;
};
GS_EDITOR_API Font font();
using InputAdapter = void (*)(void*, std::uint64_t);
GS_EDITOR_API void input_adapter(InputAdapter attach, InputAdapter detach);
// A frame serializes the shared ImGui context and input queue across APIs.
class GS_EDITOR_API Frame {
 public:
  Frame(void* display, std::uint64_t window, unsigned width, unsigned height,
        const char* backend, const char* version, bool x11_input = false);
  ~Frame();
  Frame(const Frame&) = delete;
  Frame& operator=(const Frame&) = delete;
  void complete(double milliseconds);
  Settings settings;
  ImDrawData* draw_data = nullptr;

 private:
  std::unique_lock<std::recursive_mutex> lock_;
  void* state_ = nullptr;
  void* previous_ = nullptr;
};
GS_EDITOR_API void deactivate(void* display, std::uint64_t window) noexcept;
GS_EDITOR_API void report(const char* error) noexcept;
GS_EDITOR_API void character_report(const char* error) noexcept;
GS_EDITOR_API void forget(void* display, std::uint64_t window) noexcept;
// Platform boundary: no graphics or X11 headers in the editor model.
enum class Kind {
  key_down,
  key_up,
  motion,
  button_down,
  button_up,
  focus_lost
};
struct Input {
  Kind kind;
  unsigned code = 0;  // X11 keysym or mouse button at the platform boundary.
  unsigned physical = 0;
  unsigned long time = 0;
  float x = 0, y = 0;
  bool modified = false;
};
GS_EDITOR_API bool input(void* display, std::uint64_t window,
                         const Input& event) noexcept;
}  // namespace glyphshift::editor
