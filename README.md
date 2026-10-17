# GlyphShift

> Turn any offline game into a living terminal.

GlyphShift is a Linux runtime graphics tool that transforms an offline game's
rendered frames into a configurable ASCII/glyph aesthetic. It runs alongside
the game, renders an in-game editor overlay, and applies changes immediately.
The intended experience is closer to MangoHud or ReShade than to a standalone
image converter.

This README is the implementation brief. Preserve the behavior and scope
described here unless a change is explicitly documented.

## Current implementation

M0 and M1 are implemented: Debug/Release builds, deterministic GLX/Vulkan
fixtures, unit tests, and an OpenGL pass-through launcher/interposer. The
Vulkan layer, glyph effect, and editor belong to later milestones. M1 leaves
the application image unchanged and does not implement the product hotkeys.

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

## Development (M0–M1)

### Dependencies

Build on Linux x86-64 with a C++20 compiler (including floating-point
`std::from_chars`), CMake 3.20 or newer, Ninja, Xlib development headers,
OpenGL/GLX development libraries, Vulkan headers and loader, and
`glslangValidator`. Tests and developer formatting commands additionally use
Python 3 and Git. Formatting requires `clang-format` (verified with 22.1.8).
Running the fixtures requires an accessible X11 display,
an OpenGL 3.3 core driver, and a Vulkan 1.0 driver supporting Xlib surfaces,
swapchains, and a combined graphics/presentation queue. The Vulkan fixture
requires an SDR RGBA/BGRA UNORM surface. Install the Khronos validation layer
to run `--validation`; requesting it when unavailable fails explicitly.

Arch Linux package names (package installation was not performed during M0):

```bash
sudo pacman -S --needed base-devel cmake ninja libx11 libglvnd \
  vulkan-headers vulkan-icd-loader glslang vulkan-validation-layers \
  python git clang
```

Install the Vulkan ICD matching the GPU separately, for example `nvidia-utils`
for the tested NVIDIA setup. Development headers/loader alone do not provide a
Vulkan device. Optional verification tools are `spirv-tools` (`spirv-val`),
`xdotool`, Python 3, and ImageMagick (window captures and pixel comparison).
Neither fixture requires root or system-wide installation.

### Build and test

From the repository root:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev

cmake --preset release
cmake --build --preset release
ctest --preset release
```

`cmake --preset dev` also generates `build/dev/compile_commands.json` and
creates a symlink named `compile_commands.json` at the checkout root for LSPs.
Both files are ignored by Git. Release configuration leaves that Debug link
alone. Existing user-owned database files or different symlinks are preserved
with a warning. To opt out in a custom build, leave
`GLYPHSHIFT_LINK_COMPILE_COMMANDS=OFF` (the default outside the dev preset).

Warnings are enabled for all C++ targets: `-Wall -Wextra -Wpedantic
-Wconversion -Wshadow`. CTest runs pure option/timing tests, both fixtures'
`--help` paths, launcher process tests, and interposer tests without needing a
display. When
`clang-format` is present, it also tests the pre-commit hook. These do not
claim graphics coverage.
The two SPIR-V shaders are compiled locally by CMake; there are no downloaded
build dependencies. Fixture shader files live in the corresponding build tree;
reconfigure/rebuild after relocating a checkout or removing that tree.

### Code formatting and pre-commit

`.clang-format` uses Google style, C++20, and an 80-column limit. The C++
sources, including the M0 fixtures, have been formatted with this style.
Use either the Python commands or equivalent CMake targets:

```bash
python3 tools/format.py --apply
python3 tools/format.py --check
cmake --build --preset dev --target format
cmake --build --preset dev --target format-check
```

Activate the repository's native Git hook once per checkout:

```bash
git config --local core.hooksPath .githooks
```

This has already been activated in the working repository used for M1.
No separate `pre-commit` framework is needed. The hook checks the exact staged
C/C++ source bytes with `clang-format`, rejects unformatted content, and asks
you to format/review/stage it. It never rewrites the index or unstaged work.
Deleted files are skipped, paths with spaces are supported, and missing
`clang-format` fails with an installation instruction. Git clones do not
activate hooks automatically; run the configuration command above locally.

### Launch with GlyphShift (M1)

```bash
./build/dev/glyphshift -- ./build/dev/glyphshift-glx-fixture
GLYPHSHIFT_LOG=1 ./build/dev/glyphshift -- \
  ./build/dev/glyphshift-glx-fixture --frames 120
