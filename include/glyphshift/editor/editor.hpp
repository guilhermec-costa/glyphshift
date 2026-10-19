#pragma once
#include <cstdint>
#include <glyphshift/config/api.hpp>
#include <glyphshift/config/settings.hpp>
#include <mutex>
struct ImDrawData;

namespace glyphshift::editor {

struct Font {
    const unsigned char* pixels;
    int width, height;
};

GS_EDITOR_API Font font();
using InputAdapter = void (*)(void*, std::uint64_t);
GS_EDITOR_API void input_adapter(InputAdapter attach, InputAdapter detach);

// A window-system adapter that owns an input surface for the editor. It reports
// events through input() and keeps its surface in step with the open state.
// All three are called with the editor lock held; the first source given for a
// window stays with it until forget().
struct InputSource {
    void (*poll)(void* display, std::uint64_t window, bool& open);
    void (*sync)(void* display, std::uint64_t window, bool& open);
    void (*forget)(void* display, std::uint64_t window);
};

// A frame serializes the shared ImGui context and input queue across APIs.
class GS_EDITOR_API Frame {
public:
    Frame(void* display,
        std::uint64_t window,
        unsigned width,
        unsigned height,
        const char* backend,
        const char* version,
        const InputSource* source = nullptr);
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
// Platform boundary: no graphics or window-system headers in the editor model.
// Adapters translate their own key codes and buttons into these events.
enum class Kind { key_down, key_up, motion, button_down, button_up, focus_lost };

// Keys with a meaning beyond the text they produce. Letters, digits and space
// arrive as Key::none with their character.
enum class Key : unsigned char {
    none,
    insert,
    escape,
    f10,
    tab,
    enter,
    backspace,
    del,
    home,
    end,
    left,
    right,
    up,
    down,
    page_up,
    page_down,
    shift,
    control,
    alt,
    super,
};

struct Input {
    Kind kind;
    Key key = Key::none;
    char32_t character = 0;  // Unicode text of the key, on press and release; 0 if none.
    unsigned button = 0;     // 1 left, 2 middle, 3 right, 4/5 wheel up/down.
    unsigned physical = 0;   // Identifies the physical key across press and release.
    unsigned long time = 0;  // Event time; equal times mark an autorepeat pair.
    float x = 0, y = 0;      // Pointer position in the presentation surface.
    bool modified = false;   // Control, Alt or Super held.
};

GS_EDITOR_API bool input(void* display, std::uint64_t window, const Input& event) noexcept;
}  // namespace glyphshift::editor
