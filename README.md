# GlyphShift

> Turn any offline game into a living terminal.

GlyphShift is a Linux runtime graphics tool that transforms an offline game's
rendered frames into a configurable ASCII/glyph aesthetic. It runs alongside
the game, renders an in-game editor overlay, and applies changes immediately.
The intended experience is closer to MangoHud or ReShade than to a standalone
image converter.

This README is the implementation brief. Preserve the behavior and scope
described here unless a change is explicitly documented.

## Product experience

The user launches an offline game through GlyphShift:

```bash
glyphshift -- ./game
```

GlyphShift loads its graphics module into the child process, intercepts each
frame immediately before presentation, applies the glyph post-processing pass,
draws its editor, and presents the result.

Default controls:

- `F10`: toggle the glyph effect.
- `Insert`: open or close the in-game editor.
- `Escape`: close the editor without closing the game.

The editor changes the running effect immediately. It must eventually expose:

- character set and ordering;
- glyph font/atlas;
- independent cell width and height;
- aspect correction;
- monochrome or source-color rendering;
- foreground and background colors;
- brightness, contrast, gamma, and saturation;
- edge contribution;
- glyph stability/hysteresis;
- enabled/disabled state;
- named, per-game presets.

## MVP scope

The first supported environment is deliberately narrow:

- Linux x86-64;
- OpenGL through GLX on X11;
- Vulkan through a Vulkan loader layer;
- native Linux applications and offline Windows games running through Proton;
- windowed or borderless-windowed applications;
- applications launched by GlyphShift;
- offline games and local graphics applications only.

Both graphics backends are required for the MVP. OpenGL validates the simplest
native integration path; Vulkan provides native Vulkan coverage and the route
to Direct3D games translated by DXVK or VKD3D-Proton.

The following are explicitly out of scope for the MVP:

- Wayland-native input integration;
- EGL;
- Steam integration;
- attaching to an already-running process;
- fullscreen-exclusive compatibility;
- ANSI terminal output, recording, or streaming;
- multiplayer games or anti-cheat compatibility.

Do not implement anti-cheat evasion, process concealment, persistence,
privilege escalation, or system-wide injection. GlyphShift must affect only a
process the user explicitly launches through it. It must refuse privileged or
setuid targets and remain an offline-game tool.

## Technical direction

Use:

- C++20;
- CMake;
- a launcher executable named `glyphshift`;
- a shared object named `libglyphshift_gl.so`;
- a Vulkan layer shared object named `libglyphshift_vk.so` plus its manifest;
- OpenGL/GLSL and Vulkan/SPIR-V renderer backends;
- Dear ImGui for the in-game editor;
- `LD_PRELOAD` plus symbol interposition for GLX integration;
- `dlsym(RTLD_NEXT, ...)` to call the real graphics functions.

Intercept `glXSwapBuffers` for OpenGL. Add `eglSwapBuffers` only after GLX is
stable. Implement Vulkan as a conforming loader layer that is enabled only for
the child process launched by GlyphShift; do not patch Vulkan functions or
install an always-on system-wide layer.

```text
                         +-- OpenGL backend -----------------------+
glyphshift launcher ----+   LD_PRELOAD -> interposed              |
    |                    |   glXSwapBuffers                        |
    |                    |                                        +--> display
    |                    +-- Vulkan backend -----------------------+
    |                        loader layer -> intercepted           |
    |                        vkQueuePresentKHR                     |
    |
    +-- shared settings, editor model, presets, glyph semantics
```

Each backend captures the final application image, applies equivalent glyph
semantics, draws the same editor controls, and then presents. Never perform
substantial graphics work in a shared-library constructor. Initialize lazily
only after a valid OpenGL context or Vulkan device/swapchain exists.

## Proposed repository layout

```text
.
├── CMakeLists.txt
├── CMakePresets.json
├── apps/
│   └── launcher/
├── include/glyphshift/
│   ├── config/
│   ├── core/
│   ├── editor/
│   └── render/
├── src/
│   ├── config/
│   ├── core/
│   ├── editor/
│   └── render/
│       ├── opengl/
│       │   ├── glx_interpose.cpp
│       │   ├── gl_state.cpp
│       │   └── context_registry.cpp
│       └── vulkan/
│           ├── layer.cpp
│           ├── dispatch.cpp
│           └── swapchain_registry.cpp
├── shaders/
│   ├── opengl/
│   └── vulkan/
├── layers/
│   └── VK_LAYER_GLYPHSHIFT_overlay.json.in
├── assets/fonts/
├── tests/
│   ├── unit/
│   └── fixtures/
└── docs/
```

Do not create all directories speculatively. Add them when their milestone
needs them.

## Rendering model

