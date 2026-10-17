#include <imgui.h>

#include <cstdlib>
#include <glyphshift/editor/editor.hpp>
#include <iostream>
#include <thread>
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
  unsetenv("GLYPHSHIFT_CONFIG");
  void* display = reinterpret_cast<void*>(1);
  auto frame = [&] { return Frame(display, 42, 800, 600, "Test", "1.0"); };
  {
    auto f = frame();
    check(!f.draw_data && f.settings.enabled, "closed default");
  }
  auto key = [&](unsigned code, unsigned physical, bool down,
                 unsigned long time = 0) {
    return input(display, 42,
                 {down ? Kind::key_down : Kind::key_up, code, physical, time});
  };
  check(!input(display, 99, {Kind::key_down, 0xff63, 1}),
        "unknown window forwarded");
  check(!key('a', 38, true), "host key passed before opening");
  check(key(0xff63, 118, true), "Insert consumed");
  check(key(0xff63, 118, true), "repeat consumed without toggle");
  check(key(0xff63, 118, false), "Insert release consumed");
  {
    auto f = frame();
    check(f.draw_data && f.draw_data->TotalVtxCount > 0,
          "Dear ImGui geometry generated");
  }
  check(!key('a', 38, false),
        "host-held key release forwarded while editor open");
  check(key('b', 56, true), "editing key consumed");
  check(input(display, 42, {Kind::button_down, 1}), "UI button consumed");
  check(input(display, 42, {Kind::motion}), "UI motion consumed");
  check(key(0xff1b, 9, true), "Escape closes without reaching app");
  check(key(0xff1b, 9, false), "Escape release consumed after close");
  {
    auto f = frame();
    check(!f.draw_data, "editor closed");
  }
  check(key('b', 56, false), "editing key release suppressed after close");
  check(input(display, 42, {Kind::button_up, 1}),
        "UI button release suppressed after close");
  check(!input(display, 42, {Kind::motion}), "normal motion restored");
  check(!key(0xff1b, 9, true), "closed Escape returned to app");
  key(0xff1b, 9, false);
  check(key(0xffc7, 76, true), "closed F10 consumed");
  key(0xffc7, 76, false, 100);
  key(0xffc7, 76, true, 100);  // Traditional X11 release/press autorepeat pair.
  {
    auto f = frame();
    check(!f.settings.enabled, "F10 toggles once including autorepeat");
  }
  key(0xffc7, 76, false, 101);
  key(0xff63, 118, true);
  key(0xff63, 118, false);
  {
    auto f = frame();
    check(f.draw_data && !f.settings.enabled, "editor independent of effect");
  }
  check(!input(display, 42, {Kind::focus_lost}), "focus event forwarded");
  {
    auto f = frame();
    check(!f.draw_data, "focus loss closes capture");
  }
  report("Shader test diagnostic");
  key(0xff63, 118, true);
  key(0xff63, 118, false);
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
  forget(display, 42);
  {
    auto f = frame();
    check(f.settings.enabled && !f.draw_data, "destroyed window state reset");
  }
  forget(display, 42);
  forget(display, 43);
  const auto atlas = font();
  check(atlas.pixels && atlas.width > 0 && atlas.height > 0,
        "shared font atlas");
  return failures ? 1 : 0;
}
