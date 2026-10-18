# GlyphShift implementation and remaining work

This is the authoritative engineering reference for product scope, architecture,
constraints, milestone acceptance criteria and remaining work. The
[main README](../../README.md) presents the software;
[the test guide](../../tests/README.md) provides hands-on commands.
All shell commands in this document run from the repository root.

## Status at a glance

M0–M5 are complete within their recorded acceptance scope. Subsequent completed
work adds glyph strength, Unicode/custom character sets and local fonts,
real-application X11 editor input, and illustrated fill/contour rendering.
This does not mean every control in the original product vision or every Linux
application is supported.

The latest renderer validation recorded here passed 12 Debug tests and 14
Release tests, including GLX/Vulkan graphics smoke with Vulkan validation.
Focused editor tests passed on both fixtures and offline SuperTux through
GE-Proton/DXVK. Mesa llvmpipe GLX passed; Mesa Vulkan and the Mesa offscreen
oracle remain unverified for the environment-specific reasons recorded below.
These are existing results, not new tests performed by this documentation split.

## Remaining TODOs

These items are pending scope, not instructions to begin implementation.
Only implement work explicitly requested by the user.

### Rendering and editor controls

- [ ] Art direction phase 3: automatic ink/paper polarity per cell, saturation,
  optional phosphor glow and recalibrated included styles. Source-color
  styles still fill bright scenes with dense glyphs, and directional texture
  (SuperTux ice) still produces contour strokes at the shipped thresholds.
- [ ] Measure the GPU cost of the phase 2 analysis pass. Shape matching reads
  16 descriptor texels per matchable glyph in each high-contrast cell.
- [ ] Add configurable temporal glyph stability/hysteresis. The current
  illustrated renderer filters spatially and has no previous-frame glyph state.
  The phase 2 cell texture is intended to carry the previous frame's choice.
- [ ] Add explicit aspect correction beyond independent cell dimensions.
- [ ] Add saturation control; exposure, contrast and gamma already exist.
- [ ] Validate visual quality and motion across longer gameplay sessions and
  multiple scene types, including fine HUD/text readability.
- [ ] Measure GPU execution cost and gameplay performance. The editor currently
  reports CPU preparation/submission time, not GPU execution time.

### Compatibility and validation

- [ ] Retry Mesa Vulkan on an environment where the direct fixture can create
  a drawable; the recorded NVIDIA X server fails before GlyphShift injection.
- [ ] Run the analytic glyph-filter oracle with Mesa in a working offscreen
  GLX environment; windowed Mesa GLX tests already passed.
- [ ] Expand coverage to AMD/Intel hardware and additional offline Proton games.
  VKD3D/D3D12 coverage is not established by the SuperTux/DXVK test.
- [ ] Investigate application-owned active input grabs, general XI2/raw-input
  suppression and IME input before claiming support for those paths.

### Later Linux support and distribution

- [ ] Investigate native Wayland input and a Gamescope-style compositor backend.
- [ ] Add EGL only for a concrete compatibility target.
- [ ] Design Steam-specific installation and profile UX.
- [ ] Choose and add a project license before presenting the repository as
  licensed open source; the existing ImGui license covers that dependency only.

The original exclusions remain in force: no multiplayer/anti-cheat support,
anti-cheat evasion, privileged targets or system-wide injection.

## Typographic glyph atlas (art direction, phase 1)

The user reported that full-glyph output read as visual noise rather than
ASCII art. Inspection of the atlas found two causes independent of the shader:

- Font glyphs were centered by their own bounding box, so `.`, `_`, `-`, `,`,
  `` ` `` and `'` all landed in the middle of the cell. `_` and `-` were
  identical, and characters could not express a floor, ceiling or outline side.
- The generic `monospace` alias resolved to Nimbus Mono PS (a thin Courier
  clone) at a fixed 20 px. Its 1 px strokes became half-intensity hairlines at
  8×12 cells.

Font glyphs now share one set of tile metrics. The face is scaled so the
monospace advance of `0` fits the 16-pixel tile width, or its ascender..descender
line fits the 24-pixel height, whichever is smaller. Every glyph is placed on
that common baseline and centered by advance, not by its ink. Ink that
overhangs the tile is nudged inside; only glyphs larger than the tile (for
example wide CJK) keep the earlier scale-to-fit placement. Non-scalable faces
keep the 20-pixel size with baseline placement.

With an empty `glyph_font`, discovery now asks Fontconfig for a bold face of
DejaVu Sans Mono, Hack, Liberation Mono, Noto Sans Mono, Cascadia Mono,
JetBrains Mono or Source Code Pro, in that order. A candidate is accepted only
when the match reports that family. Otherwise the system `monospace` alias is
used in bold. An explicit `glyph_font` path is unaffected.

A new `glyph_weight` setting (0..1, default 0) thickens outline font glyphs by
up to two tile pixels, about one pixel at an 8×12 cell. It belongs to the
character selection, so changing it rebuilds the atlas like other character
changes and never per frame. The editor's **Characters and font → Glyph weight**
slider commits on release instead of rebuilding while dragging. The slider sits
after **Apply characters**, so existing editor test coordinates are unchanged.
The setting is parsed, validated and serialized in named presets. Procedural
blocks, Braille and the built-in classic bitmap ignore it.

No shader, backend, lookup-texture layout or GPU resource changed. Coverage
calibration, density ordering and shape lookups are recomputed from the new
masks as before, so saved presets with font sets now render with the new
glyphs. The [preset gallery](../../presets/README.md) images were regenerated
with `glyphshift-preview` from the same captured frame.

### Phase 1 verification

Verified on 2026-10-17 on the existing Arch/X11/i3/NVIDIA RTX 3060 Ti setup.
With no DejaVu installed, discovery selected Hack Bold; the atlas was dumped
and inspected. Debug built without compiler warnings and passed all 12 CPU
tests. The character test now checks that `_` and `.` sit below `-`, `'` and
`` ` `` sit near the top, punctuation keeps its order, and weight 1 adds
over 25% ink to `@` without inking space. Invalid weights are rejected.
Preset tests round-trip a nondefault weight. The Debug graphics smoke passed on
GLX (including the analytic area/silhouette oracle) and Vulkan with Khronos
core/synchronization validation (72 seconds). The focused editor/XTest tests
and SuperTux were not run for this change; the new slider was not exercised
through real input.

```bash
cmake --build --preset dev
setpriv --inh-caps=-all --ambient-caps=-all -- ctest --preset dev
setpriv --inh-caps=-all --ambient-caps=-all -- env \
  VK_LAYER_PATH=/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d \
  VK_LAYER_VALIDATE_SYNC=1 cmake --build --preset dev --target smoke
python3 tools/format.py --check
git diff --check
```

A scratch two-pass prototype, outside the repository, rendered the same
SuperTux frame with per-cell shape matching over this atlas. Silhouettes and
outlines became oriented characters (`|`, `_`, `^`, `T`) instead of a pixel
mosaic. That result motivates phase 2; it is not a shipped renderer.

## Two-pass glyph analysis (art direction, phase 2)

Phase 1 made characters legible; outlines still rarely became characters.
The illustrated renderer read nine point samples per cell, classified one
cell-scale Sobel direction, and matched only 2×2 light/dark patterns. Fine
texture aliased into scattered glyphs, and text glyphs could not express a
silhouette's side, floor or slope.

The illustrated mode now runs two passes on GLX, Vulkan and
`glyphshift-preview`. Both come from `shaders/glyph/glyph.frag`; CMake builds
the first with `ANALYSIS` defined.

1. **Analysis**, one fragment per cell, writes one `RGBA32UI` texel: ink and
   paper as 10-bit-per-channel RGB, the fill glyph, and the contour (0 none,
   1..4 axis). An integer target is never blended, whatever blend state the
   host application left enabled.
2. **Composition**, per pixel, reads that texel, integrates the fill and
   contour masks from the summed-area atlas, chooses the adaptive stroke, and
   applies glyph strength and sRGB output exactly as before.

Classic mode is unchanged: it still renders in the composition pass, and its
images match the previous renderer. Analysis per cell:

- **Tone and color** come from sixteen region means (each a 2×2 texel box),
  replacing the nine point samples.
- **Contours vote.** A 5×5 lattice at quarter-cell steps spans the cell plus
  the first pixels of its right and lower neighbors, so a boundary on the grid
  belongs to exactly one cell. Each of the nine interior points applies the
  existing `contour_glyph` rule to its 3×3 neighborhood, with differences
  doubled so `edge_threshold` keeps its half-cell units. An axis wins with at
  least three votes (a straight contour crossing the cell) and at least twice
  any other axis. `contour_vote` mirrors this on the CPU.
- **Shapes match descriptors.** When `shape_matching` is on and the regions'
  luminance range exceeds `shape_threshold`, the regions are normalized to
  0..1 and compared against a 4×4 coverage descriptor of every matchable
  glyph. Each descriptor is scaled so the glyph's densest region is 1.
  Monochrome dark-on-light inverts the target, as before. The best glyph is
  used only when its mean squared error is at most `shape_fit` (0.06). Clean
  outlines fit closely; tiled texture does not and keeps the tonal ramp.
  `shape_glyph` mirrors this on the CPU. Matched cells keep the existing
  light/dark group palette and suppress contour strokes.

A new optional `shape_characters` setting lists glyphs used only by shape
matching, never by tonal fill, for example
`shape_characters="/\\|_()<>^v`',.-LJ7[]T"`. They occupy the atlas slots after
the fill set; fill plus shape characters must fit 256 slots, or the selection
is rejected. The editor's **Illustration → Shape glyphs** field is applied by
**Apply contour / shape glyphs** (formerly Apply contours), together with the
contour glyphs. It sits below every control the focused editor tests click.
Settings, presets and validation round-trip the new key.

The lookup texture grows from 4 to 21 rows of 256 bytes. Row 4, texel 0 holds
the matchable glyph count minus one; rows 5..20 hold the descriptors. Push
constants stay at 96 bytes and the art parameters are unchanged.

Backend resources:

- **GLX:** a second program, plus a per-drawable cell texture and FBO
  allocated only when the window or cell size changes the grid. Texture unit 3
  is now saved and restored with units 0..2. NVIDIA compiles a program for the
  framebuffer bound at link time; the analysis program first drawn into an
  FBO was recompiled with a performance warning, which the GL debug smoke
  correctly rejected. The analysis program is therefore linked while a
  temporary 1×1 integer FBO is bound. This was traced to that exact draw with
  synchronous debug output before the change.
- **Vulkan:** one shared cell image per swapchain renderer, sized for 2×2
  cells so live cell-size edits never reallocate, plus an analysis render pass,
  pipeline and framebuffer. Its subpass dependencies order the rewrite after
  the previous frame's composition reads (same queue, submission order) and
  make the writes visible to this frame's composition. The pass runs whenever
  the effect is enabled, so the image is always in the layout the composition
  descriptor declares.

The included Newsprint and Green terminal styles and `illustrated-ink.conf`
replace their partial-block silhouettes with ASCII `shape_characters`. With
partial blocks present, their exact 0/1 descriptors win most structural
matches. The gallery images, [shape-color](../shape-color.png) and
[shape-ink](../shape-ink.png) were regenerated with the two-pass preview.

### Phase 2 verification

Verified on 2026-10-17 on the existing Arch/X11/i3/NVIDIA RTX 3060 Ti setup.
Debug and Release built without compiler warnings. Debug CTest passed 12/12.
Release CTest passed 14/14, including both graphics smokes with Khronos core and
synchronization validation (90 seconds). The Debug smoke target also passed on
both APIs with no `GL debug:` or `Vulkan validation:` output. The analytic
oracle passed unchanged against the two-pass preview: 14 area-filter cases,
2 contour-over-fill cases and 28 silhouette cases, plus the exact
strength-zero bypass. Nine illustrated smoke captures compared GLX against
Vulkan: eight were identical and one had normalized RMSE 0.0000033. All six
glyph SPIR-V modules passed `spirv-val --target-env vulkan1.0`.

New CPU checks cover contour votes (grid-edge ownership, both axes, a
backslash diagonal, checkerboard/flat/zero-strength rejection), descriptor
matching of all 14 mixed quadrant masks in both polarities, `|` and `_` from
shape characters, rejection of a checkerboard no glyph fits, shape glyphs
staying out of every tonal row, the 256-slot limit, and persistence. A
deliberately wrong expectation was confirmed to fail the art test.

The first full smoke after the shader change failed on the NVIDIA recompile
warning described above; an exact-float cell format was tried and did not
remove it, and was reverted. The focused XTest editor tests, SuperTux and GPU
timing were not run for this change: the Shape glyphs field was not exercised
through real input, and the analysis pass's GPU cost is unmeasured.

```bash
cmake --build --preset dev
setpriv --inh-caps=-all --ambient-caps=-all -- ctest --preset dev
setpriv --inh-caps=-all --ambient-caps=-all -- env \
  VK_LAYER_PATH=/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d \
  VK_LAYER_VALIDATE_SYNC=1 cmake --build --preset dev --target smoke