The MVP is a GPU glyph renderer, not a terminal emulator and not text output.
Characters are glyphs sampled from a font atlas and composited by a fragment
shader.

For every output cell:

1. Sample the corresponding source-image region.
2. Calculate representative luminance and color.
3. Optionally include an edge metric.
4. Map the result to the active ordered character set.
5. Sample the selected glyph from the atlas.
6. Composite foreground and background using the selected color mode.

The initial shader may use a small fixed sample pattern instead of an exact
average. Cell width and height must be independent because terminal glyphs are
rarely square. Never read the framebuffer back to the CPU in the render loop.

The character set is runtime data. Do not hard-code `" .:-=+*#%@"` into
shader control flow. A compact lookup texture or uniform buffer is acceptable
for the MVP.

Temporal stabilization follows the basic pass. Retain the previous glyph index
while new luminance remains within a configurable hysteresis threshold. Leave
room in the renderer design for a previous-frame glyph-index texture without
prematurely implementing a general render graph.

OpenGL and Vulkan must expose the same user-facing parameters and produce
visually equivalent output, but they do not need to share compiled shader code.
Keep glyph mapping rules and defaults in the shared core and use thin,
API-appropriate shaders. Add deterministic tests for mapping semantics instead
of relying on byte-identical GPU output.

## OpenGL integration requirements

Injected rendering shares the game's context and must behave like a careful
guest:

- Store runtime state per GLX context/drawable; never assume one global context.
- Guard initialization and presentation against recursion.
- Resolve the real `glXSwapBuffers` once in a thread-safe way.
- Do not assume presentation occurs on the main thread.
- Handle resizing and drawable changes without leaking resources.
- Do no rendering work when the effect and editor are both disabled.
- Preserve and restore every modified OpenGL binding and state, including the
  program, VAO, buffers, active texture unit, textures, samplers, framebuffer,
  viewport, scissor, blend, depth, culling, color mask, polygon mode, and
  pixel-store state.
- Keep logging off the hot path and rate-limit repeated errors.
- If initialization or shader compilation fails, disable GlyphShift for that
  context and pass presentation through unchanged.
- Destroy GL resources only while their owning context is valid.

Compatibility is more important than cleverness. Never leave a game with
partially modified GL state after an error.

## Vulkan integration requirements

The Vulkan backend is an MVP deliverable, not a later experimental port:

- Implement the standard layer negotiation and dispatch-chain contracts.
- Track instances, physical devices, logical devices, queues, and swapchains
  without assuming there is only one of each.
- Intercept swapchain creation, image discovery, destruction, and presentation.
- Record GlyphShift work into its own command buffers and synchronize it
  without stalling the entire device on every frame.
- Preserve the application's semaphore ordering and present behavior.
- Handle swapchain recreation, resize, image-count changes, and common SDR
  formats. HDR support is not required for the MVP and must fail safely.
- Respect image layouts, queue families, and device ownership.
- Keep per-frame resources indexed safely across frames in flight.
- If the layer cannot support a device, queue, format, or swapchain, report one
  useful diagnostic and pass presentation through unchanged.
- Validate development fixtures with Khronos validation layers enabled.

The first Vulkan target is a controlled fixture or `vkcube`, followed by one
native Vulkan application and one confirmed-offline game through Proton. Never
load GlyphShift into a Proton game while anti-cheat or online services are
active, and never add logic intended to bypass them.

## Editor and input

Dear ImGui is the editor UI, not the rendering engine. Keep renderer settings
independent from the editor so the effect runs while the editor is closed.

The first input backend may target X11. While the editor is open, consume the
keyboard and pointer events used by the UI so the game does not react to edits.
Restore normal input immediately when it closes. Put hotkey handling behind a
small interface because Wayland will require a different implementation.

The overlay must display:

- active backend/API and OpenGL or Vulkan version;
- time spent by GlyphShift per frame;
- active preset name;
- shader and initialization errors without log spam;
- all controls required by the current milestone.

Keep the first UI compact and functional before styling it.

## Configuration

Pass runtime configuration to the injected module through narrowly named
environment variables such as `GLYPHSHIFT_CONFIG`. Persistent data should
follow the XDG base-directory specification.

Parsing, validation, defaults, and serialization must not depend on OpenGL or
Vulkan. Invalid values should emit a useful warning and use safe defaults
instead of terminating the game.

## Milestones

Implement and validate these in order. Both backends are part of the MVP; a
milestone that changes shared behavior must be checked against both once both
exist.

### M0 — Build and deterministic fixture

- Establish CMake targets and Debug/Release presets.
- Build deterministic GLX and Vulkan fixtures with the same moving gradient.
- Add basic unit-test infrastructure for API-independent code.
- Document all required development packages and build commands.

Exit condition: a clean checkout configures, builds, runs both fixtures, and
shuts them down cleanly.

