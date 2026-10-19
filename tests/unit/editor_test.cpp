#include <imgui.h>

#include <cstdlib>
#include <glyphshift/editor/editor.hpp>
#include <iostream>
#include <thread>

#include "test_support.hpp"

namespace {
int failures = 0;

void check(bool ok, const char* name) {
    if (!ok) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}
}  // namespace

int main() {
    using namespace glyphshift::editor;
    check(IM_UNICODE_CODEPOINT_MAX == 0x10FFFF, "full Unicode input representation");
    glyphshift::test::unset_env("GLYPHSHIFT_CONFIG");
    glyphshift::test::unset_env("GLYPHSHIFT_PRESET");
    glyphshift::test::set_env("GLYPHSHIFT_PROFILE", "unit-editor");
    glyphshift::test::set_env(glyphshift::test::config_home_variable,
        (std::filesystem::temp_directory_path() / "glyphshift-editor-unit-empty").string());
    void* display = reinterpret_cast<void*>(1);
    auto frame = [&] {
        return Frame(display, 42, 800, 600, "Test", "1.0");
    };
    {
        auto f = frame();
        check(!f.draw_data && f.settings.enabled, "closed default");
    }
    auto key = [&](Key named, char32_t text, unsigned physical, bool down, unsigned long time = 0) {
        Input event{down ? Kind::key_down : Kind::key_up, named, text};
        event.physical = physical;
        event.time = time;
        return input(display, 42, event);
    };
    auto button = [&](Kind kind, unsigned number) {
        Input event{kind};
        event.button = number;
        return input(display, 42, event);
    };
    check(!input(display, 99, {Kind::key_down, Key::insert}), "unknown window forwarded");
    check(!key(Key::none, 'a', 38, true), "host key passed before opening");
    check(key(Key::insert, 0, 118, true), "Insert consumed");
    check(key(Key::insert, 0, 118, true), "repeat consumed without toggle");
    check(key(Key::insert, 0, 118, false), "Insert release consumed");
    {
        auto f = frame();
        check(f.draw_data && f.draw_data->TotalVtxCount > 0, "Dear ImGui geometry generated");
    }
    check(!key(Key::none, 'a', 38, false), "host-held key release forwarded while editor open");
    check(key(Key::none, 'b', 56, true), "editing key consumed");
    check(button(Kind::button_down, 1), "UI button consumed");
    check(input(display, 42, {Kind::motion}), "UI motion consumed");
    check(key(Key::escape, 0, 9, true), "Escape closes without reaching app");
    check(key(Key::escape, 0, 9, false), "Escape release consumed after close");
    {
        auto f = frame();
        check(!f.draw_data, "editor closed");
    }
    check(key(Key::none, 'b', 56, false), "editing key release suppressed after close");
    check(button(Kind::button_up, 1), "UI button release suppressed after close");
    check(!input(display, 42, {Kind::motion}), "normal motion restored");
    check(!key(Key::escape, 0, 9, true), "closed Escape returned to app");
    key(Key::escape, 0, 9, false);
    check(key(Key::f10, 0, 76, true), "closed F10 consumed");
    key(Key::f10, 0, 76, false, 100);
    key(Key::f10, 0, 76, true, 100);  // Traditional X11 release/press autorepeat pair.
    {
        auto f = frame();
        check(!f.settings.enabled, "F10 toggles once including autorepeat");
    }
    key(Key::f10, 0, 76, false, 101);
    key(Key::insert, 0, 118, true);
    key(Key::insert, 0, 118, false);
    {
        auto f = frame();
        check(f.draw_data && !f.settings.enabled, "editor independent of effect");
    }
    check(!input(display, 42, {Kind::focus_lost}), "focus event forwarded");
    {
        auto f = frame();
        check(!f.draw_data, "focus loss closes capture");
    }
    // A key release may go to another application after focus changes. Missing
    // that release must not make Insert look permanently held on return.
    key(Key::insert, 0, 118, true);
    input(display, 42, {Kind::focus_lost});
    {
        auto f = frame();
        check(!f.draw_data, "focus loss while Insert held closes editor");
    }
    report("Shader test diagnostic");
    key(Key::insert, 0, 118, true);
    key(Key::insert, 0, 118, false);
    for (int i = 0; i < 40; ++i) {
        auto f = frame();
        check(f.draw_data != nullptr, "repeated editor frames");
        f.complete(.25);
    }
    // Session state is independent, including a second presentation thread.
    std::thread other([&] {
        Frame f(display, 43, 320, 240, "Other", "1.0");
        check(f.settings.enabled && !f.draw_data, "separate window state");
    });
    other.join();
    deactivate(display, 42);
    check(!key(Key::insert, 0, 118, true), "failed renderer returns hotkeys to host");
    key(Key::insert, 0, 118, false);
    forget(display, 42);
    {
        auto f = frame();
        check(f.settings.enabled && !f.draw_data, "destroyed window state reset");
    }
    forget(display, 42);
    forget(display, 43);
    // A window-system input source is polled every frame, synced when the open
    // state changes, keeps the first source given and is told when the window
    // goes away.
    static int polls = 0, syncs = 0, forgets = 0, others = 0;
    static bool open_on_poll = false;
    static const InputSource source{[](void*, std::uint64_t, bool& open) {
                                        ++polls;
                                        if (open_on_poll)
                                            open = true;
                                    },
        [](void*, std::uint64_t, bool&) {
            ++syncs;
        },
        [](void*, std::uint64_t) {
            ++forgets;
        }};
    static const InputSource other_source{[](void*, std::uint64_t, bool&) {
                                              ++others;
                                          },
        [](void*, std::uint64_t, bool&) {
            ++others;
        },
        [](void*, std::uint64_t) {
            ++others;
        }};
    {
        Frame f(display, 44, 800, 600, "Test", "1.0", &source);
        check(polls == 1 && syncs == 0 && !f.draw_data, "input source polled");
    }
    open_on_poll = true;
    {
        Frame f(display, 44, 800, 600, "Test", "1.0", &other_source);
        check(polls == 2 && others == 0, "first input source kept");
        check(f.draw_data != nullptr && syncs == 0, "source opened the editor");
    }
    open_on_poll = false;
    key(Key::insert, 0, 118, true);
    {
        Frame f(display, 42, 800, 600, "Test", "1.0", &source);
        check(polls == 3 && syncs == 0, "unchanged state not synced");
    }
    input(display, 44, {Kind::key_down, Key::escape});
    {
        Frame f(display, 44, 800, 600, "Test", "1.0", &source);
        check(!f.draw_data, "escape closed the source's editor");
    }
    check(polls == 4, "source polled while closed");
    {
        Frame f(display, 44, 800, 600, "Test", "1.0");
        open_on_poll = true;
    }
    check(polls == 5 && syncs == 0, "source kept for frames without one");
    {
        Frame f(display, 44, 800, 600, "Test", "1.0");
        check(f.draw_data != nullptr, "source reopened");
    }
    open_on_poll = false;
    input(display, 44, {Kind::focus_lost});
    deactivate(display, 44);
    check(forgets == 1, "deactivate forgets the source's surface");
    forget(display, 44);
    check(forgets == 1 && others == 0, "deactivated window has no source");
    {
        Frame f(display, 44, 800, 600, "Test", "1.0", &source);
    }
    forget(display, 44);
    check(forgets == 2, "forget releases the source's surface");
    forget(display, 42);
    const auto atlas = font();
    check(atlas.pixels && atlas.width > 0 && atlas.height > 0, "shared font atlas");
    return failures ? 1 : 0;
}