cmake --build --preset release
setpriv --inh-caps=-all --ambient-caps=-all -- env \
  VK_LAYER_PATH=/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d \
  VK_LAYER_VALIDATE_SYNC=1 ctest --preset release
spirv-val --target-env vulkan1.0 build/release/generated/glyph.analysis.spv
```

## Ready-to-use style collection

Six complete live configs are available in the [preset gallery](../../presets/README.md):
Newsprint, Green terminal, Blueprint, Amber Braille, Mosaic and Soft color.
Five use pure glyph output; Soft color deliberately blends original detail at
strength 0.65. They use existing renderer controls and do not add a milestone,
backend or installation step. A later change embeds them at build time and lists
them under **Insert → Preset → Included styles**.

Each `presets/art-*.conf` was passed to the current Debug `glyphshift-preview`
against the same captured SuperTux frame. All six parsed without warnings,
loaded their glyph atlas without fallback and rendered successfully on the
existing NVIDIA/X11 setup. The distinct outputs were inspected and saved under
`docs/presets/`, with a labeled overview. Documentation shell syntax, local
links and whitespace were checked. No C++ or shader code changed, and no new
live game or Vulkan run was performed for this preset-only addition.

## Shape-aware illustration refinement

The user reported that the illustrated preset hid Tux and produced distracting
repeated symbols/solid regions. The renderer now offers three additional live
settings, serialized in per-game presets:

```ini
shape_matching=true
shape_threshold=0.35
scene_paper=0.95
```

*Superseded in part by [phase 2](#two-pass-glyph-analysis-art-direction-phase-2):
matching now uses 4×4 descriptors of every matchable glyph with a fit limit,
and the lookup texture has 21 rows.*

`shape_matching` defaults to false for compatibility. It samples the four
interior quadrant centers of a cell and classifies their light/dark arrangement
when local contrast exceeds `shape_threshold` (0..1, default 0.2). A precomputed
16-entry lookup selects the actual chosen glyph whose four quadrant coverages
best match that arrangement. No per-frame CPU readback or GPU search over the
font is needed. Custom character sets and local fonts still determine the
available shapes; including Unicode quadrant blocks allows exact 2×2 silhouettes.
Without those blocks the closest available text glyph is used.

Matched cells use the two sampled source-color groups as their palette, keeping
both dark silhouettes and light interior details. Monochrome uses the selected
ink/paper colors and inverts the structural pattern for dark ink on light paper.
Centered contour strokes are suppressed inside matched cells so that a slash
or bar cannot overwrite their silhouette. Outside matched cells, ordinary tone
mapping and directional contours remain active.

When matching is enabled, partial Unicode blocks are excluded from the tonal
ramp and reserved for shape selection, avoiding repeated half-block stripes in
uniform surfaces. Solid blocks remain eligible for highlights. A partial-block-only
set falls back to its full selection rather than leaving an empty ramp. Manual
ordering, duplicates, density sorting and reversal continue to govern tonal fill;
structural matching uses the spatial mask rather than tonal order.

`scene_paper` (0..1, default 0) blends the configured paper toward cell-average
scene color, and moderates normalized ink to retain dark-region brightness.
In matched cells it uses the darker sample group instead of the cell average.
It affects source color only. These are constant cell palettes composited
through glyph masks: strength 1 does not blend per-pixel source detail into the
result. Strength 0 retains the exact original source-color image.

The lookup texture now has four 256-byte rows: legacy ordering, illustrated
tone, structural patterns, and tonal fill excluding partial blocks. GLX, Vulkan
and the preview tool all upload the same data. The existing four vec4 art
parameters carry the controls in previously reserved components, keeping Vulkan
push constants at 96 bytes. Atlas regeneration remains limited to character/font
changes; changing these art controls does not rebuild GPU resources.

The recommended color/ink presets now use 6×10 cells to preserve smaller objects,
with sparse tonal ramps, stronger contour rejection and optional scene-colored
paper. The coarser blocks preset retains the earlier style. No temporal state or
hysteresis is implemented by this refinement; motion stability remains a TODO.

Same-frame GPU previews: [color](../shape-color.png), [ink](../shape-ink.png),
[original](../illustration-original.png). Earlier gallery assets below preserve
historical results and settings. The new comparisons show a captured frame,
not a gameplay smoothness measurement.
The [3× Tux close-up](../shape-tux-comparison.png) compares the previous 8×12
preset (left) and revised 6×10 preset (right); both rendering and cell-size
changes contribute to that comparison.

### Shape-refinement verification

Verified on the existing NVIDIA/X11 environment. Debug built without warnings
and passed all 12 tests. Release built without warnings and passed all 14 tests,
including both expanded graphics smokes with Khronos synchronization validation
(73.17 seconds). Final editor slider-width adjustments and additional persisted
settings coverage were then rebuilt in both presets; Debug passed 12/12 again,
and the four relevant Release format/illustration/preset/editor tests passed.

The CPU test checks all 16 structural masks exactly, reserves partial blocks
from tonal shading and checks a partial-block-only fallback. Settings and preset
tests cover validation and nondefault persisted shape/paper settings. The GPU
oracle passed the existing 16 area/contour cases plus 28 structural cases
(14 mixed masks in source color and reversed monochrome), and two exact
strength-zero bypasses. Both graphics smokes additionally exercise live
shape/paper configuration and restoring the legacy renderer.

Final focused Release tests on GLX/Vulkan fixtures and offline SuperTux clicked
the new shape checkbox, checked `shape_matching=true` in the saved preset, and
passed input, focus, three resizes and clean exit. The SuperTux level run loaded
the revised color preset and captured the [running editor](../shape-live-editor.png).
No GL debug or Vulkan validation errors were reported. The new source-color and
ink previews and the live capture were inspected directly. These checks do not
measure smoothness over a longer play session or claim new Mesa/hardware coverage.

Commands actually executed included:

```bash
cmake --build --preset dev
ctest --preset dev
cmake --build --preset release
setpriv --inh-caps=-all --ambient-caps=-all -- env \
  VK_LAYER_PATH=/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d \
  VK_LAYER_VALIDATE_SYNC=1 ctest --preset release --output-on-failure
setpriv --inh-caps=-all --ambient-caps=-all -- \
  python3 tests/smoke/atlas_filter.py --build build/dev \
  --output /tmp/glyphshift-shape/oracle
setpriv --inh-caps=-all --ambient-caps=-all -- env \
  VK_LAYER_PATH=/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d \
  VK_LAYER_VALIDATE_SYNC=1 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
  /tmp/glyphshift-offline /usr/bin/python3 tests/smoke/editor_apps.py \
  --build build/release --application supertux --level --illustrated \
  --output /tmp/glyphshift-shape/live-supertux
```

The final focused matrix repeated the editor command for `glx-fixture`,
`vulkan-fixture` and `supertux`, with `--level` only for SuperTux; output is under
`/tmp/glyphshift-shape/final-APPLICATION`. Temporary prerequisites are described
in the test guide. Formatting/whitespace, Python syntax and all eight generated
glyph/editor SPIR-V modules also passed. No packages or downloads were added.

## Engineering reference and validation history

The sections below preserve the original implementation brief, milestone exit
conditions, commands and verification evidence. Earlier verification sections
are historical snapshots: their test counts and limitations describe that
stage. Later character-selection, X11 input and illustrated-renderer sections
record the corresponding updates. In particular, the initial M5 editor-input
limitations were subsequently addressed for the tested applications.
Temporary `/tmp` tools, games and logs mentioned here are not repository assets
and may no longer exist. Use the test guide for current manual launch commands.

## Current implementation

M0 through M5 are complete. Both backends apply the same built-in glyph atlas
and ramp, with live cell dimensions, source/monochrome color, and effect toggle.
M4 adds the shared Dear ImGui editor and Insert/Escape/F10 controls.
M5 adds per-game named presets, automated graphics smoke tests and robustness
coverage. The recorded M5 matrix includes native applications and an offline
SuperTux Windows run through portable GE-Proton/DXVK. Input limitations remain
explicit below; this does not claim general game compatibility or every future
product control. A subsequent user-requested legibility pass adds configurable
glyph strength and readable/classic example presets. A further explicit user
request adds selectable character sets, custom UTF-8 sequences and local fonts,
described below.
A subsequent explicit request adds X11 editor input for real applications,
including privately resolved Xlib paths and the tested SuperTux/Proton setup;
see [real-application editor input](#real-application-editor-input-requested-after-m5).
An explicit subsequent request adds an opt-in illustrated renderer with
independent contours/fill, filtered glyph coverage and tonal/color controls;
see [illustrated rendering](#illustrated-rendering-requested-after-m5).
Historical validation results are retained below.

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

## Development (M0–M5)

For hands-on fixture and offline SuperTux commands, including capability
dropping and live visual adjustments, see the [test guide](../../tests/README.md).

### Dependencies

Build on Linux x86-64 with a C++20 compiler (including floating-point
`std::from_chars`), CMake 3.20 or newer, Ninja, Xlib development headers,
OpenGL/GLX development libraries, Vulkan headers and loader, and
`glslangValidator`. Character atlas generation additionally needs FreeType,
Fontconfig and pkg-config development files. Real-application editor input
additionally requires libxcb and xcb-keysyms development files. Nonprocedural sets need an
installed monospace font or an explicit local TTF/OTF path.
Tests and developer formatting commands additionally use
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
  python git clang freetype2 fontconfig pkgconf ttf-dejavu libxcb xcb-util-keysyms
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
display. When `clang-format` is present, it also tests the pre-commit hook. These do not
claim graphics coverage.
The fixture, glyph and editor SPIR-V shaders are compiled locally by CMake.
Dear ImGui v1.91.9b is vendored under `third_party/imgui`, with its MIT license,
source URL and archive checksum. Configuration/builds require no downloads.
Vendor sources are kept unmodified, exempt from the project formatting hook
and Git whitespace checks, and compiled with upstream warning conventions; project sources retain all
warning flags and formatting checks. Fixture shader files live in the corresponding build tree;
reconfigure/rebuild after relocating a checkout or removing that tree.

### Code formatting and pre-commit

`.clang-format` inherits the Google preset and explicitly overrides layout for
readability: C++20, four-space indentation, a 100-column limit, separate lines
for control-flow bodies and nonempty functions, and blank lines between
definitions. Wrapped calls and declarations use one argument/parameter per
line. Braces stay attached and pointers bind to the type (`Type* value`).
The project's C++ sources, headers and fixtures use this style; vendored
`third_party` sources are excluded. The same configuration is enforced by the
pre-commit hook.
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

### Launch with GlyphShift (M1–M5)

```bash
./build/dev/glyphshift -- ./build/dev/glyphshift-glx-fixture
GLYPHSHIFT_LOG=1 ./build/dev/glyphshift -- \
  ./build/dev/glyphshift-glx-fixture --frames 120