### M1 — OpenGL pass-through interposition

- Build `libglyphshift_gl.so`.
- Interpose `glXSwapBuffers` and call the original function.
- Add opt-in diagnostic logging.
- Add recursion and failure guards.
- Implement the launcher that sets `LD_PRELOAD` for its child.

Exit condition: the fixture looks unchanged through GlyphShift, resizes
correctly, and exits cleanly.

### M2 — Vulkan pass-through layer

- Build `libglyphshift_vk.so` and a development layer manifest.
- Implement loader negotiation and per-instance/device dispatch tables.
- Intercept swapchain lifecycle and `vkQueuePresentKHR` without altering the
  presented image.
- Extend the launcher to enable the layer only for its child process.
- Run the Vulkan fixture with validation layers enabled.

Exit condition: the Vulkan fixture looks unchanged, recreates its swapchain,
resizes, and exits cleanly without validation errors introduced by GlyphShift.

### M3 — Glyph rendering on both backends

- Capture the final image without CPU readback on OpenGL and Vulkan.
- Render the same fixed atlas and character ramp on both backends.
- Support cell dimensions, monochrome/source color, and effect toggle on both.
- Preserve OpenGL state and Vulkan synchronization/layout correctness.

Exit condition: both fixtures are visibly and equivalently transformed, toggle
instantly, and render correctly after repeated resizes.

### M4 — Shared in-game editor

- Integrate Dear ImGui.
- Implement `Insert`, `Escape`, and `F10`.
- Bind widgets to live renderer settings.
- Display render timing and diagnostics.

Exit condition: parameters change live on both backends and closing the editor
returns input to each fixture.

### M5 — Robustness, presets, and real applications

- Add multi-context and multi-swapchain coverage.
- Add per-game preset persistence.
- Add automated GLX and Vulkan launch/smoke tests.
- Validate one native OpenGL application, one native Vulkan application, and
  one confirmed-offline Proton game.
- Test Mesa and NVIDIA environments when they are available.

Exit condition: failures degrade to pass-through behavior, presets survive
restart, both smoke tests pass, and the results of real-application tests are
documented without claiming broader compatibility.

### Later — Broader Linux support

- Investigate Wayland input and a Gamescope-style compositor backend.
- Add EGL only for a concrete compatibility target.
- Add Steam-specific installation and profile UX.
- Expand tested Proton compatibility, still restricted to offline use.

## Definition of MVP done

The target workflow is:

```bash
cmake --preset dev
cmake --build --preset dev
./build/dev/glyphshift -- ./build/dev/glyphshift-glx-fixture
./build/dev/glyphshift -- ./build/dev/glyphshift-vulkan-fixture
```

Both running fixtures must:

- remain unchanged when effect and editor are disabled;
- toggle original/glyph rendering using `F10`;
- open a usable editor using `Insert`;
- apply character ramp, cell size, color, and tonal changes immediately;
- render correctly after resizing and repeated editor toggles;
- terminate without crashes, OpenGL error storms, or Vulkan validation errors
  introduced by GlyphShift;
- require neither root privileges nor system-wide installation.

The same build must also complete a documented manual test with one native
OpenGL application, one native Vulkan application, and one confirmed-offline
Proton game. Passing on one backend while the other is incomplete is not MVP.

Build paths and commands may change during M0, but this README must be updated
to show commands that actually work.

## Rules for implementation agents

1. Work on one milestone at a time and make the smallest change that advances
   its exit condition.
2. Inspect the working tree before editing. Never overwrite unrelated user
   changes.
3. Keep API-independent code free of GLX, X11, OpenGL, and Vulkan headers.
4. Prefer explicit ownership and RAII, but destroy GL objects only with the
   owning context current.
5. Do not introduce a plugin system, scripting language, ECS, or render graph
   during the MVP.
6. Share product semantics, settings, and editor state. Keep resource lifetime,
   presentation, synchronization, and shaders backend-specific unless a real
   common boundary is demonstrated.
7. Avoid per-frame heap allocation after context initialization.
8. Add focused tests for pure logic, particularly configuration validation and
   luminance-to-glyph mapping.
9. Document commands actually executed. Never claim compatibility with a game,
   driver, compositor, or GPU that was not tested.
10. Update this README whenever scope, behavior, build commands, or hotkeys
    change.

## Immediate next task

Start with **M0 only**:

1. Create the minimal CMake project and presets.
2. Add small GLX and Vulkan moving-gradient fixtures with equivalent output.
3. Build both with warnings enabled.
4. Run both under X11, resize them repeatedly, enable Vulkan validation, and
   confirm clean shutdown.
5. Record the exact verified setup and build commands here.

Do not implement interposition, the Vulkan layer, or glyph shaders until both
M0 fixtures work.