```

The launcher finds `libglyphshift_gl.so` next to its own executable, prepends
its absolute path to `LD_PRELOAD`, preserves existing preloads, and executes
the inspected target file descriptor. It supports executable paths or `PATH`
lookup, preserves arguments and the current directory, and replaces itself
with the target so exit codes and signals propagate naturally. Environment
changes affect only this launch and its descendants; the invoking shell is
unchanged. Building the `glyphshift` target also builds its shared library.

M1 accepts readable, executable, dynamically linked Linux x86-64 ELF targets;
scripts, static executables, and other binary architectures are rejected.
The module/build directory must not contain whitespace or colons because
`LD_PRELOAD` uses them as separators. Target paths and arguments may contain
spaces. Run as a normal user: root, mismatched real/effective IDs, secure
execution, process capabilities, setuid/setgid targets, and targets with file
capabilities are refused. The target runs with Linux `no_new_privs`, preventing
it and its descendants from acquiring privileges on later execs. Privilege
metadata is inspected on the same open file that is executed. This tool is
for local/offline applications; it does not detect whether a game is online.

Launcher usage errors return 2, missing executables return 127, and refused
or otherwise unlaunchable targets return 126. The application's own exit code
is returned after a successful exec. No installation or elevated privileges
are required.

The interposer exports only `glXSwapBuffers`, resolves the next symbol via
`dlsym(RTLD_NEXT, ...)` once with thread-safe lazy initialization, and forwards
the original display/drawable. It creates no GL resources and changes no GL
state. There is no graphics work in a constructor, per-frame allocation, or
per-frame logging. `GLYPHSHIFT_LOG=1` emits one initialization diagnostic on
the first swap; logging is otherwise off. A thread-local guard suppresses
recursive reentry, with at most one opt-in warning. If the original symbol
cannot be resolved, swaps are skipped with one opt-in diagnostic rather than
calling a null pointer; there is no alternative original presentation function
to forward to in that failure case. Initial symbol resolution/logging preserves
`errno`, and the downstream function's resulting `errno` is retained.

M1 covers the dynamically linked GLX swap symbol. Graphics entry points
obtained directly from driver procedure-address APIs have not been validated.
Vulkan applications can run through the launcher unchanged, but no Vulkan
entry point is intercepted and no Vulkan layer is enabled until M2.

### Run and check the fixtures directly

Run from an X11 session with working `DISPLAY` and Xauthority:

```bash
./build/dev/glyphshift-glx-fixture
VK_LAYER_VALIDATE_SYNC=1 ./build/dev/glyphshift-vulkan-fixture --validation
```

Both start at 800×600 and draw the same gradient: red increases to the right,
green increases downwards, and a blue sine wave moves diagonally. Both use the
same GLSL source, pixel-center coordinates, and direct UNORM output. Simulation
time is frame index / 60 seconds, independent of wall-clock timing; pacing
limits rendering to approximately 60 FPS. Slower GPUs advance animation more
slowly, without skipping simulation steps. No framebuffer readback occurs in
the render loop.

`Escape` or the window close button exits. Resize each window repeatedly,
including narrow and tall shapes, then close it and confirm exit status zero.
On a tiling window manager, make the fixture floating first; otherwise size
requests may be ignored. Vulkan recreates its swapchain on resize/out-of-date
presentation and uses two frames in flight. Device-idle waits are restricted
to swapchain recreation and teardown.

For bounded runs and reproducible images:

```bash
./build/dev/glyphshift-glx-fixture --frames 120
./build/dev/glyphshift-vulkan-fixture --frames 120
./build/dev/glyphshift-glx-fixture --fixed-time 1.25 --frames 1800
VK_LAYER_VALIDATE_SYNC=1 ./build/dev/glyphshift-vulkan-fixture \
  --validation --fixed-time 1.25 --frames 1800