VK_LAYER_VALIDATE_SYNC=1 ./build/dev/glyphshift -- \
  ./build/dev/glyphshift-vulkan-fixture --validation
```

The launcher finds `libglyphshift_gl.so` next to its own executable, prepends
its absolute path to `LD_PRELOAD`, preserves existing preloads, and executes
the inspected target file descriptor. It supports executable paths or `PATH`
lookup, preserves arguments and the current directory, and replaces itself
with the target so exit codes and signals propagate naturally. Environment
changes affect only this launch and its descendants; the invoking shell is
unchanged. Building the `glyphshift` target also builds its shared library.

The launcher accepts readable, executable, dynamically linked Linux x86-64 ELF targets;
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

The interposer exports `glXSwapBuffers`, `glXMakeCurrent`,
`glXMakeContextCurrent`, `glXDestroyContext`, `glXGetProcAddress` and
`glXGetProcAddressARB`, plus the Xlib input hooks
described under M4. It resolves the next symbols
via `dlsym(RTLD_NEXT, ...)` with thread-safe lazy initialization and forwards
the original arguments. Context lifecycle hooks release owned resources while
the context is still current. The M3 pass below runs before the original swap
when enabled. There is no graphics work in a constructor, steady-frame heap
allocation, or per-frame logging. `GLYPHSHIFT_LOG=1` emits one initialization diagnostic on
the first swap; logging is otherwise off. A thread-local guard suppresses
recursive reentry, with at most one opt-in warning. If the original symbol
cannot be resolved, swaps are skipped with one opt-in diagnostic rather than
calling a null pointer; there is no alternative original presentation function
to forward to in that failure case. Initial symbol resolution/logging preserves
`errno`, and the downstream function's resulting `errno` is retained.

M1 covers the dynamically linked GLX swap symbol. M5 also routes known GLX
hooks returned by `glXGetProcAddress`/`ARB` through GlyphShift and forwards all
other procedure lookups. Its GLX smoke obtains the swap function this way.
The Vulkan layer described below is also enabled for applications launched
through GlyphShift; GLX applications do not initialize Vulkan as a result.

### Vulkan loader layer (M2–M5)

The build adds `libglyphshift_vk.so` and
`build/dev/layers/VK_LAYER_GLYPHSHIFT_overlay.json` (or the Release equivalent).
This is an explicit development layer, not a system or implicit layer; the
manifest's `GLOBAL` type means it covers instance and device calls. Nothing
is installed in the system's Vulkan search directories.

The launcher checks both renderer libraries, `libglyphshift_editor.so` and the manifest, then enables
`VK_LAYER_GLYPHSHIFT_overlay` through `VK_INSTANCE_LAYERS` for the launched
process and its descendants. Existing requested layers are preserved. It
prepends the local manifest directory to an existing `VK_LAYER_PATH` override;
otherwise it prepends to `VK_ADD_LAYER_PATH`, preserving normal system layer
discovery. This keeps system-installed validation layers available. Both loader
discovery variables and the enable request are scoped to this launch, just like
`LD_PRELOAD`; running a fixture directly does not enable GlyphShift.

The layer follows the [Khronos loader/layer interface](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md),
negotiates interface version 2, and advances each loader creation-chain link
before calling the next layer. Its only exported ELF entry point is
`vkNegotiateLoaderLayerInterfaceVersion`; procedure-address functions are
returned through negotiation. It does not link to the Vulkan loader or patch
Vulkan entry points.

Dispatch tables are registered per instance/device using the loader's dispatch
keys. Physical-device and queue handles route through their owning instance
and device, respectively; original handles are never wrapped or overwritten.
Physical enumeration (including core/KHR device groups), logical-device
creation/destruction, both queue retrieval methods, swapchain creation
(including shared swapchains), image discovery, destruction, and presentation
are intercepted. Queue family/index/flags and swapchain format/extent/images
are retained as CPU metadata. Unknown commands forward to the appropriate
next procedure-address function, including the physical-device chain; optional
commands unavailable downstream remain null.

When both effect and editor are disabled, or outside the supported capture
path, presentation forwards
the exact `VkPresentInfoKHR` pointer, wait semaphores,
image indices, `pNext`, per-swapchain results, and return code. It performs no
rendering, command submission, GPU wait, image-layout transition, readback,
or heap allocation. Registry locks are released before downstream calls.
The enabled M3 path adds the GPU work and synchronization described below;
swapchain recreation remains the application's responsibility.
CPU metadata is removed on destruction. Optional metadata allocation failures
leave forwarding intact and emit at most one opt-in warning per owner. If
mandatory dispatch-state allocation fails during instance/device creation,
the layer returns `VK_ERROR_OUT_OF_HOST_MEMORY` and rolls back any downstream
object already created using the original allocation callbacks.

`GLYPHSHIFT_LOG=1` reports instance/device and swapchain lifetime, and the first
presentation per device. There is no repeated per-frame log. With validation
enabled, the Vulkan loader may additionally emit general messages announcing
`VK_INSTANCE_LAYERS`; the fixture labels these `Vulkan loader:` separately from
`Vulkan validation:` messages. They remain visible. All error severities still
make the fixture exit nonzero, including errors during teardown.

For a development check with the temporary validation package from M0:

```bash
VK_LAYER_PATH=/tmp/glyphshift-m0-validation/root/usr/share/vulkan/explicit_layer.d \
  VK_LAYER_VALIDATE_SYNC=1 GLYPHSHIFT_LOG=1 \
  ./build/dev/glyphshift -- ./build/dev/glyphshift-vulkan-fixture \
  --validation --frames 120
```

With validation installed normally, omit `VK_LAYER_PATH`. Run each preset,
resize the fixture repeatedly, and confirm clean exit and matching
`Vulkan swapchain created` / `Vulkan swapchain destroyed` diagnostics. The
manifest's library path is relative to its own location, so the launcher,
libraries and `layers/` directory must stay together. Reconfigure after moving
the source/build trees, as with the M0 fixture shader paths.

### Glyph rendering and live controls (M3)

GLX and Vulkan apply the GPU glyph pass by default, using an original built-in 8×8
bitmap atlas and the shared ramp ` .:-=+*#%@`. Settings are independent from
both APIs; shaders receive the ramp as lookup-texture data. A four-sample cell
average uses shared Rec.709 luminance coefficients. GLSL is shared by both
backends and embedded into the libraries (SPIR-V for Vulkan); there is no
runtime glyph shader-file dependency.

The M3 live settings file remains optional:

```bash
cat > /tmp/glyphshift.conf <<'EOF'
enabled=true
cell_width=8
cell_height=12
color_mode=source
EOF
GLYPHSHIFT_CONFIG=/tmp/glyphshift.conf ./build/dev/glyphshift -- \
  ./build/dev/glyphshift-glx-fixture
```

Edit or atomically replace that file while the application runs. `enabled`
accepts true/false or 1/0; each cell dimension accepts 2..128 independently;
`color_mode` accepts source/mono. Missing keys use defaults (enabled, 8×12,
source color, glyph strength 0.45). `glyph_strength` accepts finite values
from 0 to 1: 0 preserves source pixels (grayscale in mono mode), 1 renders
pure glyphs, and intermediate values blend scene detail with glyph rendering.
Invalid values warn once per reload and use defaults. Files
must be regular, readable, and at most 4096 bytes. The steady path only checks
file metadata; parsing/read allocation occurs on changes. There is no CPU
framebuffer readback, signal handler or global input grab. Changed file values
replace the session settings at the next frame. The file is never rewritten
by the editor. M5 persistence uses separate named preset files described below.

The GLX pass snapshots/restores all modified state, copies the default back
buffer into an RGBA8 texture on the GPU, and draws the atlas via a fullscreen
triangle. Context/drawable resources are separate. Capture textures are reused
on resize. GL resources are released while the owning context is current,
before interposed context switches/unbinding or current-context destruction;
a subsequent activation initializes lazily again. Destruction on another
thread marks a current context for retirement at its next unbind. Active
queries or bound transform-feedback buffers conservatively skip the effect.
`GLYPHSHIFT_FIXTURE_GL_DEBUG=1` enables the fixture's driver debug callback.

`GLYPHSHIFT_FIXTURE_STATE_CHECK=1` surrounds each fixture swap with an
independent adversarial state snapshot/check, including indexed color/blend,
fractional/indexed viewports, scissor, clip control, FBO routes, textures,
samplers, pixel-store settings and pack/unpack buffers when supported.

The Vulkan layer requests transfer-source and color-attachment usage only
when the surface supports it. A rejected expanded-usage creation is retried
with the original request. Capture is a GPU image copy; only the built-in
atlas/ramp are uploaded from host memory. Each swapchain image owns a capture
texture, framebuffer, command buffer, submission fence and completion
semaphore. The added submit waits on the application's original presentation
semaphores, transitions PRESENT → TRANSFER_SRC → COLOR_ATTACHMENT → PRESENT,
and signals the image's completion semaphore for downstream presentation.
Other presentation fields, per-swapchain results and return codes are retained.
Command buffers receive the loader's dispatch initialization callback.
No per-frame device/queue-idle wait or steady-frame heap allocation is used.
The renderer waits for its queue at swapchain teardown/resize; this follows
the conventional unextended WSI lifetime model, whose teardown limits are
explained in the [Khronos semaphore-reuse guide](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).

The M3 Vulkan capture path supports SDR RGBA/BGRA 8-bit UNORM/SRGB, one image
layer, and an unprotected graphics presentation queue. SRGB capture uses an
UNORM view of the copied bytes and compensates for output attachment encoding.
The renderer submits on the application's graphics presentation queue. For
exclusive sharing, Vulkan requires that queue family to own the image at
presentation; additional transfer/compute families on the device are allowed.
Concurrent sharing requires the presentation family in the declared list.
M5 preserves known presentation pNext metadata (fences, present mode, present
IDs/ID2 and EXT timing) and accepts mutable-format/ID2/wait2/timing swapchain
flags, without changing the caller's chain or enabling extra device features.
Newer metadata is recognized only when available in the build's Vulkan headers.
The copied capture image uses its existing UNORM format; mutable-format does
not request extra reinterpretation of the application's swapchain images.
See the Khronos [presentation ownership contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html),
[presentation fences](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html)
and [timing metadata](https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentTimingsInfoEXT.html).
Multi-swapchain present batches, unknown or overlong presentation `pNext`
chains, shared/protected swapchains, unsupported create flags, queue changes,
and unsupported formats/usage conservatively forward unchanged, with an opt-in
once-per-device diagnostic. Initialization failures disable that swapchain's
effect and report the exact failure. These are fixture-scoped M3 capabilities;
M5 owns broader multi-context/swapchain and application coverage.

### Shared in-game editor (M4)

Launch either fixture through GlyphShift using the commands above, then:

- `Insert` opens/closes the editor.
- `Escape` closes the editor and is consumed; the application keeps running.
  With the editor already closed, Escape reaches the fixture and exits it.
- `F10` toggles the glyph effect with the editor open or closed.

The same Dear ImGui panel exposes the M3 renderer settings: effect enabled,
independent cell width/height (2..128 pixels), monochrome/source color, and
glyph strength (0..1), and reset to defaults. The panel fits the window and scrolls in small viewports.
The title-bar close button also returns input immediately. Keyboard navigation
uses Tab, arrows, Space and Enter; Ctrl-clicking a slider permits numeric entry.
It displays the backend/API version, previous GlyphShift CPU preparation and
submission time in milliseconds (including per-image fence waits, not GPU
execution time), active preset, and the latest shader/initialization diagnostic.
M5 adds preset selection, a name field and explicit Save preset action, without
adding rendering parameters beyond M3.