```

`--frames N` accepts a positive integer; omission runs until closed.
`--fixed-time SECONDS` freezes animation at a finite, nonnegative simulation
time. Validation emits warning/error messages to stderr, remains active through
resource teardown, and any validation error makes the Vulkan fixture exit
nonzero. All other initialization/runtime failures also return nonzero with a
diagnostic. The fixture closes its X11 connection before unloading the Vulkan
ICD, keeping NVIDIA's Xlib close callbacks valid during teardown.

### Verified M0 environment and results

Verified on 2026-10-17, Linux x86-64, Arch Linux, X11 (`DISPLAY=:0`), i3
4.25.1, NVIDIA GeForce RTX 3060 Ti, NVIDIA 615.71.09. GL reported
`3.3.0 NVIDIA 615.71.09`; Vulkan reported device API `1.4.351`.
Build tools: GCC 16.2.1, CMake 4.4.3, Ninja 1.13.2, Xlib 1.8.13,
Vulkan headers/loader 1.4.357.0, glslang compiler 16.4.0
(Arch package 1:1.4.357.0-1).

The configure/build/CTest commands above were executed for both presets:
compilation completed without warnings and all three tests passed in each.
Both `--frames 120` runs rendered and exited zero after the teardown fix.
`spirv-val --target-env vulkan1.0` accepted both generated shader modules.

The installed environment lacked Khronos validation layers. For this check,
the official Arch package `vulkan-validation-layers-1.4.363.0-1` was downloaded
over HTTPS from `geo.mirror.pkgbuild.com` and extracted under
`/tmp/glyphshift-m0-validation/root`, without system installation. Its temporary
manifest was pointed at the extracted library. Validation runs used:

```bash
export VK_LAYER_PATH=/tmp/glyphshift-m0-validation/root/usr/share/vulkan/explicit_layer.d
export VK_LAYER_VALIDATE_SYNC=1
```

These environment overrides are only needed for that temporary setup; with the
package installed normally, just use `--validation` and `VK_LAYER_VALIDATE_SYNC`.

A temporary Python/X11 verification driver launched each preset's fixtures
with `--fixed-time 1.25 --frames 1800` (plus `--validation` for Vulkan). It
made only the fixture windows floating using `i3-msg '[id=WINDOW] floating
enable'`, then requested 320×240, 1024×768, 640×480, 1000×300, 300×800,
and 800×600, twice each. `xdotool getwindowgeometry --shell WINDOW` confirmed
every requested size. Both presets passed all 12 resizes per fixture and
exited zero after a synthetic Escape event. Vulkan core and synchronization
validation emitted no warnings or errors, including teardown.

ImageMagick captured the windows at fixed time after returning to 800×600.
`magick compare -metric RMSE` returned `0 (0)` for GLX versus Vulkan in both
presets on this GPU. This is an observed fixture result, not a promise of
byte-identical rendering across drivers.

A further Debug run of each fixture used the moving animation, repeated the
12 real resizes, and exited zero on `WM_DELETE_WINDOW`. Captures before and
after the resize sequence differed, confirming animation advances.

A source-only copy at `/tmp/glyphshift-m0-clean` (excluding `.git`, existing
builds, and local agent metadata) configured, built, and passed all three CTest
tests using the same six preset commands above. From that copy, the following
commands also passed with exit zero for all four executions:

```bash
for preset in dev release; do
  ./build/$preset/glyphshift-glx-fixture --frames 120 || exit
  VK_LAYER_PATH=/tmp/glyphshift-m0-validation/root/usr/share/vulkan/explicit_layer.d \
    VK_LAYER_VALIDATE_SYNC=1 \
    ./build/$preset/glyphshift-vulkan-fixture --validation --frames 120 || exit
done
```

Invalid `--frames 0` and inaccessible `DISPLAY=:9876` were also checked on both
Debug fixtures: both returned 1 with useful diagnostics. `git diff --check`
passed. The initial working tree was clean; existing repository instructions
were preserved.

No Mesa runtime, other GPU, Wayland, game, or Proton compatibility is claimed
by M0. Later-milestone integrations were not implemented or tested.

### Verified M1 results

Verified on 2026-10-17 in the same Arch/X11/i3/NVIDIA RTX 3060 Ti environment
recorded for M0. `clang-format --version` reported 22.1.8. The existing M0
working tree was retained and its C++ code was formatted; no commit was made.

The following commands were executed in the working checkout:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --build --preset dev --target format-check
python3 tools/format.py --check
git config --local core.hooksPath .githooks
git hook run pre-commit
git diff --check
nm -D --defined-only build/dev/libglyphshift_gl.so
ldd build/dev/libglyphshift_gl.so
```

Both presets built without warnings and passed all six CTest tests. Launcher
tests covered argument preservation (including empty arguments and spaces),
PATH lookup, launch from another directory, inherited preloads, `no_new_privs`,
exit 37, SIGTERM propagation, setuid/setgid refusal through direct paths and
symlinks, missing/non-executable targets, unsupported files, and a missing
module. Interposer tests exercised 8 concurrent threads and 8,000 forwarded
calls per run, exact arguments, `errno`, recursion, unavailable original
symbols, diagnostics emitted once, and silence when logging is disabled.
The hook test used a temporary Git repository and verified both directions
of partial staging, unchanged index/worktree, filenames with spaces, and
skipping deleted files. The root compilation database was parsed and contained
all ten C++ translation units, including the launcher and interposer; it
continued to point to Debug after Release configuration.

A temporary X11 verification driver compared direct and launcher executions
of each fixture in both presets at `--fixed-time 1.25 --frames 1800`.
For each execution, it floated only the fixture window, confirmed 12 actual
resizes (320×240, 1024×768, 640×480, 1000×300, 300×800, 800×600, repeated),
captured each size, sent `WM_DELETE_WINDOW`, and confirmed exit zero.
ImageMagick `compare -metric RMSE` reported `0 (0)` for all 12 direct versus
launcher images in each preset/API. With `GLYPHSHIFT_LOG=1`, GLX emitted exactly
one initialization line and Vulkan emitted none. Vulkan runs also enabled
Khronos core/synchronization validation using M0's temporary validation package;
there were no validation warnings/errors. These are fixture results on this
GPU, not broader compatibility claims.

The external test environment inherited a capability and was correctly
refused by the launcher. GUI verification was therefore run with
`setpriv --inh-caps=-all -- python3 /tmp/glyphshift-m1-check.py` (GLX) and the
corresponding temporary Vulkan driver, dropping capabilities for the test
process without changing the launcher policy. Actual root/setuid process
execution and file-capability targets were not exercised; setuid/setgid target
mode-bit refusal was tested without acquiring privileges.

A fresh source-only copy at `/tmp/glyphshift-m1-clean` configured and built
with `cmake --preset dev` / `cmake --build --preset dev`, passed all six tests
with `ctest --preset dev`, and generated its own root compilation-database
link with ten entries. Existing build artifacts and the original database link
were excluded from that copy.

The final Debug launcher was also run with logging unset and the Release
launcher with `GLYPHSHIFT_LOG=1`, both launching the GLX fixture with
`--frames 120`. Both terminated naturally with exit zero; Debug was silent
apart from the fixture's own output, and Release emitted exactly one
GlyphShift initialization line.

M1 stops at pass-through interposition. No Vulkan layer, glyph rendering,
editor, game/Proton integration, or additional backend was implemented.

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

## M0 implementation checklist

The M0 implementation and verification above cover:

1. Create the minimal CMake project and presets.
2. Add small GLX and Vulkan moving-gradient fixtures with equivalent output.
3. Build both with warnings enabled.
4. Run both under X11, resize them repeatedly, enable Vulkan validation, and
   confirm clean shutdown.
5. Record the exact verified setup and build commands here.

M0 and the separately requested M1 are complete. M2 and later milestones
require separate requests; do not start them automatically.