`libglyphshift_editor.so` owns the API-independent settings and editor model,
the shared font atlas and ImGui contexts, serialized across presentation
threads. This keeps input hooks in the preloaded GLX library and the Vulkan
layer connected to the same session state, including in Vulkan-only apps.
All three shared libraries and the launcher must remain together; runtime
library lookup uses `$ORIGIN`. Each GPU backend uploads the font atlas and
renders ImGui geometry into its own target after the glyph pass. Vulkan uses
mapped, coherent per-swapchain-image geometry buffers, written only after that
image's submission fence completes. GLX additionally restores array-buffer
bindings and separate/indexed blend factors/equations. Closing the editor with
the effect disabled skips all GPU rendering work. First use, window/target
initialization and geometry-capacity growth may allocate; stable frames reuse
resources. ImGui symbols remain hidden from host applications.

The original M4/M5 X11 adapter filters Xlib events for registered presentation
windows only. The newer real-application input path below supersedes its
private-Xlib limitation for the tested applications.
GLX identifies the current drawable; Vulkan tracks Xlib surface creation to
find its display/window. Supported event paths are `XPending`, `XEventsQueued`,
`XNextEvent`, `XPeekEvent`, `XCheckWindowEvent`, `XCheckMaskEvent` and
`XCheckTypedEvent`. Open-editor keyboard/pointer presses and matching releases
are consumed. Releases for keys already held by the application before opening
remain available to it, preventing stuck host keys. Hotkey repeats are debounced,
and releases for captured keys/buttons remain suppressed after closing. Focus
loss closes the editor. There are no root-window hooks or keyboard/pointer
grabs. Render initialization failures release editor capture and pass through;
when rendering itself is unavailable the error is logged rather than drawn.
M5 supplements missing key, pointer and focus event masks on registered
windows, retains application mask changes through `XSelectInput`, and suppresses
extra events the application did not request. It restores original masks when
rendering becomes unavailable and retires metadata on window/display teardown.
XCB, XI2/raw input, other Xlib selection APIs and functions fetched privately
through `dlsym(libX11_handle, ...)` remain unsupported. The latter bypass is
observed in the native vkcube result below.

### Named per-game presets (M5)

```bash
./build/dev/glyphshift --profile my-offline-game -- ./game
./build/dev/glyphshift --profile my-offline-game --preset night -- ./game
```

Without `--profile`, the launcher derives a stable 16-digit ID from the resolved
executable path; moving the executable changes that ID. An explicit profile is
recommended when a common runtime executable launches different games.
`GLYPHSHIFT_PROFILE` also supplies a profile when the option is absent. The
launcher sets it only in its child environment. `--preset` (or
`GLYPHSHIFT_PRESET`) overrides the initial preset for that launch; selection or
saving through the editor updates the remembered selection.

Preset files live under
`$XDG_CONFIG_HOME/glyphshift/profiles/PROFILE/NAME.conf`, falling back to
`$HOME/.config/glyphshift/profiles/PROFILE/NAME.conf` when XDG_CONFIG_HOME is
unset, empty or relative. An `active` file remembers the selected name. Profile
and preset names accept 1–64 ASCII letters, digits, dots, underscores and
hyphens, and cannot start with a dot. No path components are accepted.
The built-in `default` needs no file; saving a preset named `default` overrides
it for that profile.

Insert opens the editor. Choose an existing preset to load and remember it,
or edit the name and press Save preset to save the current settings and
selection. Unsaved widget/hotkey changes remain session-local. Saves use
same-directory temporary files, fsync and atomic rename, with mode 0600;
there is no per-frame preset disk write. Invalid files, invalid selection or
unavailable storage produce a diagnostic and safe defaults without terminating
the application. The optional live `GLYPHSHIFT_CONFIG` file takes precedence
initially and when its contents change; presets never overwrite it.

### Configurable legibility (requested after M5)

The M5 monochrome screenshot was a binary glyph-only verification, with all
scene detail reduced to 100×50 cells at 800×600. It was not a satisfactory
readability default. The user requested better visual quality with configurable
style. Both APIs now expose **Glyph strength** in the editor, config file and
saved per-game presets. The default is 0.45 with source color; cell defaults
remain 8×12. Strength 1 retains pure glyph rendering, including binary mono with the built-in classic atlas.
Strength 0 uses per-pixel source color or grayscale; F10 still bypasses the
entire effect and restores the original image regardless of color mode.
Intermediate strength is an explicit blend of the source pixels and the glyph
pass, preserving fine silhouettes and text at the cost of a less pure ASCII
look. No CPU image readback was added to the injected render loop.

Example presets are provided as live-config files:

```bash
GLYPHSHIFT_CONFIG="$PWD/presets/readable.conf" ./build/dev/glyphshift -- ./game
GLYPHSHIFT_CONFIG="$PWD/presets/mono-readable.conf" ./build/dev/glyphshift -- ./game
GLYPHSHIFT_CONFIG="$PWD/presets/classic.conf" ./build/dev/glyphshift -- ./game
```

`readable` uses source color, 6×10 cells and strength 0.45; `mono-readable`
uses the same dimensions/strength in grayscale; `classic` uses pure monochrome
with 8×12 cells and strength 1. These are starting points, not mandatory styles.
Edit values live or save a named preset through the editor. Older presets
without glyph_strength use the new default; explicitly set 1 to request pure
ASCII. Preset loading/saving validates the strength, including rejection of
NaN/infinity and out-of-range values. Named preset serialization includes it.

The bitmap coverage for `=` and `#` was corrected so the ramp's ink coverage
strictly increases with brightness. Previously `=` had more ink than `+`, and
`*`/`#` had identical coverage. A unit test now guards this ordering.

Same-frame comparisons, rendered by the actual shader on the GPU:
[original](../../docs/m5-supertux-original.png),
[previous binary result](../../docs/m5-supertux-glyph.png),
[readable color](../../docs/quality-supertux-readable.png),
[readable monochrome](../../docs/quality-supertux-mono.png).
These last two are fixed-image shader previews of the recorded game capture,
not new live gameplay captures. To reproduce without launching the game:

```bash
magick docs/m5-supertux-original.png -depth 8 /tmp/glyphshift-input.ppm
./build/dev/glyphshift-preview /tmp/glyphshift-input.ppm \
  presets/readable.conf /tmp/glyphshift-preview.ppm
magick /tmp/glyphshift-preview.ppm /tmp/glyphshift-preview.png
```

`glyphshift-preview` is a developer target built with BUILD_TESTING. It uses the
same embedded GL shader, atlas, ramp and settings parser in an offscreen GLX
pbuffer, without opening a desktop window. It reads/writes minimal P6 RGB PPM
(no header comments; dimensions 1..8192), requires a GPU-accessible X11 display,
and returns an error when unavailable. GPU readback is confined to this tool.
ImageMagick is optional for converting input/output images. It is not a product
runtime dependency.

The legibility changes were verified with `cmake --build --preset dev`,
`ctest --preset dev` (10/10), `cmake --build --preset release` and the Release
CTest command recorded under M5 (12/12, including GLX and Vulkan graphics).
The smoke now edits the actual strength widget via numeric entry at 0, 1 and
0.37, verifies that zero matches the source pixels exactly, that the pure and
fractional outputs differ, and saves/restores the fractional value through a
named preset and process restart. All six resize/input/state/failure checks
remain active. Vulkan uses Khronos core/synchronization validation. Preset
serialization uses locale-independent shortest-round-trip floats; pure tests
also verify precision beyond six decimal places and reject invalid strength.

A source-only copy at `/tmp/glyphshift-quality-clean` configured and built the
Release preset and passed its ten CPU tests without compiler warnings. Its
preview output was byte-identical to the working preview for the readable
preset. `magick compare -metric AE` found zero differing pixels between the
strength-0 color preview and the original PNG. `spirv-val --target-env
vulkan1.0` accepted all eight glyph/editor SPIR-V modules across Debug/Release;
format and Git whitespace checks passed. Mesa llvmpipe also passed the extended
GLX strength/widget/preset smoke with:

```bash
setpriv --inh-caps=-all -- env __GLX_VENDOR_LIBRARY_NAME=mesa \
  LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  python3 tests/smoke/run.py --build build/dev --api glx \
  --output /tmp/glyphshift-quality/mesa-glx
```

One additional authorized offline SuperTux run checked live switching from
readable source color to pure mono (exactly two colors), then back to readable.
It used the Release build with Khronos validation and the same network block:

```bash
setpriv --inh-caps=-all -- env GS_BUILD=release GS_VALIDATE=1 \
  /tmp/glyphshift-offline /usr/bin/python3 /tmp/glyphshift-quality/proton-test.py
```

The game changed to 1920×1022 during that run and refused the subsequent
requested resize; the temporary driver failed that check and ended the test
prefix. This run is **not** counted as a complete resize/clean-exit pass.
Mode/strength switching was observed before that failure, with no Vulkan
validation diagnostics. Live captures/logs are under
`/tmp/glyphshift-quality/proton`; the complete resize/input/preset acceptance
checks are the passing GLX/Vulkan fixtures above. Wine editor/hotkey limitations
from M5 remain; this quality change does not implement a new input backend.

### Character selection and fonts (explicitly requested after M5)

The glyph ramp is now configurable per game and shared by GLX/Vulkan. The
editor's **Characters and font** section provides a set selector, a custom
sequence field, a local font path, density sorting and dark/light reversal.
Edits remain a draft until **Apply characters**, so typing does not rebuild GPU
resources on every keystroke. Use **Save preset** to persist the applied
selection alongside the other effect settings. Selecting a different set keeps
the custom draft text available for later use.

| character_set | Available characters |
| --- | --- |
| classic | Original built-in ` .:-=+*#%@` ramp. |
| ascii | All 95 printable ASCII characters, U+0020..U+007E. |
| alphanumeric | Space, dot, digits, lowercase and uppercase Latin letters. |
| binary | Space, `0`, `1`. |
| blocks | Space and `░▒▓█`. |
| braille | All 256 Braille patterns, U+2800..U+28FF. |
| custom | The supplied UTF-8 sequence, 1..256 Unicode scalar values. |

Example live config, usable in the Proton command from the test guide:

```ini
character_set=custom
characters=" .01ABCé░▒▓█"
glyph_order=density
glyph_reverse=false
glyph_font=""
```

Character selection works with the existing strength/color/cell controls.
`glyph_order=manual` maps the supplied order from dark to light; duplicate
characters are allowed and give those glyphs more bins. `glyph_order=density`
stably sorts by rasterized ink coverage. ASCII, alphanumeric and Braille default
to density ordering unless overridden; other sets default to manual order.
`glyph_reverse=true` reverses the resulting ramp. Setting `characters` without
an explicit character_set selects custom automatically. If both are present,
the explicit character_set wins. Custom sequences are limited to 256 codepoints
and 1024 UTF-8 bytes, including spaces and duplicates.

Quote character sequences to preserve leading/trailing spaces and literal `#`.
Inside quoted strings, `\\` represents a literal backslash and `\"` a literal
quote. A `#` outside quotes starts a comment. Control characters, malformed
UTF-8, surrogates and out-of-range scalars are rejected with a safe classic
fallback. Named presets serialize the selection, text, font path, ordering and
reverse flag, including correct quoting.

With an empty glyph_font, the classic set retains its original bitmap; other
text glyphs use a bold monospace face discovered through Fontconfig (see the
[typographic atlas](#typographic-glyph-atlas-art-direction-phase-1) for the
family preference and baseline placement).
A nonempty glyph_font must be an absolute path to a local Unicode TTF/OTF
font; paths containing spaces or `#` should be quoted. Blocks U+2580..U+259F
and Braille are generated internally, including when used in a custom sequence,
so they do not require font coverage. Unavailable fonts, unsupported bitmap
formats or missing codepoints produce a diagnostic and use the built-in
classic set rather than rendering missing-glyph boxes or terminating the game.
The diagnostic identifies the missing U+ codepoint when applicable.

The atlas uses a 16×16 grid of 16×24 tiles, with a separate 256-entry ramp
texture. The original classic masks are replicated exactly into those tiles.
Both backends update lazily when the applied selection changes. GLX caches the
CPU atlas across context switches; Vulkan retires the old renderer outside
registry locks, waits its queue during this explicit change and creates fresh
resources. Stable frames do not rasterize fonts, discover fonts or upload a
new atlas. Font files are read at initialization/selection changes; replacing
the same font file on disk does not automatically trigger a reload.

This is per-codepoint glyph rendering, not text shaping: ligatures, combining
sequences, emoji sequences, color emoji and complex-script layout are not
implemented. The editor retains its own default UI font; its UTF-8 field may
show fallback boxes for symbols absent from that UI font. The rendered effect
uses the selected glyph font. The Xlib adapter accepts basic Latin and Unicode
keysyms but does not implement IME composition or general clipboard integration;
use a UTF-8 config file for sequences your desktop input path cannot enter.
At the time of this character-selection pass, Wine editor/input limitations
remained unchanged; the subsequent real-application input work is recorded below.
File-based selection remains available.

Ready-to-edit examples:
[ASCII](../../presets/characters-ascii.conf),
[blocks](../../presets/characters-blocks.conf),
[Braille](../../presets/characters-braille.conf),
[custom](../../presets/characters-custom.conf).

Character-selection verification on 2026-10-17 used the existing NVIDIA/X11
setup and Mesa llvmpipe. Both presets built without compiler warnings. Debug
CTest passed 11 tests; Release passed 13, including the extended GLX/Vulkan
smoke suites with Khronos core/synchronization validation. The smoke selects
binary through the actual editor, applies it, saves it and checks the restored
image after process restart. A separate live-file loop switches classic,
ASCII, binary, blocks, Braille, custom Unicode, and reversed order, checks that
the images differ, checks missing-font fallback against the exact classic
image, and restores the classic result. Existing resize, input, state probes,
multi-context/swapchain and pass-through failure checks remain active. Mesa
GLX passed the same extended smoke using the environment recorded above.

The new characters CPU test checks UTF-8 scalar decoding (including a non-BMP
codepoint), malformed/control/overlong input rejection, 256-character bounds,
classic mask preservation, procedural coverage, density sorting/reversal,
complete printable ASCII rasterization, quoted config round trips and missing
font fallback. Preset tests include custom Unicode, quoting, order and reverse
persistence; the editor is compiled consistently with 32-bit Unicode support.

A source-only copy at `/tmp/glyphshift-characters-clean` independently configured
and built Release and passed all 11 CPU tests. The working builds also passed
format/whitespace checks and validation of all eight glyph/editor SPIR-V
modules. Shared editor exports remain limited to GlyphShift APIs. FreeType
2.14.3 and Fontconfig 2.18.3 were available; no packages or fonts were installed.
Actual shader previews of the saved SuperTux capture were inspected:
[ASCII](../../docs/characters-ascii.png), [blocks](../../docs/characters-blocks.png),
[Braille](../../docs/characters-braille.png). These are fixed-image GPU previews,
not additional Proton/gameplay validations. At this stage Wine input, Mesa
Vulkan and broader compatibility limits recorded under M5 remained unchanged.

### Real-application editor input (requested after M5)

The editor now has a dedicated X11 input connection shared by both rendering
backends. It does not depend on intercepting the application's event queue.
This addresses the private-Xlib path used by vkcube and the tested Wine/Proton
path, where rendering targets an internal child rather than the managed game
window. The existing fixture-focused Xlib adapter remains available.

Press **Insert** with the game focused, edit with mouse or keyboard, and use
**Escape**, **Insert** or the panel's close button to return to the game.
**F10** toggles the effect whether the editor is open or closed. **Save preset**
persists edits; external live-config files remain optional and are never
rewritten by the editor. Restart the application through the rebuilt launcher
to load the updated libraries.

The platform adapter lives separately from the API-independent model. It
opens an owned XCB connection using the application's X11 display, finds the
managed client ancestor of the presentation surface, and registers passive
Insert/F10 grabs only on that client. It never registers root-window grabs.
While the editor is open, an input-only child receives keyboard and pointer
input, translates coordinates into the presentation surface, and returns focus
to the previous child on close. There is no active keyboard/pointer grab that
would lock out window-manager shortcuts. Losing focus closes the editor without
taking focus back from another application. Closing retains input until
UI-owned key/button releases arrive, preventing those releases from leaking
into the game. Native FocusOut notifications let applications reset held input. If the window
manager returns focus to the managed parent during a pointer move or resize,
the adapter keeps editor focus within that same client until closing.

The child tracks the client size, supplies a pointer cursor, and is unmapped
when closed. Renderer failure and surface/window retirement release owned
input resources. Checked XCB requests avoid replacing the host's process-wide
Xlib error handler; a conflicting hotkey grab reports one diagnostic and
releases grabs acquired by this adapter. Stable frames reuse the connection
and child, with a bounded event drain. The legacy synthetic-event smoke uses
its existing no-focus fixture mode; a separate focused test exercises the new
path with actual XTest keyboard/pointer events and private-Xlib fixtures.

Run the focused test only in an available X11 desktop session. It temporarily
focuses its test application and moves the pointer, verifies focus before each
input action, and restores focus/pointer afterward when still owned by the
test. It requires libXtst at runtime and accepts the already extracted native
applications/Proton setup described under M5. It installs/downloads nothing:

```bash
python3 tests/smoke/editor_apps.py --build build/dev \
  --application glx-fixture --output /tmp/glyphshift-editor-glx
VK_LAYER_VALIDATE_SYNC=1 python3 tests/smoke/editor_apps.py --build build/dev \
  --application vulkan-fixture --output /tmp/glyphshift-editor-vulkan
python3 tests/smoke/editor_apps.py --build build/dev \
  --application glxgears --output /tmp/glyphshift-editor-glxgears
python3 tests/smoke/editor_apps.py --build build/dev \
  --application vkcube --output /tmp/glyphshift-editor-vkcube
/tmp/glyphshift-offline /usr/bin/python3 tests/smoke/editor_apps.py \
  --build build/dev --application supertux --output /tmp/glyphshift-editor-supertux
```

The last command requires the authorized offline wrapper, extracted game and
Proton prefix from the test guide. Keep the network restriction in place.
For the temporary validation package, supply the recorded VK_LAYER_PATH;
capability-bearing agent environments additionally use setpriv as under M1.
The driver saves captures/logs and checks real numeric editing, character-set selection/application, named
preset saving, Insert/Escape/F10, three resizes with the editor open, focus loss,
reopening and the title-bar close button. Fixtures additionally verify that UI
input is suppressed and ordinary keyboard/pointer input returns afterward.
These focused checks are opt-in rather than part of unattended CTest.

Verified on 2026-10-17 in the existing Arch/X11/i3/NVIDIA environment. Debug
and Release built successfully; Debug passed all 11 CPU tests. An independent
source-only Release build at `/tmp/glyphshift-editor-clean` also passed 11 CPU
tests without compiler warnings. Formatting, Git whitespace, documentation
links/shell syntax, editor exports and all eight generated glyph/editor SPIR-V
modules were checked. No packages, Steam installation or downloads were added;
libxcb 1.17.0 and xcb-keysyms 0.4.1 were already available.

The focused Debug matrix passed both fixtures, glxgears, vkcube and offline
SuperTux. The final focused Release matrix additionally selected/applied the
binary character set through the editor and verified its saved preset along
with a typed cell width of 21. Both private-Xlib fixtures, glxgears, vkcube,
SuperTux's menu and its prepared level introduction passed. In that final
matrix **all legacy Xlib event filters were disabled by the test mode**, even
if a graphics driver called them internally. The new input adapter therefore
supplied the actual mouse/keyboard interactions itself. No GL debug or Vulkan
core/synchronization validation errors were observed, including clean exit.

A further Release level run closed the editor, sent Enter to the game, verified
that the introduction advanced into the colored world, and reopened the panel
using a live readable config (source color, 6×10, strength 0.35). This confirms
keyboard input returning to the tested Wine application, beyond the fixture's
strict keyboard/button event log checks. The [live level capture](../../docs/editor-supertux-play.png)
was inspected directly; it is an actual application capture, not a fixed-image
shader preview. This does not measure gameplay smoothness or aesthetic quality
throughout a play session.

Verification commands included:

```bash
cmake --build --preset dev
ctest --preset dev
cmake --build --preset release
setpriv --inh-caps=-all -- env \
  VK_LAYER_PATH=/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d \
  VK_LAYER_VALIDATE_SYNC=1 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
  /tmp/glyphshift-offline /usr/bin/python3 tests/smoke/editor_apps.py \
  --build build/release --application supertux --level \
  --output /tmp/glyphshift-editor-apps/play-supertux
```

The complete Release CTest suite passed all 13 tests, including both legacy
no-focus GLX/Vulkan graphics smokes (57.09 seconds total), with core and
synchronization validation enabled. Mesa llvmpipe also passed the focused GLX
editor test with legacy filters disabled, including character selection,
keyboard/pointer suppression and restoration, preset saving and focus changes:

```bash
setpriv --inh-caps=-all -- env \
  VK_LAYER_PATH=/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d \
  VK_LAYER_VALIDATE_SYNC=1 ctest --preset release
setpriv --inh-caps=-all -- env __GLX_VENDOR_LIBRARY_NAME=mesa \
  LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  python3 tests/smoke/editor_apps.py --build build/release \
  --application glx-fixture --output /tmp/glyphshift-editor-apps/mesa-focused
```

Other focused Release outputs are under `/tmp/glyphshift-editor-apps/strict-*`;
the corresponding matrix log is `strict-matrix.log`. Temporary evidence can
be removed by a reboot; the linked level capture is committed in this repository.

This remains an X11 windowed/borderless integration. Native Wayland, exclusive
fullscreen, application-owned active input grabs, general XI2/raw-input
suppression, IME and arbitrary games are not established by this change.

### Illustrated rendering (requested after M5)

The user requested a glyph drawing with recognizable characters and directional
contours, referencing [AcerolaFX's ASCII shader](https://github.com/GarrettGunnell/AcerolaFX/blob/main/Shaders/AcerolaFX_ASCII.fx).
This is a new implementation of that general direction, using the final color
image only. No AcerolaFX code or textures were copied. Scene depth/normals,
ReShade/GShade integration and new platform backends are not added.

Existing configs retain `render_style=classic` by default. To use the new mode,
open **Insert → Illustration → Illustrated rendering**, or launch with an
example live config:

```bash
GLYPHSHIFT_CONFIG="$PWD/presets/illustrated.conf" ./build/dev/glyphshift -- ./game
GLYPHSHIFT_CONFIG="$PWD/presets/illustrated-blocks.conf" ./build/dev/glyphshift -- ./game
GLYPHSHIFT_CONFIG="$PWD/presets/illustrated-ink.conf" ./build/dev/glyphshift -- ./game
```

The first renders bright colored glyphs and solid highlights with adaptive
light/dark contours.
The second mixes text and a solid block with a manual fill ramp. The third
uses a reversed manual ramp with extra spaces for dark ink on warm paper.
All use strength 1 so the complete drawing comes from glyphs. They are editable starting points,
not a promise that one style suits every game's textures or HUD.
`presets/readable.conf` is not changed by this feature.

**Illustration** exposes the render mode, independent fill toggle, exposure,
contrast, gamma, contour strength/threshold, contour compositing, ink/paper
colors, and four custom contour characters. Edits to the four characters require
**Apply contours** (now **Apply contour / shape glyphs**);
other controls update immediately. **Save preset** persists all fields. The
existing **Characters and font** controls select the fill set and its ordering.
Contours have separate atlas slots and do not reduce the 256-character fill
limit. The new controls follow Reset to defaults, leaving existing widget
positions above it intact; scroll the panel when necessary.

```ini
render_style=illustrated
cell_width=8
cell_height=12
glyph_strength=1
tone_exposure=1.15
tone_contrast=1.2
tone_gamma=1
edge_strength=1
edge_threshold=0.18
fill_enabled=true
contour_overlay=true
foreground=0.92,0.97,1
background=0.015,0.025,0.045
# Exactly four Unicode scalars, ordered vertical, horizontal, /, backslash.
edge_characters="|-/\\"
```

Exposure/contrast/gamma accept finite 0.25..4 (defaults 1). Contour strength and
threshold accept 0..1 (defaults 1 and 0.18); strength 0 disables contours.
Colors are three comma-separated finite RGB components in 0..1, defaulting to
white ink and black paper. `fill_enabled=false` produces contour-only output;
with contours also disabled, the result is the paper color. Four spaces can
intentionally suppress the contour glyphs. Invalid fields warn and fall back
to bounded defaults. Preset saves reject invalid fields and serialized data
larger than the existing 4096-byte limit.

*Phase 2 replaces this sampling with a per-cell analysis pass: region means
and a voted 5×5 contour lattice; see
[two-pass glyph analysis](#two-pass-glyph-analysis-art-direction-phase-2).*
The illustrated pass samples a weighted 3×3 neighborhood at the scale of each
cell. A Sobel gradient classifies vertical, horizontal and two diagonal
contours. A magnitude threshold and a directional-coherence test reject weak
or symmetric fine texture. Cell-boundary samples include adjacent pixels, so
edges aligned with the character grid are still detected. By default, contour
strokes overlay the luminance fill instead of erasing an entire cell. Each cell chooses whichever ink/paper color contrasts more with
its mean filled color, keeping dark strokes visible on bright blocks and vice
versa. Set `contour_overlay=false` for contour-replaces-fill behavior with a
fixed foreground stroke. Default `|`, `-`, `/`, backslash
contours are built-in full-cell strokes; other contour characters use the
selected local font or procedural block/Braille support. Unavailable glyphs
use the same diagnosed safe classic fallback as other character selections.

Exposure and contrast transform luminance, followed by a power of 1/gamma.
Fill selection with density ordering uses a 256-bin lookup calibrated to
measured glyph coverage, including reversed order; manual ordering retains its
supplied bins. Source color is normalized by its strongest RGB component to
supply bright ink without multiplying source darkness into sparse glyphs
again. Foreground colors monochrome fill; foreground/paper form the adaptive
contour palette, and background supplies paper. Glyph strength retains its
existing source/gray blend behavior,
including exact source pixels at strength 0 in source mode. F10 still bypasses
the complete effect.

The atlas upload is now a 272×425 R32F summed-area texture containing 260
independent 17×25 tiles, including zero borders around 16×24 glyph masks.
Bilinear interpolation of integral values integrates each output pixel's exact
rectangular footprint in the mask, avoiding neighboring-glyph bleeding and
minification holes. The legacy mode samples the original nearest texel through
the same representation; its glyph masks and mapping remain unchanged. CPU
rasterization/integral construction happens at initialization or character/font
changes, never per frame. The ramp texture has two 256-byte rows for legacy
and calibrated mappings. Both backends use the same fragment source and 64
bytes of shared art parameters; Vulkan's full push constants are 96 bytes,
within Vulkan 1.0's guaranteed minimum. Tone, color and style edits do not
rebuild atlas/GPU resources. No new intermediate framebuffer, queue/device
idle per frame, or CPU framebuffer readback was added to the injected path.

Small cells still have a physical information limit: 4×4 cannot preserve the
identity of arbitrary letters. Filtering preserves their ink coverage instead
of dropping strokes, and the panel suggests 8×12 or larger for readable text.
Contour selection remains spatial; temporal hysteresis is not implemented by
this change. HUD/text can still lose detail in a full glyph drawing; reducing
strength remains a configurable compromise.

#### Illustrated renderer verification and comparisons

Verified on 2026-10-17 on the existing Arch/X11/i3/NVIDIA RTX 3060 Ti setup.
Debug passed 12 CPU tests. Release passed all 14 tests, including the expanded
GLX/Vulkan graphics smokes with Khronos core/synchronization validation
(69.50 seconds total). A source-only Release copy under
`/tmp/glyphshift-illustration-clean` built without warnings and passed 12 CPU
tests. Formatting, whitespace, Python syntax, all eight glyph/editor SPIR-V
modules and documentation examples/links were checked. No dependencies were
installed or downloaded for this change.

The new CPU test checks tone response, four contour orientations, symmetric
texture rejection, configuration validation/round trips, reconstruction of all
260 glyph masks from their summed areas, area conservation from 2×2 through
128×128 and monotonic calibrated density bins. Preset tests additionally save
and reload nondefault illustration fields, including custom palettes and
contour compositing. GPU smoke checks new mode/old mode restoration, contour
strength, replacement/overlay, fill separation, custom Unicode contour glyphs,
tone, background colors, small cells and exact source output at strength zero.
The analytic GPU oracle passed 14 independent half/quadrant-block scaling and
color-composition cases at error <=1/255, plus two cases proving adaptive
light/dark contour strokes preserve the surrounding solid fill. Comparison of
ten same-scene GLX/Vulkan outputs had maximum normalized RMSE 0.0000114
(tolerance 0.003); most cases matched exactly.

Focused Release tests activated illustration through the actual panel on both
fixtures and on offline SuperTux/GE-Proton/DXVK, saved the selected mode and
binary fill, and passed keyboard/mouse, focus, three open-panel resizes and
clean exit. The level test confirmed Enter returning to Wine after closing the
editor, then captured the final illustrated preset and reopened panel over the
running world. Vulkan validation reported no errors, including teardown.
The additional SRGB focused test passed after the driver added control-state
captures and more pacing between the new mode checkbox and header collapse;
the first missed-selection attempt is not counted as successful verification.

Mesa llvmpipe passed the focused illustrated GLX window workflow, including
actual UI selection, preset saving, resize and input restoration. The separate
Mesa offscreen oracle could not run: GLX pbuffer context creation failed with
`failed to create drawable` / `Cannot create offscreen GLX context` on this
NVIDIA X server. Neither Xvfb nor Xephyr is installed for an isolated retry.
Thus the analytic oracle is verified on NVIDIA, not Mesa; the windowed Mesa
shader/input checks passed. Mesa Vulkan and other hardware remain outside this
change's verified matrix.

Main verification commands (the temporary validation/offline prerequisites
are the same ones described above):

```bash
cmake --build --preset dev
ctest --preset dev
cmake --build --preset release
setpriv --inh-caps=-all -- env \
  VK_LAYER_PATH=/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d \
  VK_LAYER_VALIDATE_SYNC=1 ctest --preset release
setpriv --inh-caps=-all -- env \
  VK_LAYER_PATH=/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d \
  VK_LAYER_VALIDATE_SYNC=1 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
  /tmp/glyphshift-offline /usr/bin/python3 tests/smoke/editor_apps.py \
  --build build/release --application supertux --level --illustrated \
  --output /tmp/glyphshift-illustration/final-supertux
setpriv --inh-caps=-all -- env __GLX_VENDOR_LIBRARY_NAME=mesa \
  LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  python3 tests/smoke/editor_apps.py --build build/release \
  --application glx-fixture --illustrated --output /tmp/glyphshift-illustration/mesa-editor
```

Logs/captures are under `/tmp/glyphshift-illustration`; temporary evidence may
disappear after reboot. Durable comparisons below use exactly the same original
live SuperTux frame as input to the actual GPU preview shader. The styled
comparisons are fixed-frame previews, separate from the live integration test.
Open them at their native resolution to judge individual glyphs.

| Comparison | Image |
| --- | --- |
| Original live frame, effect off | [Original](../../docs/illustration-original.png) |
| Classic renderer, full ASCII, 8×12, strength 1 | [Previous mapping](../../docs/illustration-classic.png) |
| Illustrated text + weighted solid highlights, 8×12 | [Color](../../docs/illustration-color.png) |
| Coarser text + blocks, 10×14 | [Blocks](../../docs/illustration-blocks.png) |
| Reversed text and blank paper, 8×12 | [Ink](../../docs/illustration-ink.png) |
| Actual final Vulkan/Proton panel over the running level | [Live editor](../../docs/illustration-live-editor.png) |

These checks do not establish temporal stability, a gameplay performance
benchmark or universal artistic quality. The provided styles are configurable;
no later Linux-support milestone was started.

### Automated graphics smoke tests (M5)

From an accessible X11 session with the Khronos validation layer available:

```bash
cmake --build --preset dev --target smoke
# Or run one API:
cmake --build --preset dev --target smoke-glx
cmake --build --preset dev --target smoke-vulkan

# Include the graphics checks in CTest (off by default):
cmake --preset release -DGLYPHSHIFT_GPU_SMOKE_TESTS=ON
cmake --build --preset release
ctest --preset release
```

The Python drivers use standard-library ctypes and Xlib, without image-tool or
Python-package dependencies. Captures require a visible, unobscured 32-bit
little-endian RGB X11 visual and a window manager that accepts floating window
resizes. Optional i3 integration moves only the test window to the current
workspace, without requesting keyboard focus. Tests send input directly to
the fixture rather than using desktop input grabs. Missing DISPLAY or an
inaccessible display returns skip code 77; graphics/validation failures fail
the test rather than being reported as successful coverage.

Each API checks launch, transformed/original images, editor capture and host
input restoration, real UI preset saving and process restart, six confirmed
resizes and clean exit. GLX additionally checks procedure-address swaps, two
contexts and a test-only shader-compilation failure that must preserve the
original image with one diagnostic. Vulkan checks two simultaneous swapchains
on one device and unsupported incremental-present pNext forwarding unchanged,
with Khronos core/synchronization validation. Fixture event selection is
intentionally press-only for GLX and release-only for Vulkan to exercise missing
input masks. Logs and PPM captures are saved in `build/PRESET/smoke-evidence`.
The aggregate smoke target runs GLX and Vulkan sequentially. CTest marks both
RUN_SERIAL to avoid capture conflicts.

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

### Verified M2 results

Verified on 2026-10-17 using the Arch Linux/X11/i3/NVIDIA RTX 3060 Ti setup and
the temporary Khronos validation package recorded for M0. M0/M1 and developer
tooling were committed as `9959ae9` before starting M2.

Commands executed for M2 include:

```bash
cmake --build --preset dev
ctest --preset dev
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --build --preset dev --target format-check
git diff --check
nm -D --defined-only build/dev/libglyphshift_vk.so
ldd build/dev/libglyphshift_vk.so
```

Both presets built without compiler warnings and passed seven CTest tests.
The new GPU-independent layer test loads the actual `.so` into a synthetic
loader chain: negotiation (including rejection of interface 1), missing link
info, layer/extension enumeration, two simultaneous instances/devices,
physical-device groups and aliases, queue retrieval, swapchain and shared
swapchain lifecycle, partial/full image discovery, unsupported/unknown
commands (including safe physical-command classification when the next layer
lacks a physical query hook), and cleanup were checked. It injects real C++ allocation failures
to verify mandatory-state rollback and optional-metadata pass-through. It also
checks exact create/present pointers and allocation callbacks, downstream error
and per-swapchain results, 16,002 forwarded presents across concurrent queues,
no allocation during presentation, and safe downstream reentry into procedure
lookup. Launcher tests now cover preserving Vulkan discovery overrides and
requested layers, plus failure when the Vulkan library or manifest is missing.
Symbol inspection showed only the negotiation function exported; `ldd` showed
no `libvulkan` dependency. The LSP database now contains 13 translation units.

A temporary driver at `/tmp/glyphshift-m2-vulkan-check.py` was executed using:

```bash
setpriv --inh-caps=-all -- python3 /tmp/glyphshift-m2-vulkan-check.py
```

It compared direct versus launched Vulkan fixtures in Debug and Release at
`--fixed-time 1.25 --frames 1800`, with `--validation` and synchronization
validation enabled. Each execution confirmed 12 real window resizes through
the six M0 test dimensions repeated twice, then exited zero on
`WM_DELETE_WINDOW`. For each launched execution the layer logged 16 matched
swapchain creations/destructions, one instance/device lifecycle, and one
forwarded-present diagnostic. Every corresponding direct/launched image
matched exactly (`magick compare -metric RMSE`: `0 (0)` at all 12 sizes in
each preset). No validation warnings/errors were introduced by GlyphShift.
The loader's two general configuration messages about `VK_INSTANCE_LAYERS`
were expected and reported separately, not suppressed.

The GLX regression passed the same direct/launched image and resize checks in
both presets, through `/tmp/glyphshift-m2-glx-check.py`. Dropping inherited
capabilities is needed only for this external test environment, as explained
under M1. Games, Proton, other GPUs/drivers, old loader interfaces, and rendering
features from M3 onward are not claimed by these fixture tests.

A source-only copy at `/tmp/glyphshift-m2-final-clean` configured with
`cmake --preset dev`, rebuilt from scratch with
`cmake --build --preset dev --clean-first`, and passed all seven tests with
`ctest --preset dev`. Its launcher ran the Vulkan fixture for 60 frames with
Khronos/synchronization validation and exited zero. The relative manifest
library path and a newly generated LSP database were also checked in the clean
source setup. Both working-tree presets separately exited zero after 120
Vulkan frames with validation enabled.

### Verified M3 results

Verified on 2026-10-17 in the Arch/X11/i3/NVIDIA RTX 3060 Ti environment
recorded above. M2 was committed as `96f3503` before the separately requested
M3. Shared glyph semantics and the initial GLX pass were committed during M3
as `4bad030` and `040e4b9`; the latter corrects the dispatch-function list.

Commands executed include:

```bash
cmake --build --preset dev
ctest --preset dev
cmake --build --preset release
ctest --preset release
python3 tools/format.py --apply
python3 tools/format.py --check
cmake --build --preset dev --target format-check
git diff --check
spirv-val --target-env vulkan1.0 build/dev/generated/glyph.vert.spv
spirv-val --target-env vulkan1.0 build/dev/generated/glyph.frag.spv
spirv-val --target-env vulkan1.0 build/release/generated/glyph.vert.spv
spirv-val --target-env vulkan1.0 build/release/generated/glyph.frag.spv
nm -D --defined-only build/dev/libglyphshift_gl.so
nm -D --defined-only build/dev/libglyphshift_vk.so
ldd build/dev/libglyphshift_gl.so
ldd build/dev/libglyphshift_vk.so
```

Both presets passed all eight CTest tests without compiler warnings. New pure
logic checks cover atlas pixels, luminance/ramp boundaries, reordered ramp
data, invalid/nonfinite inputs, validated settings and live-file replacement.
Existing launcher, staged-format-hook, GLX forwarding and synthetic Vulkan
layer tests continue to pass. The root LSP database contains 19 translation
units and continues to point to Debug. Neither injected library links to GL
or the Vulkan loader; exported entry points are restricted to the four GLX
hooks and Vulkan negotiation, respectively. Both generated glyph SPIR-V
modules passed validation in each preset.

A temporary X11 driver was executed with:

```bash
setpriv --inh-caps=-all -- python3 /tmp/glyphshift-m3-matrix.py
M3_API=vulkan M3_INJECTED_ONLY=1 setpriv --inh-caps=-all -- \
  python3 /tmp/glyphshift-m3-matrix.py release
```

It uses `--fixed-time 1.25`, compares direct and injected fixtures, and confirms
12 actual resizes through 320×240, 1024×768, 640×480, 1000×300, 300×800 and
800×600, repeated. At every size it atomically replaces the settings file to
select off, source 8×12, monochrome 8×12, source 13×19, and source 8×12 again.
Disabled images match the direct original exactly; re-enabled images match
the previous source image exactly. Monochrome captures contain only black
and white. Source-color and changed-cell images were also inspected visually.
The injected GLX runs enable both the independent hostile-state snapshot and
KHR_debug callback. Vulkan runs enable Khronos core/synchronization validation
using the temporary package recorded under M0. Captures require a visible,
unobscured fixture; an initial obscured-window capture was rejected and the
driver was corrected to activate/raise the fixture before captures and keep
tall windows inside the screen. The final Vulkan Release round was repeated
after ImageMagick capture failures; its complete 12-resize run then passed.
Both presets passed all 48 cross-API mode/size comparisons with maximum
normalized RMSE `0.000146148` (threshold `0.003`). All runs exited zero on
`WM_DELETE_WINDOW`, with no GL debug or Vulkan validation warnings/errors,
including teardown. The loader's expected layer-enablement announcements
remained visible. Results are under `/tmp/glyphshift-m3-results`.

A source-only copy at `/tmp/glyphshift-m3-clean`, excluding existing builds,
Git/agent metadata and the root database, independently configured and built
both presets with `cmake --preset dev` / `release` and their build commands.
Both passed all eight tests and generated their own compilation database.
Its final Debug GLX launcher also passed 60 frames with both state checking
and KHR_debug enabled; its Release Vulkan launcher passed 60 frames with
Khronos/synchronization validation enabled:

```bash
setpriv --inh-caps=-all -- env GLYPHSHIFT_FIXTURE_GL_DEBUG=1 \
  GLYPHSHIFT_FIXTURE_STATE_CHECK=1 /tmp/glyphshift-m3-clean/build/dev/glyphshift \
  -- /tmp/glyphshift-m3-clean/build/dev/glyphshift-glx-fixture --frames 60
setpriv --inh-caps=-all -- env \
  VK_LAYER_PATH=/tmp/glyphshift-m0-validation/root/usr/share/vulkan/explicit_layer.d \
  VK_LAYER_VALIDATE_SYNC=1 /tmp/glyphshift-m3-clean/build/release/glyphshift \
  -- /tmp/glyphshift-m3-clean/build/release/glyphshift-vulkan-fixture \
  --validation --frames 60
setpriv --inh-caps=-all -- python3 /tmp/glyphshift-m3-srgb.py
```

The last temporary driver uses the clean Debug build and
`GLYPHSHIFT_FIXTURE_SRGB=1`, an opt-in fixture format selection. Its source,
mono, 13×19 and disabled live captures passed with clean exit after 101 frames
and no validation warnings/errors. The source capture had 4,974 colors;
monochrome contained exactly two. The source image was visually inspected. The default
fixture continues to select UNORM.

These are fixture results on the recorded GPU. Games, Proton, Mesa, other GPUs,
Wayland and broader multi-context/swapchain cases were not verified. The M4
editor/hotkeys and M5 integrations were not implemented.

### Verified M4 results

Verified on 2026-10-17 with the Arch/X11/i3/NVIDIA RTX 3060 Ti setup above;
the resumed session reported kernel `7.2.4-arch1-2`, NVIDIA `615.71.09`,
OpenGL `3.3.0`, and Vulkan device API `1.4.351`. M3 was committed as `2450123`
before this separately requested M4. No M5 work was started.

The working checkout and a source-only copy at `/tmp/glyphshift-m4-clean`
(excluding Git, build artifacts, the compilation database and agent metadata)
configured and built both presets and passed all nine CTest tests without
project compiler warnings:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
cmake --preset release
cmake --build --preset release
ctest --preset release
python3 tools/format.py --apply
python3 tools/format.py --check
cmake --build --preset dev --target format-check
git diff --check
```

The new editor unit test checks real ImGui geometry generation, session
isolation, hotkey repeat suppression (including traditional X11 autorepeat),
UI capture and release after closing, forwarding releases for host-held keys,
focus loss, window retirement, and repeated frames. Existing staged-format,
launcher, GLX interposition, synthetic Vulkan dispatch, glyph and fixture tests
remain passing. The launcher test also checks the missing shared editor
library diagnostic. Both source roots generated their own Debug compilation
database with 26 translation units; Release left the Debug link intact.

Temporary X11 drivers exercised GLX and Vulkan in Debug and Release at
`--fixed-time 1.25`. Each API/preset passed real widget changes to width,
height and monochrome, reset, the effect checkbox, Insert/F10/Escape, and 12
confirmed resizes through the six M0 dimensions repeated twice. Pointer and
keyboard edits did not reach the host fixture. After closing, an `a` press/
release and mouse click reached it; Escape closed the editor first, then a
second Escape reached the fixture and exited zero. Host-input logging showed
only those expected events. Disabled images matched direct fixture originals
exactly, and closing/reopening the editor preserved the scene exactly. GLX
runs enabled both the hostile-state probe and KHR_debug; Vulkan enabled
Khronos core/synchronization validation. Final runs emitted no GL errors or
Vulkan validation warnings/errors, including teardown. The GL callback now
omits informational notification-severity messages about buffer placement;
other debug severities remain visible.

After the session interruption removed earlier `/tmp` artifacts, the remaining
Vulkan Release round was completed with a recreated verification driver:

```bash
setpriv --inh-caps=-all -- python3 /tmp/glyphshift-m4-final.py release vulkan
```

It independently captured a direct original, repeated all widget/input/resize
checks, and additionally entered `21` into the width slider via Ctrl-click and
keyboard. The result matched a live config containing `cell_width=21` exactly.
The two libraries' scene output and editor screenshots were inspected. This
driver keeps the test window on-screen and retries transient X11 captures.
Earlier capture-tool failures and an interrupted desktop-input run were
repeated; they are not counted as successful checks.

The resumed validation setup downloaded the same official Arch
`vulkan-validation-layers-1.4.363.0-1` package from
`archive.archlinux.org`, extracted it under `/tmp/glyphshift-m4-validation`,
and pointed its manifest to the extracted library. No system packages were
installed. The driver supplies `VK_LAYER_PATH` and `VK_LAYER_VALIDATE_SYNC=1`.
Expected loader announcements remain visible. Capability dropping is only for
the external test environment, as explained under M1.

The clean Debug build passed the real widget/input checks again for GLX and
Vulkan SRGB, including numeric entry and exact original/off comparisons:

```bash
GS_VERIFY_ROOT=/tmp/glyphshift-m4-clean GS_VERIFY_QUICK=1 \
  setpriv --inh-caps=-all -- python3 /tmp/glyphshift-m4-final.py dev glx
GS_VERIFY_ROOT=/tmp/glyphshift-m4-clean GS_VERIFY_QUICK=1 GS_VERIFY_SRGB=1 \
  setpriv --inh-caps=-all -- python3 /tmp/glyphshift-m4-final.py dev vulkan
```

Quick runs skip the already completed resize matrix. These temporary drivers
use `GLYPHSHIFT_FIXTURE_NO_FOCUS=1` (an X11 input hint) and
`GLYPHSHIFT_FIXTURE_INPUT_LOG=1`, sending events only to the fixture without
taking desktop keyboard focus. Default fixture behavior is unchanged. Current
captures/logs are under `/tmp/glyphshift-m4-final` and may be removed by a reboot.

`spirv-val --target-env vulkan1.0` accepted both editor shader modules in both
presets. `nm -D --defined-only build/release/libglyphshift_editor.so` showed
only the intended GlyphShift APIs, with no ImGui exports. `readelf -d` on
both renderer libraries confirmed `$ORIGIN` lookup for the shared editor and
no GL/Vulkan loader link dependency. Games, Proton, other GPUs/drivers,
XCB/XI2/raw input and broader multi-context/swapchain scenarios remain
unverified. M4 does not claim the complete MVP or implement M5 presets/smoke
integration work.

### M5 implementation and verification status

Verified on 2026-10-17 in the existing Arch/X11/i3 environment, after M4 commit
`b86a575`. The NVIDIA GPU is RTX 3060 Ti, driver 615.71.09. No system packages,
Steam installation, system-wide injection or later milestone were added.

The checkout configured/built Debug and Release; Debug passed all ten CPU
CTest tests. Release enabled graphics CTests and passed all twelve tests,
including both smoke suites with Khronos core/synchronization validation:

```bash
cmake --build --preset dev
ctest --preset dev
cmake --preset release -DGLYPHSHIFT_GPU_SMOKE_TESTS=ON
cmake --build --preset release
setpriv --inh-caps=-all -- env \
  VK_LAYER_PATH=/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d \
  VK_LAYER_VALIDATE_SYNC=1 ctest --preset release
```

Capability dropping and execution outside the agent sandbox are specific to
this verification environment, as described under M1. They are not product
requirements. A source-only copy at `/tmp/glyphshift-m5-clean`, excluding builds,
Git/agent metadata and the root database, separately configured/built both
presets and passed ten CPU tests per preset. Preset unit tests include a forked
reader after saving, per-game isolation, malformed/oversized content, invalid
names, symlink rejection and atomic replacement. The pre-commit and existing
launcher/interposition/dispatch/editor/semantics tests remain passing.
Both clean builds emitted no compiler warnings. `python3 tools/format.py
--check`, `cmake --build --preset dev --target format-check` and
`git diff --check` passed. `spirv-val --target-env vulkan1.0` accepted all
twelve generated SPIR-V modules across the two presets. `nm -D --defined-only`
confirmed the intended GLX/Xlib exports and Vulkan negotiation-only export;
`readelf -d` confirmed `$ORIGIN` editor lookup without GL/Vulkan loader linkage.
The root LSP database link still targets Debug and now contains 29 translation
units; Release configuration preserved it.

| Environment/application | Observed result |
| --- | --- |
| NVIDIA, GLX and Vulkan fixtures | Both Release smoke suites passed: UI/input, saved preset restart, six resizes, simultaneous contexts/swapchains and safe failure forwarding. No GL debug or Vulkan validation errors. |
| Mesa 26.2.2, llvmpipe (LLVM 22.1.8), GLX | Full Debug GLX smoke passed with OpenGL 4.6, including state probes and shader-failure pass-through. Software rendering, not an AMD/Intel hardware claim. |
| Mesa 26.2.2, lavapipe, Vulkan | Attempted; direct fixture without GlyphShift fails with X11 BadDrawable on this NVIDIA X server. Also fails with software WSI requested. Layer coverage on Mesa Vulkan is not established. |
| Native glxgears, mesa-utils 9.0.0-7, NVIDIA OpenGL 4.6 | Glyph image, editor open/close, repeated F10, three resizes and clean Escape exit passed. This exposed and fixed missing KeyRelease selection. |
| Native vkcube, vulkan-tools 1.4.363.0-1, NVIDIA Vulkan | Glyph image, live config disable/re-enable, three resizes and clean WM_DELETE_WINDOW exit passed with validation enabled, without validation errors. Editor hotkeys did not work: this binary privately resolves Xlib symbols and bypasses the input hooks. Full vkcube/editor compatibility is not claimed. |
| Offline SuperTux 0.7.0 Windows64, GE-Proton11-9, DXVK, NVIDIA | Debug menu and Release level-introduction tests passed: monochrome glyphs, live config disable/re-enable, three resizes and clean window-close exit. Release loaded Khronos validation with synchronization checks and reported no errors. Insert sent to the game did not open the editor; Proton hotkeys/editor interaction is not established. |

Mesa checks executed with:

```bash
setpriv --inh-caps=-all -- env __GLX_VENDOR_LIBRARY_NAME=mesa \
  LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  python3 tests/smoke/run.py --build build/dev --api glx \
  --output /tmp/glyphshift-m5-mesa-glx
setpriv --inh-caps=-all -- env \
  LD_LIBRARY_PATH=/tmp/glyphshift-m5-apps/root/usr/lib \
  VK_DRIVER_FILES=/tmp/glyphshift-m5-apps/root/usr/share/vulkan/icd.d/lvp_icd.json \
  VK_LAYER_PATH=/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d \
  python3 tests/smoke/run.py --build build/dev --api vulkan \
  --output /tmp/glyphshift-m5-mesa-vulkan
```

The Vulkan attempt failed before injection in its direct-reference phase.
A direct `glyphshift-vulkan-fixture --validation --frames 30` with the same Mesa
paths plus `LIBGL_DRI3_DISABLE=1 MESA_VK_WSI_DEBUG=sw` reproduced BadDrawable.
Temporary Arch packages were extracted into /tmp, including matching
`vulkan-swrast 26.2.2-1` and `libdisplay-info 0.3.0-1`; nothing was installed.
An initial lavapipe 26.2.4 package needed unavailable LLVM 23 and was replaced
with the matching version before testing.

The temporary native application driver was executed as:

```bash
setpriv --inh-caps=-all -- python3 /tmp/glyphshift-m5-native.py
NATIVE_API=vulkan setpriv --inh-caps=-all -- python3 /tmp/glyphshift-m5-native.py
```

It launches the native binaries from `/tmp/glyphshift-m5-apps/root/usr/bin`
through the Debug launcher, using explicit `native-glxgears`/`native-vkcube`
profiles and a monochrome live config. Two-color glyph captures become
multicolor when disabled; resizing returns to two-color glyph output. Evidence
is under `/tmp/glyphshift-m5-native`. Initial vkcube hotkey attempts failed and
are recorded as a limitation, not a successful editor check. Downloaded older
vkcube packages were inspected but were not counted as application validations.
Temporary logs/captures are not durable and may disappear after reboot.

The user explicitly authorized the portable SuperTux/GE-Proton execution after
its initial automatic approval rejection. All runs used
`/tmp/glyphshift-offline`, a temporary libseccomp wrapper denying creation of
AF_INET/AF_INET6 sockets with ENETUNREACH, inherited by Proton and its children.
X11/Unix sockets remained available. The prefix was isolated under
`/tmp/glyphshift-m5-proton-prefix`, with an empty Steam client directory; Steam
was neither installed nor running. SuperTux config disabled networking,
release checks, Discord integration and audio. No anti-cheat or online game
service was involved. The wrapper is a network restriction, not filesystem
isolation. This execution was specifically authorized for the offline test.

The first game launches preserved the original image because DXVK uses present
fences/mode/timing/ID2 pNext metadata, nonzero metadata/mutable-format creation
flags, and a device with more than one queue family. Those attempts were not
counted as successful transformation tests. The layer now preserves supported
metadata unchanged and submits on the graphics presentation queue, which must
already own an exclusive image under the Vulkan contract. Protected flags,
unknown metadata, incremental-present regions and device-group cases still
fail safely. Unit coverage checks the accepted chain, unknown tails, bounded
cycle handling, metadata flags and rejection of protected/device-group flags.
The unsupported-present diagnostic now includes the chain's structure types
once per device; swapchain logging includes format/usage/flags when enabled.

The final temporary game driver was executed with:

```bash
setpriv --inh-caps=-all -- /tmp/glyphshift-offline /usr/bin/python3 \
  /tmp/glyphshift-m5-proton-test.py
setpriv --inh-caps=-all -- env GS_BUILD=release GS_LEVEL=1 GS_VALIDATE=1 \
  GS_EVIDENCE=/tmp/glyphshift-m5-proton-level /tmp/glyphshift-offline \
  /usr/bin/python3 /tmp/glyphshift-m5-proton-test.py
```

It calls `build/PRESET/glyphshift --profile proton-supertux -- /usr/bin/python3
/tmp/glyphshift-m5-apps/GE-Proton11-9-x86_64/proton run
/tmp/glyphshift-m5-apps/supertux/SuperTux-portable/bin/supertux2.exe`, supplying
`STEAM_COMPAT_DATA_PATH`, `STEAM_COMPAT_CLIENT_INSTALL_PATH`, `SteamGameId=0`,
`PROTONFIXES_DISABLE=1`, and a test-only XDG config/live settings file. The game
uses `--window --geometry 800x600 --disable-sound --disable-music` and a temporary
`--userdir`. Release additionally supplies
`VK_LAYER_PATH=/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d`,
`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` and `VK_LAYER_VALIDATE_SYNC=1`.
A read of the test process's library mappings confirmed both the Release layer
and `libVkLayer_khronos_validation.so` were loaded.

The phase check uses an unmodified copy of `levels/world1/welcome_antarctica.stl`
as `data/glyphshift-m5-test.stl`, launched by that simple filename. SuperTux's
Windows CLI converted paths with separators into paths rejected by its own
virtual filesystem; those failed attempts were excluded. Pause-on-focus-loss
was disabled in the config loaded from the temporary Wine prefix, without taking the user's keyboard focus. The captured level test remained
on its introduction screen; active gameplay and character movement were not
validated. The driver floats,
resizes and captures the named top-level game window, not Wine's internal
presentation child. An earlier child-resize attempt produced an invalid black
capture and was discarded. Keys are sent directly to the test window; no
keyboard grabs or global held keys are used.

Both successful runs (menu and level introduction) captured exactly two colors with the monochrome effect,
more than two when disabled, and two again when enabled. They confirmed sizes
640×480, 1000×300 and 800×600 and exited zero on WM_DELETE_WINDOW. Insert did
not open an editor on the tested Wine path; only live-file controls are
validated there. No GL/Vulkan validation errors were reported in the final
Release game run, including teardown. Captures/logs reside under
`/tmp/glyphshift-m5-proton-evidence` (Debug menu) and
`/tmp/glyphshift-m5-proton-level` (Release level introduction).
Durable menu captures: [original](../../docs/m5-supertux-original.png) and
[monochrome glyph result](../../docs/m5-supertux-glyph.png).

After these changes, `cmake --build --preset dev`, `ctest --preset dev`,
`cmake --build --preset release` and the Release CTest command above passed
again (10 Debug tests; 12 Release tests including both graphics smoke suites).
The offline Proton test completes the requested **M5 validation matrix**.
This does not establish full editor support in Wine, all games, VKD3D/D3D12,
other hardware, Wayland or XCB/XI2/raw input. The broader product controls in
"Definition of MVP done" are not newly implemented by M5, and complete MVP
compatibility is not claimed.

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
10. Update this implementation reference when scope, architecture or acceptance
    criteria change; keep the main README and test guide aligned with user-facing
    behavior, build commands and hotkeys.

## M0 implementation checklist

The M0 implementation and verification above cover:

1. Create the minimal CMake project and presets.
2. Add small GLX and Vulkan moving-gradient fixtures with equivalent output.
3. Build both with warnings enabled.
4. Run both under X11, resize them repeatedly, enable Vulkan validation, and
   confirm clean shutdown.
5. Record the exact verified setup and build commands here.

M0 through the separately requested M5 are complete within the recorded
verification scope and limitations. Do not start the later broader-Linux-support
work automatically.
