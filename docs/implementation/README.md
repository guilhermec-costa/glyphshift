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

M6 (standalone capture overlay) is complete within its recorded scope:
capture, window tracking, editor with per-game presets, the background daemon
with a global attach hotkey, and its closing validation. The hotkey attached
the overlay to native SuperTux and to Windows SuperTux through GE-Proton
(offline), with game input, editor, resize tracking, detach and clean exit
checked; latency and GPU cost were measured against the launcher (see
[Closing M6](#closing-m6-real-games-latency-and-gpu-cost)). Game and validation
test assets now live in a persistent test environment
(`tests/smoke/test_env.py`) instead of `/tmp`. M6 step 5 makes the background
overlay an installable desktop application: `cmake --install` with a menu
entry and icon, optional start at login, one daemon per display, and desktop
notifications when started without a terminal, and a system tray icon whose
menu detaches games, picks the style for new attachments, toggles start at
login and quits.

The platform architecture migration (roadmap item 5) is done: the six
couplings listed under [Current state and migration](#current-state-and-migration)
were removed one step at a time, with the existing tests and the focused
XTest drivers passing after each step (see the
[migration record](#migration-record)). The remaining Linux/X11-specific
places it did not cover are listed there.

## Roadmap

Agreed order of the next work, recorded on 2026-10-18 and revised the same
day at the user's request: the Windows capture overlay comes before Wayland,
and the AUR package is not a current goal. Each item still starts only when
the user requests it.

1. **Close M6.** Done on 2026-10-18: attached through the hotkey to native
   SuperTux and an offline Proton game; measured end-to-end latency and GPU
   cost of the overlay against the launcher.
2. **M6 step 5: background app packaging.** Done on 2026-10-18 (see
   [M6 step 5](#m6-step-5-background-app-packaging)): a `glyphshift.desktop`
   entry and icon installed by `cmake --install`, optional autostart
   (`~/.config/autostart`), and a single instance that reports the running
   daemon. The system tray icon (attached games, style choice, start at
   login, quit) followed as a separate step, also done on 2026-10-18 (see
   [System tray](#m6-step-5-system-tray)).
3. **Open-source release readiness.** Partly done (see
   [Open-source release readiness](#open-source-release-readiness)): MIT
   license, CI, the README animation and the PKGBUILD are done and pushed.
   Making the repository public and tagging a first release wait for the
   user. Publishing to the AUR is not planned for now (decided by the user on
   2026-10-18); the PKGBUILD stays in the repository, unpublished.
4. **Art in motion.** Done on 2026-10-18 (see
   [Art in motion](#art-in-motion-roadmap-item-4)). The world-anchored glyph
   grid (camera motion estimation, see Rendering and editor controls) and
   automatic exposure for dark games, which needs its parameters moved from
   push constants (at the 128-byte Vulkan 1.0 ceiling) to a uniform buffer.
5. **Platform architecture migration.** Done on 2026-10-18 (see
   [Current state and migration](#current-state-and-migration)): the couplings
   listed under [Platform architecture](#platform-architecture) were removed,
   so platform, window system and renderer are independent layers.
   Prerequisite for items 6 and 7.
6. **M7 — Windows capture overlay** (below). Started on 2026-10-19; see the
   [M7 record](#m7-record).
7. **Wayland**, after M7. First check the current overlay under XWayland with
   X11 games (cheap, unverified); then add a Wayland window-system adapter
   (portal/PipeWire capture).
8. **Detail at depth** (requested by the user on 2026-10-22, worked on
   alongside M7's remaining steps; see
   [Detail at depth](#detail-at-depth-requested-2026-10-22)): 3D scenes lose
   their distant detail under the glyph effect.
9. **Windows distribution** (requested on 2026-10-22, not started; see
   [Windows distribution](#windows-distribution-requested-2026-10-22)):
   downloadable builds for users who do not compile.

### Detail at depth (requested 2026-10-22)

Seen by the user on ELDEN RING under the Windows overlay: near objects read
well, but the farther away, the more detail is lost. Hypotheses, to be
measured before choosing a remedy:

- The cell grid is fixed: a distant object covers few cells, and its detail
  falls inside one glyph.
- Distance lowers contrast: atmospheric fog flattens distant brightness and
  color, so neighboring cells map to the same ramp level and the same glyph.
- Dark scenes: the tone and exposure settings of a style may leave the dark
  far field below the first ramp level.

The game's depth buffer is not available to the capture overlay.
Windows.Graphics.Capture (and X Composite on Linux) delivers only the finished
color image of the window; the depth buffer lives in the game's GPU memory and
is reachable only from inside the game's process, by hooking its graphics API
as ReShade does. That is injection: the overlay's design rule ("nothing is
loaded into the game") and the anti-cheat exclusions rule it out, on Windows
in particular. The Linux launcher does run inside OpenGL and Vulkan games and
could in principle read a depth attachment, but only for games without
anti-cheat, and identifying the right depth buffer per game is unreliable.
So the work starts from the color image alone. Candidate directions:

- local contrast normalization before the ramp, so fogged distant regions keep
  their internal structure;
- adaptive cell size: smaller cells where the image has fine detail;
- stronger contour weight in low-contrast regions;
- an estimated depth (fog and contrast as a proxy, or a learned monocular
  depth estimate, which costs GPU time and latency), only if the above fall
  short.

Method: ELDEN RING, left open by the user, is the reference target. Its
frames are read from its window (`PrintWindow`, no input, no foreground) and
rendered with `glyphshift-preview`, which the Windows smoke checks to give the
overlay's exact pixels; a frame is fed about 40 times so automatic exposure
and temporal stability settle as they do live. Near and far regions of the
same frame are compared before and after each change. Attaching a real
overlay in the no-focus mode and reading its window was tried first: while
other windows covered the game, the overlay's window read back black, so the
preview is the measuring tool.

First measurement (2026-10-22), one 1920×1080 frame at a temple seen from
below, with the user's own style (a copy of Digital rain: 6×6 cells, exposure
4, contrast 0.54, automatic exposure 0.08; the far field looked like a flat
green block):

| Region | Source luminance, 5th–95th percentile | Mean local deviation in 48 px blocks | Output cells within 10% of the brightest |
| --- | --- | --- | --- |
| Far (the temple, 720×380) | 47–109 | 13.9 | 37% |
| Near (cape and roots, 480×380) | 10–132 | 22.1 | 4% |

The temple spans hundreds of 6×6 cells, so the grid is not what loses it.
Fog compresses the far field into a narrow, lifted band of luminance with
low local contrast; the style's exposure pushes that band to the top of a
ramp with five distinct levels, and a third of the far cells become the same
brightest glyph. The far loss here is tonal. The first remedy to try is local
contrast normalization in the analysis pass: each cell's luminance relative to
its neighborhood's mean and spread, blended with the global tone by a new
setting, so a fogged region keeps its internal structure while near regions
keep their look.

**Step 1, local contrast (2026-10-22).** A new live setting, `local_contrast`
(0..1, default 0; editor **Illustration → Local contrast**, right after Auto
exposure, below every control the focused tests click), persisted in presets
and validated like the other unit-range controls, in shader slot 23
(`art_local_contrast`). Illustrated rendering only, like automatic exposure.
For each cell, analysis takes 7×7 small-box luminance samples over a window of
1/12 of the image height centered on the cell, and:

- blends the cell's tone (after exposure, contrast and gamma) toward its rank
  among the samples by the setting: local equalization, so the darkest cell of
  a neighborhood takes the dark end of the ramp and the lightest the light end
  whatever the exposure;
- divides the contour thresholds (DoG band and lattice) and the shape-matching
  contrast by a gain of 0.12 / the samples' deviation, between 1 and 4,
  scaled toward 1 by the setting, so faint outlines in fog can still vote.

`local_rank` and `local_contrast_gain` in the core mirror the shader and are
unit-tested (`art` test), with the window and limits as shared constants.

How it was reached, on the reference frame with the user's style (cell
measures of the 6×6 green channel; correlation of the output cells with the
source luminance cells):

| Variant | Far: saturated cells, correlation | Near: correlation |
| --- | --- | --- |
| Off | 37%, 0.32 | 0.83 |
| Spread around the local mean (×0.12/deviation), window 1/6 of the height | 26%, 0.45 | 0.79 |
| The same, window 1/12 (far gain 2.6, near 1.3) | 28%, 0.45 | 0.80 |
| Local rank, strength 0.5 (window 1/12) | 21%, 0.59 | 0.79 |
| Local rank, strength 1 | 6%, 0.54 | 0.51 |

Spreading around the mean improved the tone but not the picture: the style's
exposure of 4 keeps a fogged region's mean at the top of the ramp, whose last
two glyphs (`0`, `1`) have nearly the same density, so the temple's arches and
facade still drew as one field (a tone map computed outside the shader showed
the structure was present before the ramp). The rank equalizes regardless of
exposure: at strength 0.5 the arches, columns and facade of the far temple
are drawn, and near detail keeps its correlation; at 1 near lighting flattens.
Glow was ruled out separately (the same flat field with `glow=0`).

New oracle `tests/smoke/local_contrast.py` (Windows oracle phase and the GLX
smoke): bright bands 12 levels apart (fog) and the same bands from black to
white, 6×6 cells; `local_contrast=0` is byte-identical to the default; at 0.5
the fogged bands are 18.2 levels of mean cell brightness apart instead of 2.7,
and the contrasty bands keep theirs (36.2 → 41.7). With the shader's
equalization removed, the oracle failed as intended.

Verification: Release built without warnings, 12/12 CTest tests (the smoke
excluded), format check passed, and the new oracle passed. The smoke's other
phases were not rerun: the change is confined to analysis behind a setting
that is off by default, which the oracle shows byte-identical. Not verified:
the setting live in a game (the user's own check), GPU cost (49 extra
small-box samples per cell when on; the Windows overlay has no GPU timing),
temporal behavior in motion, the classic renderer (not affected), and the
Linux GLX run of the oracle (CI does not run the smoke).

Correction the same evening. The build installed for the user rendered the
default classic effect whatever style was chosen, and editor toggles had no
effect; the user reported it. The source was not at fault: built afresh, the
same commit passes. During this step the Windows clock had been moved back
and the modification times of files written meanwhile were set to the
current time, sources and build outputs alike, so Ninja kept object files
compiled before `ArtSettings` gained its new field, and the overlay mixed two
layouts of the settings structure. A bisection (only the old shader rebuilt:
pass; the current source rebuilt: pass) and the differing hash of the
installed program confirmed a stale build. Both presets were rebuilt with
`--clean-first`; 12/12 CTest tests, the format check and the smoke's overlay,
tracking, editor and oracle phases then passed on Release. The oracle phase
alone had not caught it, since the preview, built in one piece, was correct;
the overlay and editor phases had not been run for this step.

The smoke also found the overlay window by its title, so with the user's own
GlyphShift attached to a game it picked the user's overlay, and the editor
phase posted F10 to it (turning the user's effect off). `win32.wait_window_of`
now finds the window of the class that the test's own process created;
`windows_run.py` and `editor_apps_win32.py` use it.

### Windows distribution (requested 2026-10-22)

Today only people who build from source can use GlyphShift. The plan, waiting
for the user's decisions (making the repository public, the first tag):

- Binaries are not committed to the repository; each tagged version (`v*`)
  publishes them as a GitHub Release, built by a new Windows job in CI
  (Release build, the tests that need no desktop, `cmake --install` into a
  staging directory).
- Two downloads: an installer (Inno Setup: Start menu shortcut, uninstall
  from Windows settings, optional start at sign-in) and a portable ZIP.
- Runtime dependencies: Vulkan's loader comes with the GPU driver; FreeType
  and its DLLs are installed with the programs; the Visual C++ runtime
  (`MSVCP140.dll`, `VCRUNTIME140.dll`), which the programs link dynamically
  today, is to be linked statically so no redistributable is needed.
- The programs are not code-signed: SmartScreen warns on first start until a
  certificate is bought; the download page says so. A `winget` manifest can
  follow.

## Remaining TODOs

These items are pending scope, not instructions to begin implementation.
Only implement work explicitly requested by the user.

### Rendering and editor controls

- [ ] Measure the GPU cost of the phase 2 analysis pass and the phase 3 glow.
  Shape matching reads 16 descriptor texels per matchable glyph in each
  high-contrast cell; glow reads nine neighboring cells per pixel.
- [ ] Contour thresholds are calibrated per style on one SuperTux frame.
  Outlined cartoon textures (SuperTux ice) separate from silhouettes by
  contrast only; other games and scene types are not calibrated.
- [x] World-anchored glyph grid: `anchor_grid`, with camera motion estimated
  on the GPU (art in motion, step 3). Not yet validated in live gameplay;
  parallax, a static HUD and 3D camera motion are not followed.
- [ ] Add explicit aspect correction beyond independent cell dimensions.
- [ ] Validate visual quality and motion across longer gameplay sessions and
  multiple scene types, including fine HUD/text readability.
- [ ] Measure GPU execution cost and gameplay performance more broadly. The
  GL paths (injected and overlay) report GPU time with `GLYPHSHIFT_GPU_TIMING=1`
  ([Closing M6](#closing-m6-real-games-latency-and-gpu-cost)); the Vulkan layer is
  not instrumented, and the editor still shows CPU preparation/submission time.

### Compatibility and validation

- [x] Retry Mesa Vulkan where the direct fixture can create a drawable: the
  full Vulkan smoke passed on lavapipe under Xvfb with Khronos validation
  (2026-10-18, see [Open-source release readiness](#open-source-release-readiness)).
- [x] Run the analytic glyph-filter oracle with Mesa offscreen: it passed on
  llvmpipe under Xvfb as part of the GLX smoke (2026-10-18).
- [ ] Expand coverage to AMD/Intel hardware and additional offline Proton games.
  VKD3D/D3D12 coverage is not established by the SuperTux/DXVK test.
- [ ] Investigate application-owned active input grabs, general XI2/raw-input
  suppression and IME input before claiming support for those paths.

### Later Linux support and distribution

- [ ] Investigate native Wayland input and a Gamescope-style compositor backend.
- [ ] Add EGL only for a concrete compatibility target.
- [ ] Design Steam-specific installation and profile UX.
- [x] Choose and add a project license: MIT, chosen by the user on 2026-10-18
  (`LICENSE`). Dear ImGui keeps its own MIT notice; both are installed.

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
constants stayed at 96 bytes in this phase; phase 3 grows them to 112.

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

## Ink, paper and light (art direction, phase 3)

After phase 2, source-color styles still drew bright scenes as fields of dense
glyphs: ink was always the cell's hue at full brightness, so light areas could
only be expressed by heavy characters. Raising `scene_paper` added color but
brought back the per-cell tile mosaic the user originally objected to.

Source-color ink now takes its **hue** from the scene and its **luminance cap**
from `foreground`: `ink = hue * min(1, luma(foreground) / luma(hue))`. Bright
foregrounds keep the previous behavior (every shipped source-color style uses
foreground luminance of at least 0.95). A dark foreground prints dark ink of the
scene's hue. When paper (`background`) is lighter than ink, silhouette
matching inverts in source color as it already did in monochrome: the glyph
covers the dark group, painted with its color, over the light group's paper.
The tonal ramp still uses `glyph_reverse` for that polarity, unchanged. The
analytic oracle's source-color silhouette case uses black ink on white paper
and still reproduces the source pixels exactly through the inverted path.

New live settings, persisted in presets, validated, and shown in **Illustration**
below every control the focused editor tests click:

| Setting | Range | Effect |
| --- | --- | --- |
| `saturation` | 0..2, default 1 | Source-color ink and paper, including matched groups. |
| `glow` | 0..1, default 0 | Composition pulls each pixel toward the ink of the 3×3 neighboring cells, weighted by glyph coverage and distance: phosphor glow on dark paper, ink bleed on light paper. Requires fill. |
| `paper_wash` | bool, default false | Composition interpolates paper bilinearly between cell centers, so scene-colored paper reads as a wash instead of tiles. |

They occupy a fifth art vec4: GL uploads `art[5]`, and Vulkan push constants
grow from 96 to 112 bytes, within Vulkan 1.0's guaranteed 128. No atlas or GPU
resource is rebuilt when they change. Mosaic and the silhouette oracle keep
`paper_wash` off, since both rely on constant per-cell paper.

Calibration used the same captured SuperTux frame through `glyphshift-preview`.
The ice's chunks have real dark-blue outlines with luminance contrast near 0.4,
while sprite and terrain silhouettes are near-black, around 0.8. With
`edge_strength` 0.6, an `edge_threshold` around 0.42–0.45 keeps silhouettes
and drops most ice contours. Values near 0.6 lost block and tree outlines.

- `illustrated.conf` becomes scene-colored ink on light paper: dark ink
  (foreground 0.18), reversed ramp, ASCII shape glyphs, `scene_paper` 0.35 with
  `paper_wash`, saturation 1.6 and glow 0.25, at 6×10. At 6×10, Tux remained a
  readable outline (`/#=#\` with `>`); 8×12 lost it.
- Newsprint: `edge_threshold` 0.42, glow 0.15. Green terminal: 0.45, glow 0.3.
  Blueprint: 0.6 (it uses `edge_strength` 1), glow 0.45. Amber Braille: glow
  0.2. `illustrated-ink.conf`: 0.45, glow 0.15. Soft color and Mosaic configs
  are unchanged; their regenerated previews differ from phase 2 by normalized
  RMSE 0.00017 (the 0.96 foreground luminance cap) and 0, respectively.
- A dark-paper source-color style with glow was tried and stayed too busy on
  this bright scene; it was not shipped. Lowering Green terminal's exposure
  flattened its shading and was rejected.

The gallery, [shape-color](../shape-color.png) and [shape-ink](../shape-ink.png)
were regenerated. The [live editor capture](../shape-live-editor.png) predates
these phases.

### Phase 3 verification

Verified on 2026-10-17 on the existing Arch/X11/i3/NVIDIA RTX 3060 Ti setup.
Debug built without compiler warnings and passed 12/12 CPU tests, now including
parsing, persistence, shader-parameter placement and rejection of invalid
`saturation`, `glow` and `paper_wash`. All 16 shipped configs parsed without
warnings in `glyphshift-preview`. The Debug smoke target passed on GLX
(including the unchanged analytic oracle) and Vulkan with Khronos core and
synchronization validation, with no `GL debug:` or `Vulkan validation:`
output. The smoke now also requires glow, saturation and paper wash each to
change the live image on both APIs. Their GLX and Vulkan captures matched with
normalized RMSE at most 0.0000093. Release built without warnings and passed
14/14 CTest tests including both graphics smokes with validation (99 seconds).
All six glyph SPIR-V modules passed `spirv-val --target-env vulkan1.0`.

The focused XTest editor tests, SuperTux and GPU timing were not run; the new
sliders and checkbox were not exercised through real input, and calibration
covers one captured frame of one game.

## Temporal stability (art direction, phase 4)

Phases 1–3 were judged on still frames. In motion, cells near a tone boundary,
a shape-fit limit or a contour vote threshold switch glyphs from frame to frame.
Animated lighting, compression noise and sub-pixel camera motion then make the
drawing boil.

`glyphshift-preview` now accepts extra `INPUT OUTPUT` pairs and renders them as
consecutive frames. On a captured SuperTux frame scrolled one pixel per frame,
the share of cells changing glyph per frame was 6.6% (illustrated), 11.6%
(Green terminal) and 6.7% (Newsprint) without stabilization.

The renderers keep two cell grids per target and alternate them. The analysis
pass reads the other grid, holding the previous frame's decisions, through the
same `cell_image` binding. Composition then reads the grid just written.
A new `stability` setting (0..1, default 0) controls hysteresis on all three
decisions:

- **Tonal glyphs** are kept while the cell's tone stays within ±0.06 and its
  internal light/dark contrast within ±0.12 of the values at which the glyph
  was chosen (at stability 1). Each cell texel's glyph word now also stores
  that decision tone and contrast (8 bits each above the glyph index).
  Comparing against the decision, not last frame, prevents slow drifts from
  creeping past the band. The contrast condition is what releases a cell whose
  content changed character but kept a similar mean, such as a gradient cell
  becoming an object border; a tone-only first version left 3.6% of cells
  stale after a real change in the oracle below.
- **Silhouette glyphs** stay while their error is within
  `0.5 * stability * limit` of the new best and within a fit limit widened by
  `1 + stability`. A matched cell keeps matching down to half the contrast
  threshold at stability 1. The cell word records "matched" in bit 3.
- **Contours** persist with two coherent votes, instead of the three a new
  contour needs, while they keep at least half of the leading axis's votes.

History is used only when the previous frame was illustrated, had identical
settings (including grid and glyph atlas) and was processed. GLX compares
per-drawable settings and reallocates both grids only when the grid changes.
Vulkan keeps a shared pair of grids sized for 2×2 cells, two descriptor sets per
swapchain image (binding 4 pointing at each grid) and two cell framebuffers. Both
grids are transitioned once to shader-read layout, because analysis statically
declares the previous grid even on frames without history. A frame presented
with the effect disabled clears history in both backends. When history is
invalid, the renderer sends stability 0 in the existing art parameter slot,
so push constants stay at 112 bytes.

With stability 1, the same scroll changed 5.1%, 6.7% and 4.9% of cells per
frame. Half-pixel scrolling with noise went from 3.9/6.1/3.9% to 2.8/3.0/2.4%.
The remainder is real content moving across a screen-fixed grid; holding it
would require visible lag. Lighting flicker and noise are suppressed almost
entirely. All included illustrated styles and illustrated configs ship with
`stability=0.8`. The editor's **Illustration → Temporal stability** slider sits
below every control the focused editor tests click.

### Phase 4 verification

The new deterministic GPU oracle `tests/smoke/temporal.py` uses only the
Python standard library and runs from the GLX smoke after the analytic oracle.
It renders a generated gradient with an outlined object, where noisy frames add
±6 levels of pixel noise and up to ±8 levels of global brightness flicker. It
asserts:

- a static sequence renders identically with stability 1 and 0;
- the first frame never uses history;
- without stability, the flicker moves more than 5% of cells per frame
  (measured 36.7%); with stability 1, at most 35% of that (measured 0.0%);
- after the flicker sequence, a frame with a moved object and +70 brightness
  differs from a fresh render in at most 2% of cells (measured 0.5%, one cell).

Both API smokes also require `stability=1` to reproduce the static fixture
image exactly, and a temporary trace confirmed that Vulkan actually enabled
history during a 90-frame fixture run with validation (the trace was removed).

Verified on 2026-10-17 on the existing Arch/X11/i3/NVIDIA RTX 3060 Ti setup.
Debug built without compiler warnings and passed 12/12 CPU tests, including
`stability` parsing, persistence, its shader slot and invalid values. The
Debug smoke passed on GLX and Vulkan with Khronos core/synchronization
validation and no `GL debug:` or `Vulkan validation:` output; the GLX/Vulkan
static-stability captures were identical. Release built without warnings and
passed 14/14 CTest tests including both graphics smokes (103 seconds). All six
glyph SPIR-V modules passed `spirv-val`. A single-frame preview of
`illustrated.conf` stayed pixel-identical to phase 3.

The SuperTux scroll measurements are synthetic camera motion on one captured
frame, not gameplay. The focused XTest editor tests, a live SuperTux session
and GPU timing were not run.

```bash
python3 tests/smoke/temporal.py --build build/dev --output /tmp/glyphshift-temporal
python3 tests/smoke/temporal.py --build build/dev --output /tmp/glyphshift-temporal --report
```

## Crisp glyphs and solid strokes (art direction, phase 5, step 1)

The user pointed to Acerola's ReShade ASCII shader as the reference for
personality. Reading it (ideas only; the repository states no license, so no
code was taken) showed pixel-art 8×8 glyphs fetched without filtering: every
pixel is ink or background. A temporary experiment that binarized our masks
showed the gap: the built-in contour strokes were about 2.4 texels of the 16×24
tile, so at an 8×12 cell they integrated to one half-intensity gray pixel.

- Built-in `| - / \` contour glyphs are now four texels wide (half-widths
  0.125 of the tile width, 0.084 of its height): two solid pixels at 8×12,
  with or without crisp glyphs. The analytic contour oracle now expects those
  two pixels fully in the contrasting stroke color.
- `crisp_glyphs` (bool, default false) binarizes the filtered fill and contour
  masks in composition: a pixel is ink when at least 40% of its footprint is
  covered. The art vec4 stays full, so `paper_wash` and `crisp_glyphs` share the
  former wash slot as bit flags (1 wash, 2 crisp); push constants stay at 112
  bytes. The editor's **Illustration → Crisp pixel glyphs** checkbox sits below
  the controls the focused tests click.
- A new oracle group renders the ▘ quadrant at 3×5, 8×12, 13×19 and 64×96 with
  crisp glyphs and requires every pixel to be exactly ink or paper, matching
  the analytic rectangle coverage against the 40% threshold.
- Crisp glyphs are enabled in every included style except Chalk (chalk reads
  better soft), and in `illustrated.conf` and `illustrated-ink.conf`.

Thicker strokes also make the remaining texture contours (SuperTux ice in
Newsprint and Blueprint) more visible; contour *detection* is unchanged in this
step and is the subject of step 2 (pixel-level difference-of-Gaussians edges).

Verified on 2026-10-18 on the existing NVIDIA/X11 setup: Debug and Release
built without warnings; Debug CTest 12/12 and Release 14/14 including both
graphics smokes with Khronos validation. The analytic oracle passed with the
updated contour expectation and the four crisp cases. Both API smokes require
crisp glyphs to change the live image, and their GLX/Vulkan captures were
identical. Focused editor tests and SuperTux were not run for this step.

## Pixel-level DoG contours (art direction, phase 5, step 2)

The reference shader traces outlines from a per-pixel difference of Gaussians
and lets an 8×8 tile become a contour when enough of its pixels agree on a
direction. Our cell lattice (25 box samples per cell) could not tell SuperTux's
outlined ice texture from silhouettes; thicker strokes in step 1 made that more
visible.

`edge_detector=dog` (default `lattice`, unchanged) runs this in the existing
analysis pass, without a new pass. Each cell reads up to 12×12 samples (one per
pixel for cells up to 12 pixels, a regular stride beyond) plus a three-sample
border. Two separable Gaussians (σ = `dog_sigma` pixels and 1.6σ, five taps)
are subtracted and thresholded at `dog_threshold` into a binary edge band.
Sobel on the band gives each sample's direction class (the same four classes
as the lattice detector), and the cell's votes need `edge_votes` of its samples
for a new contour and two thirds of that to keep one (temporal stability). The
existing dominance rule (at least twice any other axis), contour compositing,
crisp strokes and hysteresis are shared. `edge_threshold` applies to the
lattice detector only. Classic and lattice images are unchanged: the default
illustrated preview stayed pixel-identical before the presets were switched.

| Setting | Range | Default |
| --- | --- | --- |
| `edge_detector` | `lattice` / `dog` | `lattice` |
| `dog_sigma` | 0.3..4 px | 1.0 |
| `dog_threshold` | 0..0.2 | 0.025 |
| `edge_votes` | 0..1 | 0.2 |

The defaults come from sweeps on the captured SuperTux frame with Newsprint:
threshold 0.02 outlined trees but also striped the ice, 0.04 cleaned the ice
but lost the trees, and larger σ made outlines sparser. At 0.025 and 20% votes
trees, blocks, the trunk, the arch post and the ground are outlined while most
ice outlines are rejected. Blueprint at 0.02 regained tree outlines but turned
the ice into a hatching pattern, so it keeps the defaults. All eight included
styles plus `illustrated.conf` and `illustrated-ink.conf` now use `dog`.

DoG adds a fourth flag bit (4) and a sixth art vec4 (`dog_sigma`,
`dog_threshold`, `edge_votes`, unused), so push constants grow from 112 to 128
bytes, exactly Vulkan 1.0's guaranteed limit. Further parameters will need a
uniform buffer. The editor shows **Pixel-level contours (DoG)** and its three
sliders after **Contour threshold**, below the focused tests' clicks.

### Step 2 verification

Three new analytic oracle cases render contours only (white on black): a smooth
ramp must produce no stroke; a one-pixel dark line must become a vertical
stroke through every row of its cell with nothing two cells away; a two-pixel
checkerboard must produce no stroke. Unit tests cover parsing, persistence,
parameter placement (flags 7, slots 20–22) and invalid values. Both API smokes
require `edge_detector=dog` to change the live image.

As a rough cost check, 30 frames at 1920×1080 through `glyphshift-preview` took
about 2.2 s with either detector; the run is dominated by PPM I/O and the
difference was within noise (below about 1.5 ms per frame) on the RTX 3060 Ti.
This is not a GPU timer measurement; GPU timing remains a TODO.

Verified on 2026-10-18 on the existing NVIDIA/X11 setup: Debug and Release
built without warnings; Debug CTest 12/12 and Release 14/14 including both
graphics smokes with Khronos core/synchronization validation; all glyph SPIR-V
modules passed `spirv-val`. DoG, crisp and static-stability smoke captures were
identical between GLX and Vulkan. Focused editor tests and SuperTux were not
run for this step.

## Art in motion (roadmap item 4)

Planned in three steps, each verified and committed before the next:
(1) art parameters in a Vulkan uniform buffer, freeing push-constant space;
(2) automatic exposure from a GPU-metered scene luminance, without CPU
readback; (3) a world-anchored glyph grid that follows camera motion estimated
on the GPU.

**Step 1, art parameters in a uniform buffer (2026-10-18).** The art
parameters grow from six to eight vec4s (`art_vectors`, `ArtParameters`; the
two new vectors are zero and reserved for the next steps). GL uploads them as
`uniform vec4 art[8]` as before. Vulkan moves them out of the push constants
into a per-target, host-visible uniform buffer at descriptor binding 5,
written while recording the frame (after the target's previous submission
completed); push constants shrink from 128 to 32 bytes. The shader keeps one
source: `ART` names the uniform block on Vulkan and the uniform array on GL.

Verification: Debug 15/15 (the art test checks eight vec4s with the unused
slots zero); Release 17/17 with Khronos core and synchronization validation,
no `Vulkan validation` line; all five SPIR-V modules passed `spirv-val`.
Comparing the Vulkan smoke captures with the previous commit's: 52 of 60 are
byte-identical, and the 8 with the editor open differ only within the panel's
"CPU render / submit" time text (rows 65–72). GL previews of Comic, Ink &
Edges, `illustrated.conf` and `classic.conf` are byte-identical.

**Step 2, automatic exposure (2026-10-18).** A new live setting,
`auto_exposure` (0..1, default 0; editor **Illustration → Auto exposure**,
below every control the focused tests click), persisted in presets and
validated like the other unit-range controls. It applies to illustrated
rendering, where exposure applies:

- A meter pass (`glyph.frag` built with `METER`) runs before analysis when the
  setting is above zero, as one fragment into a 1×1 R32F image: the geometric
  mean of the scene's luminance over a 32×18 sample grid, floored at 1/255,
  adapted toward the previous value in the log domain. The adaptation rate is
  `1 - exp(-dt / 0.35 s)`, and 1 on the first metered frame (no previous
  value), so a style starts correctly exposed. Two meter images alternate
  with the cell grids; nothing is read back to the CPU.
- Analysis multiplies the user's exposure by
  `clamp((0.4 / mean) ^ auto_exposure, 0.5, 4)`. The key 0.4, limits and
  adaptation are mirrored by `auto_exposure_factor`, `exposure_adaptation` and
  `adapt_luminance` in the core and unit-tested.
- GL: per-target meter textures and framebuffers on texture unit 4, which
  `GlState` now also preserves; the meter program is linked with a float
  target bound (the NVIDIA recompile issue of phase 2). Vulkan: two shared
  meter images with their own render pass and pipeline, descriptor bindings 6
  (the previous value, read by the meter) and 7 (this frame's, read by
  analysis); set s pairs grid s with meters s and s^1.
- `glyphshift-preview` renders sequences at a fixed 1/60 s
  (`Pipeline::set_frame_interval`), so adaptation is reproducible.
- Ink & Edges now uses exposure 0.92 with `auto_exposure=1`. The metered dark
  stand-in gives a factor of 2.39, matching the hand-tuned 2.2 (its gallery
  image changed in 0.01% of pixels and was regenerated; the overview was not),
  while the bright SuperTux frame gets 0.56 instead of blowing out to white.

New oracle `tests/smoke/exposure.py` (run by the GLX smoke, generated
monochrome scene): `auto_exposure=0` is byte-identical to a config without the
key; a static scene does not drift over 30 frames and its first metered frame
equals a fresh render; the same scene at half brightness differs in 100% of
cells with fixed exposure and 0% with full automatic exposure; after a cut
from the dark to the bright scene the first frame differs in 100% of cells
from the adapted image and, two seconds later at 60 fps, in 0% (about 1%
after 1.8 s; this gradient puts every cell near a tone boundary, the worst
case). Replacing the shader's use of the meter with a no-op made the oracle
fail. Both API smokes now require `auto_exposure=1` to change the live
fixture image and to stay unchanged on a static scene; the GLX and Vulkan
captures were byte-identical.

Verification: Debug 15/15 (unit tests for the exposure functions, parsing,
persistence, invalid values and parameter slots 24/25); the Debug smoke
target passed on GLX (hostile-state probe and GL debug output) and Vulkan
(core and synchronization validation); Release 17/17 with no `Vulkan
validation` line; all glyph SPIR-V modules, including `glyph.meter.spv`,
passed `spirv-val`. With the user's permission, the focused
`editor_apps.py --presets` test passed on the Release GLX fixture, selecting
all nine included styles (Ink & Edges with automatic exposure among them)
through the editor, with distinct live images and saved copies. Not measured:
the meter pass's GPU time (576 texel reads in one fragment per frame). Not
verified: the new slider through real input, and a live dark game.

**Step 3, world-anchored grid (2026-10-18).** A new live setting,
`anchor_grid` (default false; editor **Illustration → World-anchored grid**,
below every control the focused tests click), persisted in presets. With it,
illustrated cells follow the camera, so glyphs travel with a scrolling scene
instead of being re-chosen as content slides across a screen-fixed grid.
Without it the image is byte-identical to before. Three passes run before
analysis, all on the GPU (`glyph.frag` built with `MOTION`, `RESOLVE` and
`LUMA`), with no readback:

- **Motion search** (33×33 fragments into an R32F image): for every integer
  shift d in [−16, 16]², the mean truncated (≤ 0.25) absolute luminance
  difference between this frame at 32×18 points and the previous frame at
  those points minus d. Truncation keeps the HUD, sprites and new content
  from dominating. Each point is jittered inside its lattice cell by a fixed
  integer hash: on a regular lattice whose spacing was a multiple of a
  texture's period (800 pixels wide, 4-pixel blocks), all points shared one
  phase and standing still cost as little as the real motion in three frames
  of four, which the first live test exposed.
- **Resolve** (2×1 RGBA32F, two images alternating with the cell grids): the
  lowest cost wins, plus 0.002 per pixel of distance from last frame's
  motion (repeating tiles cost the same at several shifts). It is trusted only
  when at least 0.02 below the mean cost and, unless zero, below 0.75 of
  standing still; otherwise the grid stays. The camera moved by −d; the grid
  offset is that accumulated motion modulo the cell size, texel 0 holding the
  offset and the whole cells it wrapped this frame, texel 1 the motion and
  whether it was trusted. There is no previous state on the first frame,
  after a resize or a cell-size change (offset 0).
- **Luminance** (R8, full size): this frame's luminance for the next search.

Analysis and composition place cell boundaries at `pixel + offset` =
multiples of the cell size; grids get one more column and row for the partial
edge cells (Vulkan's shared grids grow by one texel each way). Paper wash and
glow use the same offset. With temporal stability, each world cell finds its
previous decision where the grid moved it (the wrapped whole cells); cells
entering at an edge have no history. Classic rendering is not anchored.
GL: units 5–7 (previous luminance, costs, camera state), all preserved by
`GlState`; the four optional programs (meter and the three above) are now
linked on first use. Vulkan: bindings 8–11, set s pairing grid s with camera
states s and s^1 like the meters; luminance, costs and camera images with
their passes and pipelines, created with the swapchain renderer.

New oracle `tests/smoke/anchor.py` (run by the GLX smoke; preview frames of a
procedural scrolling scene with a known camera path: steady pans, a diagonal,
a reversal and a 7-pixel step): on a fixed grid 97.6% of interior cells change
per frame at the scene's new place; anchored, 0, in every frame after the
first motion search, also with `stability=1`. A static scene renders the same
anchored or not. A 4-pixel block texture 544 pixels wide (the lattice
aliasing case) must be off the screen grid after 1–6 pixels of scrolling with
7-pixel cells and back on it after 7. Mutations made it fail: never trusting
the motion, ignoring the wrapped cells when reading history, and removing the
jitter. Both API smokes gained a live check: with
`GLYPHSHIFT_FIXTURE_SCROLL=N` the fixtures draw a 4-pixel block texture
(integer hash, identical on both APIs) that holds for one second, scrolls N
pixels and holds; with 7-pixel cells the anchored image equals the
screen-grid image after 21 pixels and differs after 24. The GLX and Vulkan
captures of those runs differ in about 40 pixels (normalized RMSE 0.00002),
as their screen-grid captures do.

On the captured SuperTux frame, synthetically panned 2 pixels per frame over
20 frames (640×480 crops), the share of interior cells differing from the
previous frame at the scene's new place was 0.897 → 0 (`illustrated.conf`),
0.498 → 0 (Newsprint) and 0.938 → 0 (Comic). GPU time on the injected GLX
fixture (Release, Comic, 800×600, scroll texture, 600-frame reports, two runs
each) was within the run-to-run variation: medians 3.2–3.5 ms in all modes,
p95 about 0.4–0.7 ms higher with anchoring.

While testing, the bounded two-context GLX smoke run (which rebuilds the
pipeline after every context switch) went from 11 to 28 seconds. The cause was
outside GlyphShift: the user's NVIDIA shader disk cache had reached about
1.1 GB, so changed shaders were no longer cached and every link recompiled
(analysis about 190 ms); with an empty private cache both the previous commit
and this one took about 1.1 s. `tests/smoke/run.py` now points
`__GL_SHADER_DISK_CACHE_PATH` at `build/PRESET/shader-cache`, so the smoke's
timeouts no longer depend on that cache.

Verification: Debug 15/15 (parsing, persistence, invalid value, parameter
slots 26/27); the Debug smoke target passed on GLX (hostile-state probe, GL
debug) and Vulkan (core and synchronization validation); Release 17/17 with no
`Vulkan validation` line; the nine glyph SPIR-V modules passed `spirv-val`.
Not verified: live gameplay with real scrolling (parallax, animated sprites,
the HUD), the new checkbox through real input, and the focused XTest tests.

## SDL applications (native SuperTux)

Launching Arch's native SuperTux 0.7.0 (`sdl2-compat` 2.32.72 over SDL 3.4.16,
NVIDIA GLX) showed no effect and no editor: `GLYPHSHIFT_LOG=1` printed nothing,
although `/proc/PID/maps` showed `libglyphshift_gl.so` loaded. SDL loads libGL
itself and resolves `glXSwapBuffers` with `dlsym` on that handle, bypassing
symbol interposition. In its default `sdl` video mode SuperTux also presents
through SDL's 2D renderer, which swaps inside SDL3.

Overriding `dlsym` globally was rejected: this glibc (2.44) exports no `_dl_sym`
that would preserve the caller-relative semantics of `RTLD_NEXT`, and layers
such as Steam's overlay that define `glXSwapBuffers` and look up the next one
could then resolve to themselves. Instead, the GL library now also exports the
toolkit calls the application itself makes:

- `SDL_GL_SwapWindow` renders into the current GLX drawable, then forwards.
- `SDL_RenderPresent` first flushes SDL's batched 2D commands into the GL back
  buffer (`SDL_RenderFlush` in SDL2, `SDL_FlushRenderer` in SDL3, taken from
  the same library as the present call), renders, then forwards.

SDL2 and SDL3 differ in return types (int/void versus bool); the integer return
register is forwarded unchanged. Without a current GLX drawable (software, EGL
or Vulkan renderers) the hooks only forward; Vulkan remains covered by the
layer. A thread-local flag stops `glXSwapBuffers` from rendering a second time
if an SDL build presents through the exported GLX symbol. GLX context
lifecycle hooks are still bypassed for such toolkits, so per-context resources
are released only at process exit; single-context SDL games are the tested
case. GLFW and other toolkits that resolve GLX privately are not covered yet.

Verified on 2026-10-18: native SuperTux with `--renderer sdl` and `--renderer
opengl` each logged its interposed path and the editor's X11 input surface,
and window captures showed the Newsprint effect in both. The interposer unit
test now preloads fake SDL entry points and checks argument, return value and
errno forwarding, flush-before-present ordering, and safe behavior when SDL is
absent; removing the flush made it fail. Debug CTest passed 12/12 and the GLX
smoke passed. The editor was not opened through real input in the game.

```bash
./build/dev/glyphshift --profile supertux -- /usr/bin/supertux2
./build/dev/glyphshift --profile supertux -- /usr/bin/supertux2 --renderer opengl
```

## Ink & Edges (art direction, phase 5, step 3)

A style in the spirit of the reference: crisp white letters on black, the
reference's `.:coPO?@` ramp preceded by empty levels for negative space, DoG
contours that replace the fill (`contour_overlay=false`), no shape matching,
stability 0.8. No dark game was available on this machine, so it was
calibrated on [a darkened copy](../presets/dark-scene.png) of the SuperTux frame
(`magick ... -modulate 45,55,100 -gamma 0.75` and a blurred elliptical vignette).
That is a synthetic stand-in, not a validation in a dark game.

With the bright-scene DoG defaults (threshold 0.025) almost no contour fired:
absolute contrasts are smaller in a dark scene. Exposure 2.2, DoG threshold
0.008 and 15% votes gave lit letter fields, black negative space and clean
outlines; exposure 2.6 blew highlights into `█` blocks. On the original bright
frame this exposure overexposes, so the style is documented as made for dark
games, with Exposure as the control to lower. Since
[automatic exposure](#art-in-motion-roadmap-item-4) (art in motion, step 2) the
style uses exposure 0.92 with `auto_exposure=1` instead, which gives the dark
stand-in the same look and keeps bright scenes readable.

## Ready-to-use style collection

Nine embedded styles are listed under **Insert → Preset → Included styles**,
light paper first: Comic, Sepia, Newsprint, Chalk, then dark: Ink & Edges,
Blueprint, Midnight, CRT and Digital rain. All render at strength 1 and redraw the whole scene with the
phase 1–4 controls; see the [preset gallery](../../presets/README.md).

History: six styles (Newsprint, Green terminal, Blueprint, Amber Braille,
Mosaic, Soft color) shipped first as live configs and were later embedded in
the menu. Six more (Comic, Sepia, Chalk, Midnight, CRT, Digital rain) were
calibrated with `glyphshift-preview` on the same SuperTux frame. At the user's
request, the collection was then narrowed to styles that change the art
deeply: Soft color (which blended 65% of the original image back in), Mosaic
(colored blocks, effectively pixelation), Amber Braille and Green terminal were
removed with their configs and images. The menu's "default" row was removed as
well; a fresh profile still starts from the built-in defaults, F10 shows the
original image and **Reset to defaults** restores the base look.

The preset combo uses ImGui's largest popup height. The focused `--presets`
editor test selects every included row and checks distinct live images, but
saves personal copies of only the first and last styles, so the saved section
stays inside the popup without scrolling. Row positions are derived from the
style count.

```bash
python3 tests/smoke/editor_apps.py --build build/dev --application glx-fixture \
  --presets --output /tmp/glyphshift-editor-presets
```

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
push constants at 96 bytes (112 since [phase 3](#ink-paper-and-light-art-direction-phase-3)).
Atlas regeneration remains limited to character/font
changes; changing these art controls does not rebuild GPU resources.

The recommended color/ink presets now use 6×10 cells to preserve smaller objects,
with sparse tonal ramps, stronger contour rejection and optional scene-colored
paper. The coarser blocks preset retains the earlier style. No temporal state or
hysteresis was implemented by this refinement; see
[phase 4](#temporal-stability-art-direction-phase-4).

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
- attaching to an already-running process (code injection or ptrace); the
  later M6 overlay captures a window instead and does not attach;
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

## Platform architecture

GlyphShift is designed to run on several operating systems (Linux now,
Windows planned in M7, macOS possible later) and several window systems
(X11 now, Wayland planned). The architecture separates three independent
axes, so adding one platform, window system or GPU API does not touch the
others:

| Axis | What varies | Implementations |
| --- | --- | --- |
| Platform (operating system) | Configuration paths, font discovery, processes and signals, autostart and packaging | Linux (implemented), Windows (M7), macOS (later) |
| Window system | Window capture, overlay surface, window tracking, editor input, global hotkey | X11 (implemented), Wayland (planned), Win32 (M7), Cocoa (later) |
| Renderer (GPU API) | The glyph pipeline and editor drawing | OpenGL and Vulkan (implemented); D3D11 or Metal only if interop rules them out |

X11 and Wayland are window systems, not renderers: the same OpenGL or Vulkan
renderer works under either one.

### Layers and dependency rules

```text
apps/            launcher, overlay, daemon: the only code that picks concrete
   |             implementations and wires them together
   v
window system    x11 | wayland | win32 | cocoa
   |             capture, overlay surface, tracking, input, hotkeys
   v
platform         linux | windows | macos
   |             paths, fonts, processes, autostart
   v
renderer         opengl | vulkan | (d3d11 | metal)
   |             glyph pipeline: analysis + composition passes, editor drawing
   v
core             settings, presets, characters and atlas layout, glyph
                 semantics, editor model (ImGui), shader sources
```

- **Core** includes no operating-system, window-system or GPU-API header. It is
  testable on any platform with plain unit tests.
- A **renderer** depends only on core and its GPU API. It receives the source
  image as an imported GPU image and never creates windows or reads input.
- A **window system** adapter depends on core, the platform layer, and the
  interop extension that hands captured images to a renderer.
- A **platform** layer depends only on core and operating-system APIs.
- **Apps** compose one window system, one platform and one renderer. They hold
  no rendering or capture logic of their own.

Shaders keep one source of truth. GLSL compiles to SPIR-V today; another API
receives generated code (for example, SPIRV-Cross to HLSL or MSL), not a
hand-maintained copy.

### Interfaces between layers

The names below describe responsibilities; exact C++ signatures are chosen
when each interface is extracted.

- `render::Pipeline`: `draw(target, source, settings)` and
  `draw_editor(ui, target)`. `source` is a renderer-native image: a GL
  texture, a Vulkan image or a D3D11 texture.
- `display::Capture`: turns one window into frames of a renderer-native image
  and reports new frames (XDamage, PipeWire buffers, Windows.Graphics.Capture
  frame pool, ScreenCaptureKit samples).
- `display::Overlay`: a click-through surface kept exactly above the target,
  shown and hidden with it.
- `display::Tracker`: target geometry, visibility, stacking and destruction.
- `display::Input`: editor input as platform-neutral events (keys, text,
  pointer, focus) for the editor model, plus the Insert/F10 hotkeys on the target.
- `display::GlobalHotkey`: the attach/detach key of the daemon.
- `platform::Paths`, `platform::Fonts`, `platform::Process`,
  `platform::Autostart`: configuration directory, font file lookup by family,
  spawning overlays that end with their parent, and start-on-login.

### Image interop, per platform

The capture-to-renderer handoff decides which combinations exist:

| Platform / window system | Capture | Handoff | Renderer |
| --- | --- | --- | --- |
| Linux / X11 | X Composite | `GLX_EXT_texture_from_pixmap` | OpenGL (implemented) |
| Linux / Wayland | xdg-desktop-portal + PipeWire | dmabuf | OpenGL (EGL) or Vulkan |
| Windows / Win32 | Windows.Graphics.Capture | D3D11 shared texture (`VK_KHR_external_memory_win32` or `WGL_NV_DX_interop2`) | Vulkan, OpenGL, or D3D11 |
| macOS / Cocoa | ScreenCaptureKit | IOSurface | Metal, or Vulkan through MoltenVK |

Vulkan is the most portable renderer: native on Linux and Windows, and
available on macOS through MoltenVK. Prefer extending it over adding new
renderers when the interop allows.

### Integration modes

- **Capture overlay** (`glyphshift-overlay`, M6): the cross-platform mode. It
  never enters the game and is the only mode planned for Windows and macOS.
- **Injected rendering** (launcher with GLX interposition and Vulkan layer):
  Linux-specific, lowest latency. It stays a Linux platform feature and is not
  ported; injection into Windows or macOS games is out of scope.

### Current state and migration

The rules above are the target. The code had these couplings, removed
incrementally (all six done on 2026-10-18), each step verified by the existing
tests:

1. Done (see [Migration record](#migration-record)): `src/editor/x11_input.cpp`
   lived in the editor library and the editor entry point took an `x11_input`
   flag. It is now the X11 `display::Input`, with the editor model receiving
   neutral events.
2. Done: `src/config/characters.cpp` called Fontconfig directly; it is now the
   Linux `platform::Fonts`. FreeType rasterization stays in core.
3. Done: `src/config/presets.cpp` resolved `XDG_CONFIG_HOME`/`HOME`; it now
   uses the Linux `platform::Paths`.
4. Done: the OpenGL renderer library resolved GLX entry points (`gl_api.cpp`)
   and the injected renderer was bound to GLX. GL function loading stays in
   the renderer; GLX context handling moved to the X11 adapter.
5. Done: `apps/overlay/main.cpp` contained Composite, GLX pixmap binding and
   overlay window code inline. They are now the X11 `display::Capture` and
   `display::Overlay`, next to the tracker. `daemon.cpp` was split into
   `display::GlobalHotkey` (X11 grab) and `platform::Process` (fork,
   `PR_SET_PDEATHSIG`).
6. Done: the Vulkan renderer existed only inside the layer with its own
   pipeline code; it is now a reusable `vulkan::Pipeline` the overlay can use.

This migration is a roadmap prerequisite for Wayland and M7 and is done
before them, not mixed into feature work.

Code added before the migration already followed the rules: the X11
window-system layer existed as `src/display/x11/` (the tray, static library
`glyphshift_x11_tray`), the Linux platform layer as `src/platform/linux/`
(static library `glyphshift_platform`, headers in
`include/glyphshift/platform/`) with XDG autostart, notifications,
`config_home()` and `executable_path()`.

#### Migration record

Each step is verified with Debug `ctest --preset dev` and the Release suite
with GPU smoke and Khronos validation
(`VK_LAYER_VALIDATE_SYNC=1 python3 tests/smoke/test_env.py run -- ctest
--preset release`), on the NVIDIA/i3 setup recorded above.

Before the first step, the Release suite on the unchanged tree failed once in
`smoke-glx` (15/16): the overlay phase's `XGetImage` of the fixture window
failed right after resizing it. Run alone, unchanged, it passed; the failure is
recorded as intermittent, not caused by the migration.

**Step 1, editor input (2026-10-18).** The editor model (`libglyphshift_editor.so`)
no longer contains or links X11/XCB code:

- `editor::Input` is neutral: a named `editor::Key` (Insert, Escape, F10,
  editing, navigation and modifier keys), the Unicode `character` the key
  produces (on press and release), a pointer `button`, a physical key id for
  release tracking, the event time and pointer position. X11 keysyms are
  translated by `src/display/x11/keys.hpp`, shared by both X11 adapters.
- `editor::InputSource` (poll, sync, forget) replaces the `x11_input` flag of
  `editor::Frame`. The first source given for a window stays with it until
  `deactivate` or `forget`.
- The XCB input surface moved to `src/display/x11/editor_input.cpp` (static
  library `glyphshift_x11_input`, linked into the GL and Vulkan modules and
  the overlay); `display::x11::editor_input()` returns the source, or null
  under `GLYPHSHIFT_FIXTURE_NO_FOCUS`. The Xlib queue filter moved to
  `src/display/x11/xlib_interpose.cpp`, still part of `libglyphshift_gl.so`.

Verification: Debug 15/15 (new `x11-keys` test for the keysym translation;
the editor test now also checks that a source is polled, kept, synced only on
changes and forgotten); Release 17/17. The exported symbols of the GL and
Vulkan modules are unchanged; the editor library's `Frame` constructor changed
as intended. The focused XTest editor test (`editor_apps.py`, Release) passed
on `glx-fixture`, `vulkan-fixture` and `overlay-fixture`.

**Step 2, fonts (2026-10-18).** `platform::bold_monospace_font(families)`
(`include/glyphshift/platform/fonts.hpp`, Linux implementation
`src/platform/linux/fonts.cpp`) returns the first installed family's bold font
file, else the system monospace default. The preferred family list stays in
`characters.cpp`, which no longer includes Fontconfig. The platform library is
now position-independent and linked into the editor library, which is where the
configuration code lives; the core uses only the header's neutral interface,
the same pattern step 3 uses for `config_home()`.

Verification: Debug 15/15 (the platform test checks that a missing family is
not replaced by a substitute and that the default is an existing file);
Release 17/17. `glyphshift-preview` of Comic, Newsprint, `illustrated.conf`
and `characters-ascii.conf` (all with discovered fonts; Hack Bold here) was
byte-identical between the previous Release build and the new Debug build.

**Step 3, paths (2026-10-18).** `platform::config_home()` moved from
`desktop.hpp` to its own `include/glyphshift/platform/paths.hpp`
(`src/platform/linux/paths.cpp`). `PresetStore` resolves its directory through
it, and the per-executable profile through `platform::executable_path()`
instead of reading `/proc/self/exe` itself; the duplicate rule in
`presets.cpp` is gone. Without a usable directory the diagnostic is now
"Presets unavailable: needs an absolute XDG_CONFIG_HOME or HOME". Still
operating-system specific in the configuration code, outside this list: the
atomic preset save uses POSIX directory descriptors (`openat`, `fsync`,
`renameat`), and the editor writes diagnostics with `write(2)`; a Windows
port needs platform equivalents.

Verification: Debug 15/15 (the preset test now checks the default store's
directory with an absolute `XDG_CONFIG_HOME`, a relative one falling back to
`HOME/.config`, the profile from `GLYPHSHIFT_PROFILE` and from the executable,
and refusal without either variable); Release 17/17.

**Step 4, GLX (2026-10-18).** `src/render/opengl/` contains no GLX, X11 or
`dlfcn` code:

- The GL table is filled by `opengl::load_api(loader)` with the loader of the
  window system that owns the context; `api()` returns it afterwards. GLX's
  loader is `display::x11::glx_proc` (`src/display/x11/glx_loader.hpp`,
  `glXGetProcAddressARB` through `RTLD_NEXT` as before); the overlay and
  `glyphshift-preview` call `load_api` before creating the pipeline.
- `opengl::InjectedRenderer` (`injected.{hpp,cpp}`, in the
  `glyphshift_gl_pipeline` library with `GlState`) holds the GL part of the
  injected renderer: capability probe, busy checks, back-buffer capture,
  pipeline and editor drawing under state preservation, keyed by an opaque
  drawable id.
- `src/display/x11/glx_renderer.{hpp,cpp}` holds the GLX part: current
  context/drawable queries, the per-`GLXContext` registry with its
  bound/retired lifecycle, the editor frame and error handling. The GLX/SDL
  interposition moved to `src/display/x11/glx_interpose.cpp` and the injected
  module's export list to `src/display/x11/glx_exports.map`.

Verification: Debug 15/15; Release 17/17 (the GLX smoke's procedure-address
swaps, two contexts, hostile-state probes, GL debug output and shader-failure
pass-through included). The exported symbols of all three libraries are
unchanged, and `libglyphshift_gl.so` still links no GL library. Previews of
Comic, Ink & Edges, `illustrated-ink.conf` and `classic.conf` were
byte-identical to the previous build. Native SuperTux through the launcher
was not rerun; its SDL hooks are covered by the interposer unit test.

**Step 5, capture overlay and daemon (2026-10-18).** The overlay's and
daemon's window-system and operating-system code left `apps/overlay/`:

- `display::x11::Capture` (`capture.{hpp,cpp}`): Composite 0.2 redirection,
  XDamage reports, the target-depth GLX pixmap config, pixmap wrapping after
  resizes, and binding into the renderer's texture.
- `display::x11::OverlayWindow` (`overlay_window.{hpp,cpp}`): the
  override-redirect window with an empty XFixes input shape and no compositor
  shadow, its OpenGL 3.3 core context with swap interval 1, show/hide above
  the tracked target, and presentation.
- `display::x11::Tracker` moved from `apps/overlay/` and now also reads the
  application class for the profile and installs the error handler it
  depends on.
- `display::x11::GlobalHotkey` (`global_hotkey.{hpp,cpp}`): hotkey parsing,
  the `_GLYPHSHIFT_DAEMON_S<screen>` claim, the root grab and its BadAccess
  check, and the 400 ms repeat filter; `focused_client` and `window_title`
  live next to it. These four form the static library `glyphshift_x11_overlay`.
- `platform::Process` (`process.hpp`): `spawn_child` (fork, exec,
  `PR_SET_PDEATHSIG`), `stop_child`, `wait_child`, `ended_children`, stop
  requests from SIGINT/SIGTERM and `stderr_is_terminal`.
- `opengl::OwnedRenderer` (`src/render/opengl/owned.{hpp,cpp}`): the GL calls
  the overlay made itself (state reset, GPU timing, source texture, the F10
  original view, editor drawing), now through the loaded GL table.

`apps/overlay/` keeps option parsing, the profile rule, the attach/detach and
tray-menu logic, and wiring. Its two event loops still read Xlib events and
wait on the X connection: with one window system there is no second
implementation yet to shape a neutral event source, so that abstraction is
left to the Wayland or Win32 work. The `check_hotkey` function of `daemon.hpp`
was replaced by `GlobalHotkey::check`. Messages and exit codes are unchanged.

Verification: Debug 15/15 (the platform test now covers a child stopped on
request, a failed exec, an empty command, a child ending when its parent is
killed with SIGKILL, and SIGTERM becoming a stop request; with `prctl`
removed it failed as expected); Release 17/17. Because the user's own daemon
was running, the overlay smoke's single-instance phase had been skipped; with
the user's permission their daemon was stopped, `tests/smoke/overlay.py`
then passed including that phase, and the daemon was restarted afterwards.
The tray smoke and the tray step of `overlay_daemon.py` stay skipped here
because polybar now runs a real tray; instead, a Release daemon's icon was
seen docked in polybar's tray (its window a descendant of the bar's, mapped,
17×17) and a screen capture showed the drawn icon. With the user's
permission, the focused XTest drivers passed on the Release build:
`editor_apps.py --application overlay-fixture`, `overlay_daemon.py`, and
`overlay_game.py` on native SuperTux and on Windows SuperTux through
GE-Proton in `test_env.py run --offline` (attach by hotkey, input reaching the
game, editor, following a resize, detach, game exit, daemon exit 0).

**Step 6, Vulkan pipeline (2026-10-18).** The layer's `GlyphRenderer` mixed the
glyph pipeline with swapchain handling. It is split into:

- `vulkan::Pipeline` (`src/render/vulkan/pipeline.{hpp,cpp}`, static library
  `glyphshift_vk_pipeline`, built without any window-system define): the atlas,
  ramp and font textures, both cell grids and their history, the analysis and
  composition passes, editor drawing and its per-target geometry buffers. A
  target pairs a sampled source view with an output view; `record(command,
  target, settings, ui)` expects the source in shader-read layout and the
  output in color-attachment layout and leaves the output in the final layout
  given at construction.
- `src/render/vulkan/resources.{hpp,cpp}`: the device-function table
  (`load_render_api` from any `vkGetDeviceProcAddr`), the `Device` description
  and helpers for memory, textures, views and barriers.
- `renderer.cpp` keeps the layer's part: queue and format checks, swapchain
  images, the capture copy and its barriers, command buffers, fences,
  semaphores, `vkSetDeviceLoaderData`, and submission after the application's
  semaphores.

The command stream changed in order only: the one-time atlas upload and the
output image's transition to color-attachment layout are now recorded before
the analysis pass instead of around it (they touch independent images). The
wait-semaphore limit is now checked before recording rather than after.

Verification: Debug 15/15; Release 17/17 with the Vulkan smoke under Khronos
core and synchronization validation (no `Vulkan validation` line). The Vulkan
smoke of the previous commit, built in a separate worktree, produced 60
captures; 52 were byte-identical to the new build's, and the 8 others (editor
open) differ only in the panel's "CPU render / submit" time, confined to that
text line. Exports of all three libraries are unchanged and
`libglyphshift_vk.so` still does not link the Vulkan loader. With the user's
permission, `editor_apps.py` passed on the Release build for
`vulkan-fixture`, `vkcube` and offline Windows SuperTux through
GE-Proton/DXVK with `--level` (the layer logged "Vulkan glyph renderer
initialized" for each of its swapchains; validation was requested with
`VK_INSTANCE_LAYERS`, and no validation message appeared). The capture
overlay still renders with OpenGL; using the Vulkan pipeline there needs
GLX/Vulkan or dmabuf interop and is left to the Wayland or M7 work.

**After the migration.** Still Linux- or X11-specific outside the six items:
the overlay's and daemon's event loops read Xlib events and wait on the X
connection (`apps/overlay/`); the Vulkan layer tracks Xlib surfaces
(`layer.cpp`) and, like the GLX module, uses the X11 editor input — both are
Linux injection modules by design; `presets.cpp` saves atomically through POSIX
directory descriptors and the editor writes diagnostics with `write(2)` (both
moved to the platform layer in M7 step 3, see the [M7 record](#m7-record)).

### Repository layout (target)

```text
.
├── apps/                launcher (Linux), overlay and daemon
├── include/glyphshift/  public headers: config, core, editor, render, display, platform
├── src/
│   ├── core/  config/  editor/     portable; no OS, window-system or GPU headers
│   ├── render/
│   │   ├── opengl/                 pipeline and GL loading
│   │   └── vulkan/                 pipeline; the loader layer stays a Linux injection module
│   ├── display/
│   │   ├── x11/                    capture, overlay, tracker, input, hotkey, GLX interposition
│   │   ├── wayland/                (planned)
│   │   └── win32/                  (M7)
│   └── platform/
│       ├── linux/                  paths (XDG), fonts (Fontconfig), processes
│       └── windows/                (M7)
├── shaders/             single source, compiled per API
├── layers/              Vulkan layer manifest (Linux injection)
├── tests/
└── docs/
```

CMake builds only the platform and window-system directories for the target
operating system. Create a directory only when its work starts.

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
On Windows, `cmake --preset windows-dev` links `build/windows-dev`'s database
the same way (added on 2026-10-20, when the user's clangd found no database:
it searches the source's parent folders and their `build/` child, not
`build/windows-dev`). Windows creates the symbolic link only in Developer Mode
or with the privilege; without it, configuration warns and continues, and
clangd can be pointed at the build directory with `--compile-commands-dir`.
With the link, `clangd --check` on Windows sources (MSVC commands, Windows SDK,
C++/WinRT, Vulkan SDK and vcpkg headers) reported no diagnostic; Linux-only
sources, which are not in the Windows database, still lack their X11 headers
there.

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
execution, effective or permitted process capabilities, setuid/setgid targets,
and targets with file capabilities are refused. Inheritable-only capabilities
(some desktop sessions grant `cap_wake_alarm`) grant nothing to a target
without file capabilities under `no_new_privs`; the launcher clears them with
`capset` instead of refusing, after the user hit that refusal launching
SuperTux. The launcher test checks that the target's inheritable, permitted,
effective and ambient sets are all empty; on this machine its own shell carries
an inheritable capability, so that path ran for real and CTest passed 12/12
without the `setpriv` prefix. The target runs with Linux `no_new_privs`, preventing
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
Contour selection was spatial; temporal hysteresis arrived later in
[phase 4](#temporal-stability-art-direction-phase-4). HUD/text can still lose detail in a full glyph drawing; reducing
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

### M6 — Standalone capture overlay (X11), requested after M5

The user asked for an RivaTuner-style workflow: start the game normally (from
Steam or anywhere), then apply GlyphShift on top, instead of launching the game
through GlyphShift. RivaTuner injects into every process; that remains out of
scope (system-wide injection, anti-cheat exposure, ptrace attachment). M6 adds
a separate application, `glyphshift-overlay`, that never enters the game:

- It captures the target window's contents on the GPU through the X Composite
  extension and `GLX_EXT_texture_from_pixmap`, renders only when XDamage
  reports a new frame, and runs the same two-pass glyph shaders.
- It presents into its own override-redirect window kept exactly above the
  target, with an empty XFixes input shape so mouse and keyboard reach the game.
  The editor makes only its panel input-receiving while open.
- The user attaches with a global hotkey on the focused window. The launcher
  mode stays available as the lower-latency option.

Constraints and expected costs: X11 only (Wayland needs a portal/PipeWire
capture path), about one frame of added latency, extra composition work, and no
exclusive fullscreen; borderless fullscreen windows are supported. Capturing is
read-only screen capture of one chosen window; no process is modified.

Steps, each validated before the next: (1) capture-and-overlay prototype for a
window given by ID, with latency/cost measurements; (2) robust window tracking
(move, resize, unmap, focus, restacking, destruction); (3) editor and
per-game presets keyed by window class; (4) background daemon with global
attach/detach hotkeys, then validation with the fixtures, native SuperTux and
a Proton game.

Exit condition: an independently started game window is transformed live,
input still reaches the game, the overlay follows the window and detaches
cleanly, and measured latency/cost are documented.

#### M6 step 1: capture-and-overlay prototype

`glyphshift-overlay --window ID [--preset NAME] [--log]` attaches to a window
by X11 ID. Without `--preset` it uses `GLYPHSHIFT_CONFIG` (live reload) or the
defaults. It requires X Composite 0.2, XDamage, XFixes and
`GLX_EXT_texture_from_pixmap`, and fails with a diagnostic otherwise.

- The target is redirected with `CompositeRedirectAutomatic` (it stays on
  screen; the redirection ends when the overlay disconnects), its pixmap is
  named with `XCompositeNameWindowPixmap` and wrapped by a GLX pixmap of the
  target's depth; `GLX_Y_INVERTED_EXT` gives the row order, passed to the
  shader through a new GL-only `source_top_down` uniform (default 0 keeps the
  back-buffer convention of the injected renderer and preview).
- XDamage (`ReportNonEmpty`) wakes the loop; each new frame binds the pixmap as
  a texture, runs the glyph passes and swaps with swap interval 1.
- The overlay is an override-redirect window with an empty XFixes input shape
  and `_COMPTON_SHADOW=0`, moved and resized on the target's ConfigureNotify,
  unmapped/remapped with it, and the overlay exits when the target is destroyed.
- The two-pass rendering moved into a `GlyphPasses` module shared by the
  overlay and `glyphshift-preview`. After the refactor the analytic and
  temporal oracles passed unchanged, and previews of `illustrated`,
  `illustrated-ink`, Comic and Ink & Edges stayed pixel-identical.

New smoke `tests/smoke/overlay.py` (run from the GLX smoke) starts the GLX
fixture directly, without launcher, preload or layer, and attaches the overlay
with Newsprint. The overlay window's pixels must equal `glyphshift-preview` of
the captured original (observed: identical, max difference 0); the screen
region over the fixture must match the overlay and not the original (observed
99.97% identical channels to the overlay, 0.5% to the original); the overlay's
input shape must have zero rectangles; closing the fixture must end both
processes with status 0.

Manual checks on 2026-10-18 (NVIDIA RTX 3060 Ti, i3 with picom): an XTest click
(`xdotool`) inside the overlay reached the fixture, which logged the button.
Native SuperTux started on its own (no GlyphShift library in its memory map)
was transformed live; the overlay kept pace at about 60 frames per second
using about 0.2–0.35 ms of CPU per frame including the swap call. End-to-end
added latency was not measured; one display frame is the expected bound with
vsync. Hotkey attachment, the editor and Wayland are later steps.

#### M6 step 2: window tracking

The prototype raised the overlay above every window and only listened to the
target client, so another window raised over the game was covered, frame moves
by the window manager could be missed, a hidden workspace left the overlay on
screen, and an X error during the target's teardown ended the process.

Tracking moved into `apps/overlay/tracker.{hpp,cpp}` (since the platform
migration, `src/display/x11/tracker.{hpp,cpp}`). The tracker finds the
target's top-level ancestor (under i3 the client sits in a frame that is a
root child, confirmed with `XQueryTree`), selects StructureNotify on the client
and frame and SubstructureNotify on the root, and only reads state. After any
relevant event the overlay queries the target and applies it:

- not viewable (`map_state != IsViewable`: hidden workspace, minimized or
  withdrawn), zero-sized, or the effect disabled: unmap the overlay;
- otherwise move/resize it to the target's root geometry, rewrap the pixmap
  after a size change, map it, and restack it directly above the frame
  (`XConfigureWindow` with the frame as sibling) unless it is already there,
  so windows above the game stay above the overlay;
- destruction of the target or frame, or a failed query, ends the overlay with
  status 0. An X error handler records errors instead of Xlib's default exit.

Damage only marks a frame dirty; rendering happens when the overlay is mapped.
The overlay never takes focus, so focus changes need no handling here; the
editor (step 3) will.

`tests/smoke/overlay.py` adds a tracking phase with unmanaged test windows, so
no window manager reacts: the overlay must follow a move and resize, stay
directly above the target while an occluding window raised later stays above
it, follow the target when it is raised over the occluder, unmap with the
target and return with it, and exit 0 when the target is destroyed. Replacing
the restack with the prototype's raise made it fail ("overlay left its place
directly above the target"). Workspace switching, fullscreen toggling and
window-manager frame moves were not exercised live under i3 for this step.

#### M6 step 3: editor and per-game presets

The overlay now runs the shared editor exactly as the injected mode does. Each
loop iteration with a new game frame, an open editor, or every 50 ms while idle
creates an `editor::Frame` for (overlay display, target window). Its X11 input
adapter connects on its own, finds the target's managed client, grabs
Insert/F10 there and, while the editor is open, places its input-only child
inside the target; the overlay's empty input shape lets clicks reach it at the
same coordinates as the panel the overlay draws. The idle polling interval keeps
the hotkeys working while the game is paused and produces no frames; with the
editor open the overlay renders continuously, paced by vsync.

- Settings come from the editor session (saved presets, `GLYPHSHIFT_CONFIG`,
  F10). `--preset NAME` only sets `GLYPHSHIFT_PRESET` for the first frame.
- Presets are kept per game: profile `overlay-CLASS` from the target's
  `WM_CLASS` (lowercased; characters outside the profile alphabet become `-`),
  unless `GLYPHSHIFT_PROFILE` is set.
- Visibility: shown while the target is viewable and the effect is enabled or
  the editor is open. F10 with the editor open shows the original image (a
  strength-0, source-color, classic pass) under the panel; with the editor
  closed the overlay unmaps and the game shows through.
- The overlay resets blending and scissoring before each frame because it owns
  its context; the injected renderer keeps relying on GlState.

The focused driver `tests/smoke/editor_apps.py` gained `--application
overlay-fixture`: it starts the GLX fixture directly, attaches the overlay and
runs the full editor script through real XTest input. In this mode it judges
the composited screen (`X11.screen`) instead of the window's own pixels, and
counts the image as "editor open" when more than 0.1% of pixels are colored:
the first run failed on 62 colored edge/cursor pixels out of 480,000 under the
monochrome effect. Passed on 2026-10-18 (NVIDIA, i3 with picom): Insert opened
the editor over the overlay, a typed cell width and the binary character set
were applied and saved to `profiles/editor-overlay-fixture/app-editor.conf`,
Escape, F10, three resizes with the editor open, focus loss/return and the
title-bar close worked, no UI key or click reached the fixture, and both
processes exited with status 0.

```bash
python3 tests/smoke/editor_apps.py --build build/dev --application overlay-fixture \
  --output /tmp/glyphshift-overlay-editor
```

#### M6 step 4: background daemon and global hotkey

`glyphshift-overlay [--hotkey KEYS] [--preset NAME] [--log]`, without
`--window`, runs in the background (`apps/overlay/daemon.{hpp,cpp}`; its X11 and
process code moved to the window-system and platform layers in the platform
migration). It
registers one global hotkey, **Ctrl+Alt+G** by default; `--hotkey` accepts
`ctrl`, `shift`, `alt` and `super` modifiers joined with `+` to a keysym name
(for example `super+shift+F9`). Each press acts on the focused window:

- The target is the keyboard focus resolved to its managed client (the
  ancestor with `WM_STATE`, so Wine's inner presentation windows map to the
  game's client), or the top-level window without a window manager;
  `_NET_ACTIVE_WINDOW` is the fallback when focus is on the root.
- If that window has no overlay, the daemon starts `glyphshift-overlay --window
  ID` (itself, through `/proc/self/exe`) with the same `--preset`/`--log`;
  otherwise it sends that overlay SIGTERM (detach). One process per game keeps
  each game's editor session and profile separate, and a failure in one
  overlay cannot affect the others.
- Overlays end on their own when the game closes; the daemon reaps them and
  forgets the window. They receive `PR_SET_PDEATHSIG` SIGTERM, so they also end
  if the daemon is killed; on SIGINT/SIGTERM the daemon stops them, waits for
  them and exits 0.
- Presses within 400 ms of the previous one are ignored (key auto-repeat).

This is a deliberate exception to "no root-window grabs" of the injected
editor: a hotkey that works before any game is attached can only be a passive
grab on the root. Only the one chosen combination is grabbed, with and without
CapsLock/NumLock. A grab that conflicts with another client (BadAccess, checked
with `XSync`) ends the daemon with status 1 and a message naming `--hotkey`.

Verification on 2026-10-18 (NVIDIA, i3 with picom):

- Invalid arguments (`--hotkey` with `--window`, unknown modifier) exit 2 with
  usage; a second daemon on the same combination exits 1 with the conflict
  diagnostic, and the first exits 0 on SIGTERM. (Step 5 replaces this: a
  second daemon on the same display now reports the running one and exits 0.)
- New focused driver `tests/smoke/overlay_daemon.py` (real XTest keyboard
  input; `RealInput.key` now accepts several modifiers) passed: the GLX fixture
  started without launcher showed 0.01% of its area different on screen from
  its own pixels; Ctrl+Alt+G started one overlay child with Comic and 56.5%
  differed; a second press ended the child and the screen matched again; a
  third press attached again, closing the fixture ended that overlay while the
  daemon kept running, and SIGTERM stopped the daemon with status 0.
- Its first runs failed with "lost focus" after the first press: the test had
  focused the fixture with `XSetInputFocus` behind i3, and when the hotkey's
  grab ended, i3 handled the resulting FocusIn by restoring the window it
  considered focused (the test's 1×1 sentinel). A probe without key input
  showed that neither starting an overlay nor screen capture moves focus. The
  driver now also focuses the fixture through `i3-msg`, as a user's click
  would.

Native SuperTux and a Proton game were not attached through the hotkey in this
step; latency was not measured.

```bash
python3 tests/smoke/overlay_daemon.py --build build/dev --output /tmp/glyphshift-overlay-daemon
```

#### Closing M6: real games, latency and GPU cost

Roadmap item 1, done on 2026-10-18 on the existing setup (Arch, i3 with picom,
NVIDIA RTX 3060 Ti, driver 615.71.09, one 1920×1080 monitor at 254.87 Hz, so a
display refresh is 3.9 ms). Release build throughout.

**Persistent test environment.** Earlier records used GE-Proton, Windows
SuperTux, a seccomp network wrapper and the Khronos layer extracted under
`/tmp`; a reboot removed them. `tests/smoke/test_env.py setup` now downloads
pinned, SHA-256-checked copies once (GE-Proton11-9 and SuperTux 0.7.0 from
their GitHub releases, whose published digests matched; validation layers
1.4.363.0-1, mesa-utils 9.0.0-7 and vulkan-tools 1.4.363.0-1 from the Arch
archive, whose package signatures verified) into `~/.local/share/glyphshift-test`,
with the Proton prefix and SuperTux test profiles. Nothing is installed
system-wide. `test_env.py run -- CMD` adds the layer to `VK_LAYER_PATH`;
`run --offline` replaces the seccomp wrapper with `unshare --user
--map-current-user --net`, a kernel network namespace with no interfaces. A
probe there got `ENETUNREACH`, X11 still worked through its Unix socket, and
all capability sets were empty. The Proton drivers refuse to start Proton when
a test connection is not refused that way. `editor_apps.py` uses these paths
instead of `/tmp`; the `/tmp` paths in the sections above are historical.

**Hotkey attach on real games.** New focused driver
`tests/smoke/overlay_game.py` (real XTest input, run with the user's
permission) starts SuperTux on its own on `welcome_antarctica` at 800×600 and
the daemon with a monochrome, strength-1 live config, then presses Ctrl+Alt+G
on the focused game. Both runs passed:

| Check | Native SuperTux 0.7.0 (Arch, SDL/GL) | Windows SuperTux 0.7.0, GE-Proton11-9, offline |
| --- | --- | --- |
| Before attaching, share of the screen over the game neither black nor white | 0.62% | 0.61% |
| After Ctrl+Alt+G: one overlay child, share of non-black/white pixels | 0.010% (only compositor corner pixels) | 0.010% |
| Holding Right for 2.5 s changes the game's own image (idle 1 s: 0%) | 42% (Tux walks, the camera scrolls) | 42% |
| Insert / Escape over the game | editor panel shown, then glyphs only again | same |
| Game resized to 640×480 | overlay at the same root geometry (40, 40, 640, 480) | same |
| Ctrl+Alt+G again / a third time | original image back / attached again | same |
| Game closed | game status 0, overlay ended, daemon kept running; SIGTERM: daemon status 0 | same |

The Proton run created its prefix on first use (`Upgrading prefix from None
to GE-Proton11-9`) and attached to the Wine client window. Captures of the
game's own image and of the screen were inspected: the level scrolled under
the overlay's glyphs. Under Wine the hotkey, the editor input surface and
tracking needed no change. The first runs of the driver failed for the test's
own reasons and are not counted: a color-count criterion tripped on eight
anti-aliased corner pixels per gray level, focusing the game through i3
before `RealInput` mapped its sentinel made i3 move focus to the sentinel
after the hotkey (the order `overlay_daemon.py` already uses fixed it), and
the level introduction ends on its own, so Enter was no evidence of input;
the driver now holds Right instead.

**GPU time.** `GLYPHSHIFT_GPU_TIMING=1` enables `GpuTimer`
(`src/render/opengl/gpu_timer.{hpp,cpp}`, renderer layer, GL only): a pair of
`GL_TIMESTAMP` queries around GlyphShift's own work per frame, read up to
eight frames later only when available, so it never waits; a frame is skipped
rather than stalled if the GPU is further behind. Every 600 samples it prints
the frame rate, mean, median, p95 and maximum to stderr. The injected GLX
renderer times the back-buffer capture and glyph passes; the overlay times
the pixmap bind and glyph passes. The editor is excluded. The query entry
points are optional in the GL table, so their absence only disables timing.
In the injected path, query results would be written into a bound
`GL_QUERY_BUFFER` (GL 4.4), so that binding is cleared and restored around the
read. The Vulkan layer is not instrumented.

`tests/smoke/overlay_measure.py cost` ran native SuperTux on its animated
title screen (SuperTux renders about 65 frames per second) with Comic, an
illustrated style with DoG contours, for 20 s per mode after 5 s of warm-up.
GlyphShift's GPU time is the last 600-frame report; the whole-GPU columns are
`nvidia-smi` means (utilization, board power, graphics clock) over the same
20 s:

| Window | Mode | GlyphShift GPU time per frame, median / p95 | Whole GPU busy | Power | Graphics clock |
| --- | --- | --- | --- | --- | --- |
| 1280×720 | game alone | — | 29.4% | 37.0 W | 1065 MHz |
| 1280×720 | launcher (injected GL) | 1.88 / 3.29 ms | 31.9% | 46.8 W | 1294 MHz |
| 1280×720 | overlay | 2.10 / 3.02 ms | 37.3% | 48.8 W | 1313 MHz |
| 1920×1080 | game alone | — | 25.0% | 38.7 W | 1077 MHz |
| 1920×1080 | launcher (injected GL) | 3.02 / 4.01 ms | 39.0% | 52.0 W | 1326 MHz |
| 1920×1080 | overlay | 2.62 / 3.62 ms | 38.6% | 62.1 W | 1678 MHz |

The glyph passes cost about the same in both modes: the overlay replaces the
back-buffer blit with a pixmap bind and runs the same pipeline. The GPU's
clock follows its load, so milliseconds measured at different clocks are not
strictly comparable; at 1080p the overlay ran at a higher clock, which is
part of its lower time. Whole-GPU figures include what the overlay adds
outside GlyphShift's passes (one more full-window surface for picom to
composite), which shows as about 10 W more than the launcher at 1080p. The
cost is per game frame: the overlay renders only on XDamage, here at the
game's ~65 frames per second (0.31–0.35 ms of overlay CPU per frame including
the swap). With the monochrome classic config of the hotkey test at 800×600,
the overlay measured 0.12 ms median. The GLX smoke also passed with
`GLYPHSHIFT_GPU_TIMING=1` (hostile-state probe and GL debug output enabled, no
`GL debug:` line), so the queries leave the application's GL state intact.

**End-to-end latency.** With `GLYPHSHIFT_FIXTURE_LATENCY=FILE` the GLX
fixture clears whole frames black or white, alternating every 30 frames
(60 fps), and logs `CLOCK_MONOTONIC` just before the first swap of each phase.
`tests/smoke/overlay_measure.py latency` reads an 8×8 block at the window's
center from the composited screen (`XGetImage` on the root, about every
0.07 ms) and times each phase change from its swap: directly without
GlyphShift, through the launcher and with the overlay attached by window ID.
This is swap-to-composited-screen time; scanout and the monitor are not
included, and the screen is read on the CPU.

Right after each change the screen read sometimes returns the previous phase
for one compositor refresh before the new one stays: before each swap both
compositor buffers had shown the old phase for 500 ms, so this is most likely
the read returning the other buffer of a page flip, not the display. The
reported latency is therefore the first poll showing the new phase. "Stable"
is the start of the first run of the new phase lasting at least 30 ms;
directly and through the launcher it waits for the fixture's next frame
(16.7 ms later), so it reflects that read effect rather than latency.

120 flashes per mode (the overlay matched 105 with Comic, at least the
required 80%), at 800×600:

| Config | Direct (no GlyphShift) median / p95 | Launcher median / p95 | Overlay median / p95 |
| --- | --- | --- | --- |
| defaults (classic) | 4.49 / 6.84 ms | 5.05 / 8.68 ms | 8.07 / 9.91 ms |
| Comic (illustrated) | 5.01 / 7.02 ms | 6.31 / 8.81 ms | 7.45 / 9.37 ms |

The overlay adds about 2.5–3.6 ms at the median over the direct path, and
1.1–3.0 ms over the launcher: roughly one refresh of this 254.87 Hz display,
consistent with its one extra vsync-paced swap before picom composites it.
The bound recorded at step 1 ("about one display frame with vsync") holds; at
60 Hz the same extra swap would cost up to about 16.7 ms. The launcher adds
0.6–1.3 ms over the direct path. Minimum overlay latency was 5.25 ms in both
configurations. Not measured: photon latency at the monitor, other refresh
rates, games other than the fixture for latency, and Vulkan or Proton
latency.

Verification on 2026-10-18 (commands below): Debug built without warnings and
passed `ctest --preset dev` 12/12; Release built without warnings. The first
Release `ctest --preset release` with the persistent validation layer and
`VK_LAYER_VALIDATE_SYNC=1` failed `smoke-vulkan` (13/14) right after the
measurement runs; its output was not captured, so the cause is unknown. Run
alone it passed, and a second full run passed 14/14 (101 s). Format and
`git diff --check` passed. `overlay_daemon.py` and the editor drivers were not
rerun; their code paths are unchanged except `editor_apps.py`'s application
paths, which were only syntax-checked. The launcher ran the GLX fixture for 60
frames inside `test_env.py run --offline` (injected, status 0); the manual
Proton-through-launcher command in the test guide was not run.

```bash
python3 tests/smoke/test_env.py setup
python3 tests/smoke/overlay_game.py --build build/release --application supertux \
  --output /tmp/glyphshift-overlay-supertux
python3 tests/smoke/test_env.py run --offline -- python3 tests/smoke/overlay_game.py \
  --build build/release --application supertux-proton --output /tmp/glyphshift-overlay-proton
python3 tests/smoke/overlay_measure.py latency --build build/release \
  --config presets/art-comic.conf --samples 120 --output /tmp/glyphshift-latency
python3 tests/smoke/overlay_measure.py cost --build build/release \
  --config presets/art-comic.conf --size 1920x1080 --output /tmp/glyphshift-cost
GLYPHSHIFT_GPU_TIMING=1 python3 tests/smoke/test_env.py run -- \
  python3 tests/smoke/run.py --build build/release --api glx --output /tmp/glyphshift-smoke-timing
VK_LAYER_VALIDATE_SYNC=1 python3 tests/smoke/test_env.py run -- ctest --preset release
```

The M6 exit condition is met for the tested games and setup: an
independently started game window is transformed live, input reaches the
game, the overlay follows the window and detaches cleanly, and latency and
cost are documented. Proton coverage is one game (SuperTux; which graphics path
it used under Proton in this run was not checked); other Proton games,
other GPUs and compositors, and Wayland remain unverified.

#### M6 step 5: background app packaging

Roadmap item 2, done on 2026-10-18 on the same setup. The background overlay is
now a desktop application.

**Installation.** `cmake --install BUILD [--prefix PREFIX]` installs the
launcher, the overlay, the three shared libraries and the layer manifest
together in `PREFIX/lib/glyphshift/` (they find each other through `$ORIGIN`
and `/proc/self/exe`; install RPATH `$ORIGIN`), with symbolic links
`PREFIX/bin/glyphshift` and `PREFIX/bin/glyphshift-overlay`, the desktop entry
`PREFIX/share/applications/glyphshift.desktop` and the icon
`PREFIX/share/icons/hicolor/scalable/apps/glyphshift.svg` (`data/`, a
pixel "G" of glyph cells, no font). The entry starts the background overlay
(`Exec` is the installed `bin/glyphshift-overlay`, written at install time by
`cmake/install_desktop.cmake.in` so `--prefix` is honored and quoted per the
Desktop Entry Specification), `Categories=Game`, no terminal, no startup
notification. The launcher still refuses module paths with whitespace or
colons, so such prefixes support only the overlay.

**Single instance.** One daemon per X display. Before grabbing its hotkey,
the daemon takes ownership of the selection `_GLYPHSHIFT_DAEMON_S<screen>`
through a hidden input-only window carrying its hotkey in
`_GLYPHSHIFT_HOTKEY`, inside `XGrabServer` so checking and taking are one step;
the X server releases it when the process exits. A second launch prints and
notifies "GlyphShift is already running. Press Ctrl+Alt+G in a game..." with
the running daemon's hotkey and exits 0. This replaces the step 4 behavior
where a second daemon failed the hotkey grab with status 1; a hotkey taken by
another application still exits 1, now also with a notification.

**Notifications.** Started from a menu or at login there is no terminal, so
when stderr is not a terminal the daemon also shows a desktop notification
(`notify-send`, freedesktop notifications) on start ("GlyphShift is running.
Press Ctrl+Alt+G in a game to attach or detach."), when already running and on
a hotkey conflict. The notifier is double-forked and never waited for; without
`notify-send` or a notification service nothing is shown and nothing fails.

**Autostart.** `glyphshift-overlay --autostart on [--hotkey KEYS] [--preset
NAME]` validates the options and writes `$XDG_CONFIG_HOME/autostart/glyphshift.desktop`
(or `~/.config/autostart`) atomically, running the absolute path of the
current executable with those options; `--autostart off` removes it. It
starts nothing by itself. The entry is the XDG autostart standard: desktops
such as GNOME, KDE and Xfce run it; i3 needs a runner such as `dex
--autostart`.

New code follows the layer rules: autostart, notifications, `config_home()`
and `executable_path()` form the Linux platform layer (`src/platform/linux/`,
standard library and POSIX only); the selection lives with the X11 hotkey
grab in the daemon.

Verification on 2026-10-18:

- New CPU test `platform`: Exec quoting (plain, space, `"`, `\`, `$`, `%`,
  empty, line break rejected), `config_home` (relative `XDG_CONFIG_HOME`
  ignored), autostart write/replace/remove with no temporary file left,
  relative program rejected, own executable path, and `notify` returning
  without `notify-send`.
- New CPU test `install`: installs into a prefix containing a space and into
  a plain one; checks the layout, the links, the layer manifest's relative
  library path, the desktop entry (`Exec` quoted, `desktop-file-validate`
  clean), that the installed launcher runs `/usr/bin/true` from the plain
  prefix and reports the whitespace limit from the other (status 126), that
  the installed overlay loads and rejects bad `--autostart` and hotkey
  arguments with status 2, and that `--autostart on/off` writes a valid entry
  with the installed private path and options, then removes it.
- The overlay smoke (GLX smoke) gained a single-instance phase without input:
  a second daemon exits 0 naming the first one's hotkey; of two started
  together exactly one stays; a new daemon runs after the first exits. Twenty
  repeated simultaneous starts left exactly one daemon each time.
- With a stand-in `notify-send` that records its arguments, a daemon started
  with stderr in a file notified "GlyphShift is running" and a second launch
  "GlyphShift is already running", both with Ctrl+Alt+Shift+F12, the hotkey of
  the running daemon. This machine runs no notification service (the real
  `notify-send` reports the name is not activatable), so no notification was
  seen on screen. The hotkey-conflict notification was not exercised.
- Debug built without warnings and passed `ctest --preset dev` 14/14; Release
  built without warnings and passed 16/16 with the persistent validation
  layer and `VK_LAYER_VALIDATE_SYNC=1` (107 s). `overlay_daemon.py` (XTest)
  passed on the Release build.
- Reported by the user after `cmake --install build/release --prefix
  ~/.local`: GlyphShift started from the application menu (rofi) and worked.
  This was the user's own check; its details were not recorded.
- Not verified by the agent: login autostart (this i3 configuration runs
  `dex --autostart`, but dex is not installed), icon rendering in a desktop's
  menu, and installation into a system prefix.

```bash
cmake --install build/release --prefix ~/.local
glyphshift-overlay --autostart on
ctest --preset dev -R 'platform|install'
python3 tests/smoke/overlay.py --build build/dev --output /tmp/glyphshift-overlay-smoke
```

#### M6 step 5: system tray

The second part of roadmap item 2, done on 2026-10-18. Started from a menu,
the daemon had no visible presence and could only be stopped from a terminal.

`src/display/x11/tray.{hpp,cpp}` (window-system layer; Xlib and Xft) shows an
icon through the freedesktop System Tray protocol (XEmbed): the daemon offers
its icon window to the owner of `_NET_SYSTEM_TRAY_S<screen>` with
`SYSTEM_TRAY_REQUEST_DOCK`, and listens on the root window for `MANAGER`, so
it docks when a panel starts after it. When the manager goes away the X
server returns the icon to the root, where a window manager can take it; the
daemon destroys that window and offers a fresh one to the next manager (an
early version only unmapped it, and under i3 the window manager's handling of
the orphan raced with the next panel's embedding and left the icon unmapped
on the root). An icon destroyed together with its manager is replaced the
same way. The icon is the application icon drawn with rectangles at the
size the tray gives. Without a tray manager nothing is shown; the hotkey and
notifications work as before.

A click (any button) opens a popup menu: an override-redirect window that
grabs pointer and keyboard while open, closes on Escape or a click outside,
and supports Up/Down/Return. Rows:

- the hotkey reminder;
- "Detach from TITLE" for each attached game (window title from
  `_NET_WM_NAME`, else `WM_NAME`), or "No game attached";
- "Style for newly attached games": "Each game's saved style" (no
  `--preset`) or one of the nine included styles; this changes the `--preset`
  given to overlays started afterwards, not running ones;
- "Start at login", checked when the autostart entry exists; toggling writes
  it with the current hotkey and style, or removes it;
- "Quit GlyphShift", which ends the daemon and its overlays (status 0).

The tray must be destroyed before the display connection closes; the first
test run of Quit exited nonzero because it was destroyed afterwards.

Verification on 2026-10-18:

- New smoke `tests/smoke/overlay_tray.py` (part of the GLX smoke; no XTest,
  synthetic events through `XSendEvent`, a stand-in tray manager in
  `tests/smoke/tray_panel.py` on its own connection, with a save-set like real
  panels): the icon docks when the manager appears after the daemon, after the
  manager's connection closes and a new one starts (fresh icon, no orphaned
  window left) and after a manager destroyed it; the icon shows its two
  colors each time; the menu opens on a click; choosing Comic changes the
  style for new attachments; "Start at login" writes an entry with
  `--hotkey ctrl+alt+shift+F12 --preset art-comic` and removes it again;
  Escape and a click outside close the menu; Quit ends the daemon with status
  0. It is skipped when a real tray manager or another daemon runs. While the
  menu is open the daemon briefly grabs pointer and keyboard. The first run
  failed because the color allocation returned (254, 208, 102) for #ffd166;
  colors are now compared within 2 levels.
- `overlay_daemon.py` (XTest) gained a step: with a game attached by the
  hotkey, "Detach from" in the tray menu (opened and chosen with synthetic
  events) ended its overlay and the screen matched the game again. Passed on
  the Release build.
- Captures of the menu and the docked icon were inspected.
- Debug `ctest --preset dev` 14/14; Release `ctest --preset release` 16/16
  with the persistent validation layer (112 s), the tray phase included.
- Not verified: a real panel's tray (this polybar configuration has no
  `tray` module, and no other tray manager runs), hover drawing under a real
  pointer, and the menu's look on other fonts or HiDPI screens. To run the
  tray tests, the user's own daemon was stopped (SIGTERM) with their
  permission.

```bash
python3 tests/smoke/overlay_tray.py --build build/dev --output /tmp/glyphshift-overlay-tray
```

#### M6 technical debt (tracked)

1. Resolved in step 3: one GL pipeline (`src/render/opengl/pipeline.{hpp,cpp}`,
   static library `glyphshift_gl_pipeline`) built on the run-time GL table
   (`gl_api.cpp`) owns the programs, glyph atlas, cell grids and history, and
   editor drawing. The injected renderer keeps only capture, state
   preservation (GlState), context bookkeeping and the busy-state checks; the
   overlay and preview use the same module, and `GlyphPasses` was removed.
   After the change five reference previews stayed pixel-identical, the
   injected library's exports were unchanged, and CTest plus both API smokes
   (state probes, shader-failure pass-through, overlay) passed.
2. Resolved in step 2: window tracking left the single overlay source file.
3. Resolved in step 3: settings come from the editor session; `--preset` only
   seeds the first frame.
4. Resolved in step 3 without changing the editor: its X11 adapter already
   opens its own connection, so the overlay registers the target window.

### Open-source release readiness

Roadmap item 3, started on 2026-10-18. Publishing steps (pushing the CI to
GitHub, submitting to the AUR, creating the tag) are visible to others and
wait for the user's explicit go-ahead.

- **License.** The user chose MIT: `LICENSE` (Copyright (c) 2026 Guilherme
  China). Dear ImGui keeps its MIT notice. `cmake --install` installs both under
  `share/licenses/glyphshift/` (`LICENSE`, `LICENSE.imgui.txt`); the install
  test checks them.
- **Dependencies.** The README's build list lacked `libxcomposite`,
  `libxdamage`, `libxfixes` (overlay, M6) and `libxft` (tray); added.
- **CI.** `.github/workflows/ci.yml` runs in `archlinux:latest` containers as an
  unprivileged user (the launcher refuses root). Job `build`: format check,
  Debug and Release builds and CPU tests, on every push and pull request. Job
  `graphics` (manual only since 2026-10-18, at the user's request: it runs
  when the workflow is started by hand from the Actions tab): the GLX smoke on
  Mesa llvmpipe and the Vulkan smoke on lavapipe with Khronos validation, in
  Xvfb, with `GLYPHSHIFT_SMOKE_SETTLE=2.5`, keeping `smoke-evidence` as an
  artifact. Software rendering takes longer than the smoke's fixed waits:
  the first container run captured the classic image 0.4 s after switching
  to illustrated, the overlay window before its first frame (showing the
  window below it, without a compositor) and the image right after F10 on
  lavapipe. `run.py` now waits `GLYPHSHIFT_SMOKE_SETTLE` seconds (default
  0.25) after config changes and before captures; `overlay.py` polls for the
  overlay's expected image up to 30 s instead of sleeping 1 s.
- **Local CI check.** Both jobs' steps ran in a local `archlinux:latest`
  container from a copy of the working tree made into a Git repository (the
  format check needs Git; the first attempt without one failed there). Build:
  format check passed; Debug and Release built without warnings and passed
  14/14 CPU tests each. Graphics: the complete GLX smoke (including the
  analytic and temporal oracles, the overlay, single-instance and tray
  phases) passed on llvmpipe, and the complete Vulkan smoke passed on
  lavapipe with validation, both through the same `cmake --build --target`
  commands as the workflow. The workflow itself has not run on GitHub.
- **README animation.** `docs/demo.webp` (640 px, 12 fps, 13.5 s, 3.4 MB):
  native SuperTux's title screen, then the overlay attached to it switching
  Comic, Newsprint and Blueprint through a live config. Recorded on the
  NVIDIA/i3 setup by `tools/record_demo.py` (no input; ffmpeg x11grab, then
  animated WebP, or GIF by extension). A GIF of the same clip was 7.8 MB
  (640 px, 15 fps) or 5.8 MB (560 px, 12 fps). Its frames were inspected.
- **AUR package.** `packaging/arch/PKGBUILD` builds the tagged GitHub source
  tarball with CMake/Ninja (Release, prefix `/usr`), runs the CPU tests in
  `check()` and installs with `cmake --install`. `sha256sums` stays `SKIP`
  until the tag exists and its tarball can be hashed. `makepkg` passed from a
  local tarball with the same layout: 14/14 tests, and the package holds the
  private directory `/usr/lib/glyphshift`, the `/usr/bin` links, the desktop
  entry, the icon and both licenses. `namcap` is not installed and was not run.

On 2026-10-18 the user pushed these commits; the repository is still
private, and the workflow's result on GitHub was not seen by the agent (no
GitHub access here). The user decided not to publish to the AUR yet; with a
private repository the PKGBUILD's source could not be downloaded anyway.

Still to do, when the user decides: confirm the workflow passes on GitHub,
make the repository public and tag `v0.1.0` with release notes. Publishing to
the AUR (filling the PKGBUILD's checksum from the tag's tarball) is not a
current goal. Since 2026-10-18 the CI's graphics job runs only when started by
hand.

### Later — Broader Linux support

- Investigate Wayland input and a Gamescope-style compositor backend.
- Add EGL only for a concrete compatibility target.
- Add Steam-specific installation and profile UX.
- Expand tested Proton compatibility, still restricted to offline use.

### M7 — Windows capture overlay (in progress)

This milestone extends the platform scope beyond Linux. It is the next item
of the roadmap (item 6), before Wayland; its prerequisite, the platform
architecture migration (item 5), is done. It does not wait for a public Linux
release or the AUR. It adds
a Windows platform layer and a Win32 window-system adapter, and ports only the
capture overlay. Injection into Windows
games (D3D9/11/12 hooks) stays out of scope: ReShade already covers that
model, and it carries anti-cheat risk that capture avoids.

- Capture the game window with Windows.Graphics.Capture (official API, no
  injection; request no capture border where the OS allows it).
- Present in a topmost, click-through window (`WS_EX_LAYERED |
  WS_EX_TRANSPARENT`) kept above the game; borderless fullscreen only, as on
  Linux.
- Global attach hotkey with `RegisterHotKey`; editor input through Win32.
- Rendering: reuse the Vulkan renderer through `VK_KHR_external_memory_win32`
  import of the captured D3D11 texture, or the OpenGL renderer through
  `WGL_NV_DX_interop2`; a D3D11 renderer (shaders generated with SPIRV-Cross)
  only if neither interop is reliable. Decided in step 5 of the
  [M7 record](#m7-record): Vulkan, importing the captured texture.
- Reuse unchanged: settings, presets, profiles, the ImGui editor model and
  the two-pass pipeline design.

Exit condition: as for M6, an independently started offline game on Windows is
transformed live, input reaches the game, the overlay follows the window and
detaches cleanly, and latency/cost are measured.

#### M7 record

Started on 2026-10-19 at the user's request, on the machine used for the Linux
work, booted into Windows (dual boot): Windows 11 Pro 10.0.26200, NVIDIA
GeForce RTX 3060 Ti with driver 591.86 (Vulkan device API 1.4.325, loader
1.4.321), one 1920×1080 monitor at 254 Hz. One step at a time, each verified
and committed before the next. Linux tests cannot run here, so after each step
that touches shared code or CMake the user pushes and confirms the Linux CI
`build` job. Tests that take the desktop keyboard or pointer, or start a game,
run only with the user's permission.

**Step 1, environment (2026-10-19).** Already present: Visual Studio
Community 2022 17.10 (MSVC 14.40, with its Ninja), Windows SDK 10.0.22621
(including the C++/WinRT `Windows.Graphics.Capture` headers and
`windows.graphics.capture.interop.h`), CMake 3.30.1, Python 3.12.0 and Git
2.46.2. Installed by the user for M7: Vulkan SDK 1.4.363.0 (`glslangValidator`,
`spirv-val`, headers, loader library and the Khronos validation layer, the
layer version used on Linux), LLVM 22.1.8 for `clang-format` (the version used
on Linux and in CI; Visual Studio ships 17.0.3), and vcpkg at `C:\dev\vcpkg`
with `freetype:x64-windows` 2.14.3 (the FreeType version used on Linux).

After booting from Linux the Windows clock was three hours ahead: Linux keeps
the hardware clock in UTC and Windows reads it as local time. Files written
meanwhile, the whole vcpkg checkout included, had future modification times
after the clock was corrected, and Ninja stopped with "manifest 'build.ninja'
still dirty after 100 tries"; their times were reset and the FreeType build
then succeeded. The
same skew will recur after each Linux boot unless one of the systems is set to
the other's convention (`RealTimeIsUniversal` on Windows or
`timedatectl set-local-rtc 1` on Linux); that choice is the user's. The
checkout has CRLF line endings (Git's global `core.autocrlf=true`); its effect
on embedded presets and shaders is checked in step 3.

**Step 2, test plan (2026-10-19).** Windows gets the coverage Linux has, as
equivalents of the existing tests rather than unrelated new ones. Each row is
implemented with the part it tests, and its result is recorded here like the
Linux ones. Python tests keep using only the standard library (`ctypes` for the
Windows API). Linux-specific tests stay as they are and are only switched off
on Windows.

CTest (no display):

| Linux test | What it covers | Windows equivalent |
| --- | --- | --- |
| `core`, `illustration` | Fixture options and timing; tone, contours, exposure, art parameters, shape matching, summed-area atlas | Unchanged (step 3). |
| `glyph-semantics` | Luminance, ramp, settings validation, live config reload (`mkdtemp`, `setenv`) | Same checks; temporary directory and environment through portable helpers (step 3). |
| `editor` | ImGui model, hotkeys, capture and release, input sources (`setenv`, `/tmp`) | Same checks with the same helpers (step 3). |
| `characters` | UTF-8, atlas, procedural blocks, font rasterization and placement, default font | Same checks; the default font comes from the Windows `platform::Fonts` (steps 3–4). |
| `presets` | Store, restart (forked reader), per-game isolation, malformed and oversized files, symlink rejection, mode 0600, atomic replacement, directory from `XDG_CONFIG_HOME`/`HOME`, profile from `/proc/self/exe` | Same checks: the restart reads from a second process (the test starts itself again) instead of `fork`; owner-only access is checked on the file's ACL instead of mode bits; the symlink case runs where the account may create symbolic links (Developer Mode) and is reported as skipped otherwise; the directory comes from `%APPDATA%`; the profile from `GetModuleFileNameW` (steps 3–4). |
| `platform` (Linux) | Desktop `Exec` quoting, `config_home`, autostart entry, `executable_path`, fonts, child processes (stop, failed exec, parent killed, `SIGTERM`), terminal check | New `platform-windows`: `config_home`, `executable_path`, `bold_monospace_font` (installed family found, missing family not substituted, default file exists) in step 4; command-line quoting for `CreateProcessW`, autostart under `HKCU\...\Run` written, replaced and removed, child stopped on request, failed start, child ending when its parent is killed (job object with kill-on-close), stop requests (console Ctrl+C / close), terminal check, with the process and autostart work. |
| `x11-keys` | X11 keysym translation | New `win32-keys`: virtual-key and `WM_CHAR` translation to `editor::Key` and characters, with the Win32 editor input. |
| `vulkan-help` | Fixture `--help` | The Win32 Vulkan fixture's `--help`, with the fixture. |
| `format-hook` | Pre-commit hook on staged bytes | Same test where Git's `sh` and `clang-format` are available (step 3). |
| `launcher`, `interpose`, `vulkan-layer`, `install`, `glx-help` | Linux injection, GLX, install layout | Off on Windows. Injection is out of scope; no Windows install rules exist yet, and a Windows install test would come with them. |

GPU oracles: `atlas_filter.py`, `temporal.py`, `exposure.py` and `anchor.py`
run unchanged against a Vulkan offscreen `glyphshift-preview` with the same
command line and PPM files (step 6), and their measurements are compared with
the ones recorded above (for example temporal 36.7% → 0.0% and one cell after a
real change; exposure 100% → 0%; anchor 97.6% → 0).

Graphics smoke, without taking the desktop (synthetic window messages and
window or screen reads only):

| Linux test | Windows equivalent |
| --- | --- |
| `run.py` (injected GLX/Vulkan): original vs. transformed, F10, editor open/close, host input restored, strength 0/1/0.37 through the widget, binary set through the editor, named preset and restart, live character and illustration switching, anchored grid on live frames, six resizes with the editor, two contexts/swapchains, failure pass-through, no validation errors | A Windows smoke on the overlay attached by window handle to a Win32 Vulkan fixture started on its own, judged on the overlay's pixels: the same sequence with editor input posted as window messages to the overlay, the overlay restarted to check the named preset, the same live config list, `GLYPHSHIFT_FIXTURE_SCROLL` for the anchored grid, six resizes with the editor, two fixtures with two overlays at once, a test-only renderer failure that leaves the game's image untouched with one diagnostic, and Khronos validation in the overlay with no `Vulkan validation:` line. GLX state probes, procedure-address swaps and unsupported present chains test injection and have no Windows counterpart. |
| `overlay.py`: overlay pixels equal the preview, screen shows the overlay, empty input shape, exit with the target, tracking (move/resize, directly above, under later windows, rises with the target, unmap/map, destroy), single instance | Same checks: the overlay's own pixels against the Vulkan preview, the composited screen over the fixture, input transparency (`WS_EX_TRANSPARENT` and a `WindowFromPoint` hit test over the overlay returning the fixture), tracking with test-owned windows (`SetWindowPos`, z-order directly above the target, a later window above it stays above, raising the target, minimize/restore, destroy ends the overlay with status 0), and one daemon per session in the same three phases. |
| `overlay_tray.py`: icon docks, manager restart, menu, style, start at login, Escape/click outside, Quit | Notification-area icon: present (`Shell_NotifyIconGetRect`), added again after `TaskbarCreated` (an Explorer restart), menu opened through the icon's callback message and driven with keyboard messages, style choice, start at login on/off (the `Run` value with its options), dismissal, Quit with status 0. Showing a popup menu needs the daemon in the foreground for a moment, like the menu's brief grab on X11. |

Focused drivers with real input (`SendInput`; they take the keyboard and
pointer, so each run waits for the user's permission):

| Linux driver | Windows equivalent |
| --- | --- |
| `editor_apps.py` (`overlay-fixture`, `--illustrated`, `--presets`) | The same editor script on the Win32 fixture with the overlay attached. |
| `overlay_daemon.py` | Ctrl+Alt+G (`RegisterHotKey`) attaches, detaches and attaches again; detach from the notification-icon menu; closing the fixture ends its overlay; the daemon stops with status 0. |
| `overlay_game.py` (native SuperTux, SuperTux through Proton) | Windows SuperTux 0.7.0 run natively (the archive pinned in `test_env.py`), plus an offline single-player Steam game from the user's library chosen with the user (online or anti-cheat games are excluded): attach by hotkey, holding Right moves the game, editor open/close, resize followed, detach, attach, game exit. |
| `overlay_measure.py` | Latency: the Win32 fixture alternates black and white frames and records `QueryPerformanceCounter` before each present; the composited screen is polled at the window's center (Desktop Duplication or GDI, chosen when measuring); direct vs. overlay (there is no launcher on Windows). Cost: GPU time of the overlay's passes from Vulkan timestamp queries (`GLYPHSHIFT_GPU_TIMING=1`) and `nvidia-smi`, SuperTux's title screen alone and with the overlay at 1280×720 and 1920×1080. |

Supporting pieces: the fixture window (`tests/fixtures/window.cpp`, Xlib) gets
a Win32 implementation for the Vulkan fixture (`VK_KHR_win32_surface`, same
gradient and test variables); a GL/WGL fixture is not needed, since capture
does not depend on the game's API. `x11.py` and `real_input.py` get Windows
counterparts (window lookup, window and screen capture, posted messages;
`SendInput` that checks and restores the foreground window and the pointer).
`test_env.py` gains a Windows part: the pinned SuperTux archive and its test
profile (networking, release checks, Discord, audio and pausing on focus loss
off) under `%LOCALAPPDATA%\glyphshift-test`, and the validation layer from the
Vulkan SDK. Windows has no unprivileged network namespace, so the offline
guarantee there is the game's own configuration, Steam's offline mode for Steam
games, and, if the user permits, an outbound Windows Firewall rule for the
game's executable (it needs administrator rights). Proton stays Linux-only.

**Step 3, portable build (2026-10-19).** CMake accepts Windows x64 with MSVC
besides Linux x86-64 and picks the implementations per operating system:
Linux keeps every target and command (its blocks are only wrapped in
`if(GLYPHSHIFT_LINUX)`); Windows builds the core, the configuration and editor
library, ImGui, the Vulkan pipeline (`glyphshift_vk_pipeline`, its nine SPIR-V
modules embedded) and a Windows platform layer, with the portable tests. New
presets `windows-dev` and `windows-release` (Ninja, `cl`, vcpkg's toolchain
from `VCPKG_ROOT`, triplet `x64-windows`) run from a Visual Studio developer
shell. MSVC uses `/W4 /utf-8 /permissive-`.

- The visibility attributes of the public configuration headers became
  `GS_EDITOR_API` (`include/glyphshift/config/api.hpp`): the default visibility
  on GCC, nothing on MSVC, where the editor library is static (one process
  holds the overlay's editor; Linux keeps the shared object its injected
  modules share).
- The configuration code's remaining POSIX calls, listed under
  [After the migration](#current-state-and-migration), moved to the platform
  layer as `include/glyphshift/platform/files.hpp`: `PrivateDirectory`
  (preset files read without following symbolic links, replaced atomically and
  durably, owner-only), `file_stamp`/`read_regular_file` (the live config) and
  `write_stderr` (diagnostics). Linux's implementation is the previous code
  moved unchanged (`src/platform/linux/files.cpp`), with the same messages.
- `src/platform/windows/`: `config_home()` is an absolute `APPDATA`, else the
  roaming application data folder (relative values are ignored, as
  `XDG_CONFIG_HOME` is on Linux), so presets live in
  `%APPDATA%\glyphshift\profiles\PROFILE\`; `executable_path()` from
  `GetModuleFileNameW` (the rest of `platform::Process` comes with the daemon);
  files through handles opened with `FILE_FLAG_OPEN_REPARSE_POINT` (a link is
  refused, not followed), `MoveFileExW` with write-through for the atomic
  replace, and a protected access list allowing only the current user for new
  preset files (the counterpart of mode 0600). Win32 has no `openat`, so files
  are opened by full path while a handle keeps the checked directory open.
- Fonts on Windows come from DirectWrite's system collection (it includes
  per-user fonts). A preferred family counts only with a real bold face
  (weight at least 700, not simulated, not a variable font); otherwise the
  search continues, ending at Consolas Bold, then Courier New Bold. Here
  Cascadia Mono is a variable font (the atlas loads a file's default instance,
  regular), and JetBrains Mono is installed only as SemiBold; a probe of the
  font-dependent shape checks of the `illustration` test (`|` and `_` must
  match a centered column and a bottom row) passed with Consolas Bold and Lucida
  Console, and failed with Cascadia Mono's default instance (`_`), Courier New
  Bold (`|`) and JetBrains Mono SemiBold (both). Discovery now selects
  `C:\Windows\Fonts\consolab.ttf` on this machine.
- Two portability fixes in shared code: the font file is read through a UTF-8
  `std::filesystem` path and given to FreeType from memory, so non-ASCII paths
  work on Windows (FreeType opens files with the ANSI `fopen` there); and an
  explicit `glyph_font` must be absolute in the system's own form (a leading
  `/` on Linux as before; `C:\`, `C:/` or `\\server\` on Windows), checked
  without allocating.
- Windows executables get an 8 MB stack (`/STACK`), the main-thread stack Linux
  gives: a character atlas is about 570 KB and is built on the stack. The
  first Windows runs of `illustration`, `characters` and `presets` died with
  stack overflow (0xC00000FD) at the default 1 MB. MSVC Debug also gives each
  atlas temporary of those three tests its own slot in `main`, so they link
  with 32 MB; with that the `characters` test passed where 8 MB still
  overflowed.
- Two implicit conversions MSVC reported (`atlas_width`, two range limits in
  `settings.cpp`) are explicit now; ImGui is compiled without the project's
  warning level.
- The pre-commit hook runs `python3`, which on this Windows is the Microsoft
  Store placeholder (exit 9009); the hook now uses `python3` or `python`,
  whichever runs Python 3. `C:\Program Files\LLVM\bin` is not on this
  machine's `PATH`, so commits made outside a shell that adds it would stop
  at the hook's "clang-format is required".

Tests ported as planned: a shared `tests/unit/test_support.hpp` (environment
variables, temporary directories, the configuration variable `APPDATA` or
`XDG_CONFIG_HOME`, a missing font path in the system's form); `glyph-semantics`
and `editor` use it; `presets` reads the saved store from a second process
(`--read-saved`) on Windows, checks the owner-only access list instead of mode
0600, checks `APPDATA` and the roaming folder instead of `XDG_CONFIG_HOME` and
`HOME`, and reports the symbolic-link case as skipped when links cannot be
created. Linux keeps `fork`, the mode check, the refusal without a
configuration directory, and now fails if it cannot create the link. The
Linux-only tests are switched off on Windows.

Verification on Windows (MSVC 19.40, CMake 3.30.1, Ninja from Visual Studio,
Vulkan SDK 1.4.363.0, vcpkg FreeType 2.14.3): `windows-dev` and
`windows-release` built from scratch without warnings and passed 7/7 CTest
tests each (`format-hook`, `illustration`, `characters`, `presets`, `editor`,
`glyph-semantics`, `core`). The first runs printed "SKIPPED: symbolic link
checks" (the account lacked the symbolic-link privilege); after the user
turned on Developer Mode, the presets test ran them in both presets and
passed: a symbolic link at a preset is refused on reading, and saving replaces
the link without writing to its target. All
nine generated SPIR-V modules passed `spirv-val --target-env vulkan1.0`.
`tools/format.py --check` and `git diff --check` passed. After the user
pushed `7d4c72b`, the CI's Linux `build` job passed (format check, Debug and
Release builds and CPU tests; seen by the user on GitHub). Not verified: any
Vulkan rendering on Windows, which starts with the preview in step 6.

```powershell
cmake --preset windows-dev
cmake --build --preset windows-dev
ctest --preset windows-dev
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

**Step 4, platform tests (2026-10-19).** Two CTest tests cover the platform
layer directly; until now the Windows implementations were only exercised
through the configuration tests.

- `platform-files` (Linux and Windows, `tests/unit/platform_files_test.cpp`)
  tests the step 3 interface itself, so the Linux code moved there is checked
  directly too: a missing directory and file reported as missing; replace and
  read back, replace an existing file, owner-only result (mode 0600 / the
  protected access list); a file at the limit read and one byte over refused;
  a directory refused as a file; no temporary file left; with symbolic links
  available, a link to a file refused on reading, a replacement replacing the
  link and not its target, a link to a directory refused; file stamps for a
  missing file, a regular file, a directory, and an atomic replacement with
  the same size and modification time (the stamp must change); the live config
  read, its size limit, a missing file and a directory; `write_stderr` reaching
  a child process's redirected standard error.
- `platform-windows` (`tests/unit/platform_windows_test.cpp`), the counterpart
  of the Linux `platform` test's paths, executable and font parts:
  `config_home` with an absolute `APPDATA`, a relative one ignored and none
  (both the roaming folder); `executable_path` absolute, existing, this test's
  file; fonts: the default is Consolas Bold, a missing family falls back to it
  rather than to a substitute, Courier New gives `courbd.ttf`, Lucida Console
  (no bold face) and Cascadia Mono (variable font; checked when installed) fall
  back. Process, autostart and the remaining parts of the Linux platform test
  come with the daemon.

The first Windows run of `platform-files` found that reading a directory as a
preset gave `failed` on Windows (`CreateFileW` refuses a directory without
backup semantics) where Linux gives `invalid`; Windows now reports `invalid`
for a directory that is not a reparse point. Two temporary mutations of the
Windows implementation made the test fail as intended and were reverted:
dropping the file identity from the stamp ("atomic replacement with equal size
and time changes the stamp") and creating preset files without the private
access list ("replaced file readable by the user only").

Verification on Windows: `windows-dev` and `windows-release` built without
warnings and passed 9/9 CTest tests each, with Developer Mode on (no skipped
symbolic-link checks). `tools/format.py --check` and `git diff --check`
passed. Not verified: `platform-files` on Linux (it runs in the CI's `build`
job after a push).

**Step 5, renderer decision (2026-10-19): Vulkan.** The overlay renders with
the existing `vulkan::Pipeline`, fed by the captured D3D11 texture imported
through `VK_KHR_external_memory_win32`; the OpenGL renderer through
`WGL_NV_DX_interop2` and a D3D11 renderer are not needed. This follows the
architecture's preference for extending Vulkan, and it was validated before
committing to it with a scratch prototype (outside the repository) on this
machine:

1. A D3D11 device is created on the DXGI adapter whose LUID equals the Vulkan
   device's (`VkPhysicalDeviceIDProperties`), so both use the RTX 3060 Ti.
2. A test window (red left half, blue right half, shown without activation)
   is captured with Windows.Graphics.Capture: a free-threaded frame pool in
   `B8G8R8A8UIntNormalized`, cursor capture off. On this Windows build,
   `GraphicsCaptureAccess::RequestAccessAsync(Borderless)` returned `Allowed`
   and `IsBorderRequired(false)` took effect, so the yellow capture border can
   be turned off for an unpackaged program. The frame was 386×293 for a
   400×300 window (the capture covers the visible frame, without the
   invisible resize borders).
3. Each frame is copied (`CopyResource`) into a texture created with
   `D3D11_RESOURCE_MISC_SHARED_NTHANDLE`, then a shared `ID3D11Fence` is
   signaled. The frame pool recycles its own textures, so the copy decouples
   the overlay from the pool.
4. Vulkan 1.2 imports the NT handle as a dedicated allocation
   (`VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT`; the driver reports it
   importable and dedicated-only) bound to a `B8G8R8A8_UNORM` optimal image,
   and the fence as a timeline semaphore
   (`VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_FENCE_BIT`). The submission waits
   on the fence value, acquires the image from `VK_QUEUE_FAMILY_EXTERNAL`
   (general layout), copies it and releases it back.

Result: the Vulkan copy of the imported image equaled a D3D11 staging readback
of the same texture byte for byte (0 of 293 rows differed) and showed the
window's colors (255,0,0) and (0,0,255); Khronos validation with
synchronization validation reported no message; the synchronous handoff (copy,
signal, Vulkan wait and copy, CPU wait) took 0.384 ms per frame over 300
frames at that size, an upper bound, not the M7 cost measurement. The frame
pool's textures carry the shared flags themselves (misc 0x802); importing them
directly instead of copying is a possible later optimization, not used.
Not established by the prototype: capture of other processes' windows and of
games, frame pacing, resizing (the pool must be recreated with the new size),
and other GPUs or drivers.

**Step 6, Vulkan offscreen preview and the image oracles (2026-10-19).**
`tools/vulkan_preview.cpp` renders through `vulkan::Pipeline` offscreen (no
window or surface), with the GL preview's command line, PPM files and frame
sequences: Windows builds it as `glyphshift-preview`, so the oracles run
unchanged; Linux builds it as `glyphshift-vulkan-preview` next to the GL
preview. Source and output are `R8G8B8A8_UNORM` images; rows stay top-down
(Vulkan samples the source that way); the pipeline's final layout is
`COLOR_ATTACHMENT_OPTIMAL` and the preview transitions the output for the
readback copy. `GLYPHSHIFT_PREVIEW_VALIDATION=1` enables Khronos validation
with synchronization checks, and any warning or error fails the preview. The
first validated run reported a read-after-write hazard: the readback copy was
not ordered after the composition pass's own final layout transition (the
pass has only the implicit external dependency); leaving the output a color
attachment and transitioning it in the preview removed it. `vulkan::Pipeline`
gained `set_frame_interval` like the GL pipeline (0, the default, keeps real
time, so the Linux layer is unchanged). PPM reading and writing moved to
`tools/ppm.hpp`, shared by both previews.

The oracles needed one portability fix: they wrote their configs with
Python's default encoding, which is cp1252 on Windows and cannot encode the
Unicode block characters; their text reads and writes now say UTF-8
(unchanged on Linux, where it is the default). `exposure.py` passes 151 frames
on one command line, which exceeds Windows's 32,767-character limit with a
long output directory (error 206); a short output path such as
`build/windows-dev/oracles/exposure` works.

Results with the Vulkan preview and validation, the same in `windows-dev` and
`windows-release`, against the records above (GL preview on Linux):

| Oracle | Windows (Vulkan) | Linux record (GL) |
| --- | --- | --- |
| `atlas_filter.py` | 14 area-filter cases (error ≤ 1/255), 2 contour-over-fill, 4 crisp, 3 DoG, 28 silhouette cases and the strength-zero bypass passed | same cases passed |
| `temporal.py` | flicker churn 0.367 → 0.000, stale cells after a real change 0.005 | 36.7% → 0.0%, 0.5% |
| `exposure.py` | half brightness 1.000 (fixed) → 0.000 (automatic); after a cut 1.000 → 0.000 within two seconds | 100% → 0%; 100% → 0% |
| `anchor.py` | 0.979 of interior cells change per frame on a fixed grid, 0.000 anchored | 97.6% → 0 |

The anchored-grid oracle passed; its fixed-grid share differs by 0.3 points
from the Linux record, which is a measurement, not a pass criterion.

Cross-check against images rendered on Linux: the nine gallery images
(`docs/presets/`, GL preview on Linux) were re-rendered from the same source
frames (`docs/illustration-original.png`; `docs/presets/dark-scene.png` for
Ink & Edges, as the gallery notes) by the Windows Vulkan preview. Blueprint,
which uses no font (built-in classic glyphs, procedural strokes), was
byte-identical with the discovered font. With the Linux machine's font
(Hack 3.003 Bold, downloaded from the project's GitHub release into a scratch
directory, SHA-256 `0c2604…f8290ac`, passed as `glyph_font`), Sepia, Newsprint,
Chalk, Ink & Edges and CRT were byte-identical too, Comic differed in 3 of
480,000 pixels and Midnight in 1, by one level, and Digital rain in 1,225
pixels: in 613 cells the tips of the crisp `0` glyph are one pixel narrower,
the 40% crisp-glyph threshold falling the other way. Whether that comes from
GL versus Vulkan filtering or from FreeType's build options (the same 2.14.3
version, Arch's package versus vcpkg's) is not isolated; running
`glyphshift-vulkan-preview` on Linux would tell. With Consolas Bold, the
Windows default, the font-based styles differ widely (14–99% of pixels), as
expected from different glyph shapes; glow and paper wash spread a glyph's
difference to its neighborhood. Two harness mistakes were found and fixed
during this check, not counted as results: a `Select-Object -First` stopping
a PNG conversion early, and Windows PowerShell 5.1 reading the configs as
cp1252, which turned `█` into three other characters.

Verification: `windows-dev` and `windows-release` built without warnings,
passed 9/9 CTest tests, and passed all four oracles with validation and no
validation message; `tools/format.py --check` and `git diff --check` passed.
Not verified: the Linux build of `glyphshift-vulkan-preview`, the GL preview
after its PPM helpers moved, and the oracles' UTF-8 change on Linux (the CI's
`build` job compiles the first two; the oracles run in the manual graphics
job or the Linux smoke).

```powershell
$env:GLYPHSHIFT_PREVIEW_VALIDATION = '1'
python tests/smoke/atlas_filter.py --build build/windows-dev --output build/windows-dev/oracles/atlas-filter
python tests/smoke/temporal.py --build build/windows-dev --output build/windows-dev/oracles/temporal
python tests/smoke/exposure.py --build build/windows-dev --output build/windows-dev/oracles/exposure
python tests/smoke/anchor.py --build build/windows-dev --output build/windows-dev/oracles/anchor
```

**Step 7, part 1: the Win32 fixture and the Windows smoke (2026-10-19).** The
overlay's test target and the start of the `run.py`/`overlay.py` counterpart.

- `glyphshift-vulkan-fixture` builds on Windows: `tests/fixtures/vulkan.cpp`
  picks its surface per system (`VK_KHR_win32_surface` or Xlib), and
  `tests/fixtures/window_win32.cpp` is the Win32 `fixture::Window` (client area
  of the requested size in physical pixels, the process being per-monitor DPI
  aware; Escape or closing ends it; `GLYPHSHIFT_FIXTURE_TITLE_SUFFIX`,
  `GLYPHSHIFT_FIXTURE_NO_FOCUS` (shown without activation) and
  `GLYPHSHIFT_FIXTURE_INPUT_LOG` (virtual-key codes and buttons) as on X11).
  The window-independent helpers moved from `window.cpp` to
  `tests/fixtures/common.cpp`, shared by both. `vulkan-help` runs in CTest.
- `tests/smoke/win32.py` is the Windows counterpart of `x11.py` (ctypes only):
  windows by title, the client area as DWM composes it (`PrintWindow` with
  `PW_RENDERFULLCONTENT`, Vulkan content included), the composed screen, input
  posted to a window's queue, moves and resizes without activation, z-order
  and hit tests.
- `tests/smoke/windows_run.py` (target `smoke-windows`; CTest `smoke-windows`
  with `GLYPHSHIFT_GPU_SMOKE_TESTS=ON`, skip code 77) starts the fixture
  directly with Khronos core and synchronization validation and checks: the
  fixed-time image is the shared gradient (red to the right, green downwards)
  and is unchanged after twelve confirmed resizes through the M0 sizes; posted
  `A` and a click reach the fixture in order; `WM_CLOSE`, Escape and the
  two-swapchain mode exit 0 without validation messages; then it runs the four
  image oracles through the Vulkan preview with validation.

Two machine findings. The loader reported "Failed to open JSON file" for two
implicit-layer entries the Epic Games Launcher had left in
`HKCU\SOFTWARE\Khronos\Vulkan\ImplicitLayers` (files and launcher gone); the
fixture counts loader errors during instance creation, as on Linux, so every
run failed. With the user's permission the two values were deleted (the key
was exported first). And four third-party implicit layers load into every
Vulkan process here (OBS's capture hook, RivaTuner Statistics Server, Steam's
overlay and Fossilize); an on-screen display would change the captured images,
so the smoke disables them by name with `VK_LOADER_LAYERS_DISABLE`.
`~implicit~` would also disable NVIDIA's own `VK_LAYER_NV_optimus` and
`VK_LAYER_NV_present`, which stay.

Verification: both Windows presets built without warnings and passed 11/11
CTest tests including `smoke-windows` (about 24 s); the fixture ran 120 frames
and the two-swapchain mode 60 frames with validation in both presets; a
capture of the fixture was inspected. Not verified: the Windows fixture on
other GPUs. After the user pushed the commits up to this part (`c18c414`,
`ed69c7b`, `b9720d1`, `65ca16d`), the CI's Linux `build` job passed (seen by
the user on GitHub), covering `platform-files` on Linux, the GL preview's
shared PPM helpers, `glyphshift-vulkan-preview` and the fixture helpers in
`common.cpp`.

**Step 7, part 2: capture, overlay window and `--window` (2026-10-19).**
`glyphshift-overlay --window HANDLE [--preset NAME] [--log]` attaches to a
window by handle and draws the effect over its client area.

- `display::win32::Capture` (`src/display/win32/capture.{hpp,cpp}`): a D3D11
  device on the Vulkan device's adapter (matched by LUID), a free-threaded
  Windows.Graphics.Capture frame pool, cursor capture off and the capture
  border off when Windows allows it (it did here). Each new frame's client
  area, located inside the captured image from DWM's extended frame bounds,
  is copied into a texture shared through an NT handle, recreated when its
  size changes; the frame pool is recreated when the window's size changes. A
  frame-arrived or closed event wakes the overlay. WinRT errors become
  `std::runtime_error`.
- One shared fence orders the shared texture's users: each D3D11 copy and each
  Vulkan frame waits for the last reserved value and signals the next one; the
  renderer reserves its values only right before submitting, so the producer
  never waits for a value nobody signals.
- `vulkan::OwnedRenderer` (`src/render/vulkan/owned.{hpp,cpp}`, library
  `glyphshift_vk_owned`, kept apart from the pipeline library the Linux layer
  links without the loader): its own instance, device (Vulkan 1.2 with
  timeline semaphores when asked), swapchain (FIFO, recreated on resize or
  when the character selection changes) and `vulkan::Pipeline`, on a surface
  the window system creates. With the effect off and the editor open it shows
  the original image, as the GL overlay does. `GLYPHSHIFT_VULKAN_VALIDATION=1`
  enables Khronos and synchronization validation ("Vulkan validation:" and
  "Vulkan loader:" lines on stderr). `vulkan::Win32Import` (Windows only)
  imports the texture (dedicated allocation) and the fence (timeline
  semaphore) and transfers the image from and back to the external owner
  around each frame.
- `display::win32::OverlayWindow`: a `WS_EX_LAYERED | WS_EX_TRANSPARENT |
  WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` popup, fully opaque,
  placed over the target's client area. A scratch prototype first confirmed
  that a Vulkan swapchain presents into such a window on this driver (on screen
  at the monitor's rate) and that hit tests pass through it, while a plain
  window took them. `display::win32::Tracker`: whether the target exists and
  is viewable (shown, not minimized, not cloaked on another virtual desktop),
  its client area, and its executable name for the profile.
- The app: option parsing, the usage text and the per-game profile rule moved
  from the X11 `main.cpp` to `apps/overlay/options.{hpp,cpp}`, shared by
  `main.cpp` (X11, otherwise unchanged) and `main_win32.cpp`; the profile is
  `overlay-EXECUTABLE` on Windows. `platform::set_environment` and the Windows
  stop requests (console Ctrl+C, Ctrl+Break, close, logoff, shutdown),
  `stderr_is_terminal` joined the platform layer. The background mode and
  `--autostart` report that they are not available on Windows yet.

Smoke (`windows_run.py`, overlay phase, the counterpart of `overlay.py`'s
first part): the fixture started on its own with Newsprint attached by handle.
The overlay's own pixels and the screen over the target equal
`glyphshift-preview` of the captured frame (observed: max difference 0, 100%
identical pixels for both), the screen no longer shows the original (0% of
pixels), the overlay sits exactly on the client area, it is layered and
transparent and a hit test over it finds the target, and closing the target
ends both processes with status 0 without validation messages. The first run
differed in the two lower corners only (99.79% identical): Windows 11 rounds
a window's lower corners into its client area, the capture lacks those pixels
and the expected image (`PrintWindow`) has them. The fixture now asks for
square corners (`DWMWA_WINDOW_CORNER_PREFERENCE`); on a rounded windowed game
the glyph cells in those two corners see the corner's missing pixels
(borderless fullscreen windows have no rounded corners).

On the animated fixture (800×600, Comic, `--log`) the overlay averaged 48–50
frames per second from its start, including the startup, over a 60 fps
target, at 0.39 ms of overlay CPU per frame in Release (0.86 ms in Debug with
validation); this is not the latency and cost measurement, which is a later
part.

Verification: both Windows presets built from scratch without warnings and
passed 11/11 CTest tests, `smoke-windows` included (about 25 s, Debug and
Release). Not verified: tracking beyond position and size (z-order, minimize,
occluding windows, virtual desktops) and the editor, hotkeys, daemon and tray
on Windows, which are the next parts; the Linux build of the shared options
and `glyphshift_vk_owned` (CI).

**Step 7, part 3: window tracking (2026-10-20).** The counterpart of M6 step
2. The overlay is no longer topmost: `OverlayWindow::show(area, target)` puts
it in the target's band (topmost only when the target is) directly above the
target, by inserting it after the window that is above the target, so windows
above the target stay above the overlay; when the window above the target
belongs to the other band (the taskbar, topmost, over the top normal window),
the overlay goes to the top of the target's band instead. Every loop iteration
checks that the nearest visible window above the target is the overlay and
restacks otherwise; visibility follows the window's real state
(`IsWindowVisible`), so a placement that did not apply is retried.
`display::win32::Tracker` installs out-of-context window-event hooks (nothing
is loaded into the target): the target process's destroy, show, hide and
location changes, its minimizing, and foreground changes of any window. They
only wake the overlay's message wait; the loop then queries the state (and
polls every 50 ms in any case).

Smoke (`windows_run.py`, tracking phase, the counterpart of `overlay.py`'s
tracking): two fixtures, the target and an occluder (separate processes with
their own message loops, moved and raised without activation). The overlay
must cover the target and stay directly above it, follow a move and a resize,
stay below the occluder raised over the target while the target moves, follow
the target when it is raised over the occluder, hide while the target is
minimized and while it is hidden and return directly above it when restored or
shown, and exit 0 when the target is destroyed. Replacing the placement with
"always topmost" made it fail ("overlay did not cover the target").

Three defects found on the way, all fixed:

- The first runs of the tracking phase hung: in Debug, the crop of the client
  area called `std::clamp` with its lower bound above its upper bound while
  the target was minimized (the frame's texture is smaller than its content
  size right after a size change), and the CRT's modal assertion dialog froze
  the overlay (the user saw the dialog). The crop now clamps to what both the
  texture and the content hold. In Debug, the overlay and the fixture now
  report CRT assertions and errors on stderr and end instead of showing a
  dialog.
- In Release, the overlay stayed hidden after the target was restored from
  being minimized: the restored target led the normal band, the window above
  it was the taskbar (`Shell_TrayWnd`, topmost), the placement after it did
  not show the overlay, and the overlay's own record said it was shown, so it
  never asked again. The band rule and the real-visibility check above fixed
  both.
- The scratch probes that found these left two overlay processes waiting on
  the assertion dialog; they were ended.

Verification: both Windows presets built without warnings; `windows_run.py`
passed twice in each (fixture, overlay, tracking, oracles). Not verified:
virtual-desktop switching (cloaking is handled but untested), a topmost
target, multiple monitors and DPI changes, and the editor, hotkeys, daemon and
tray, which follow. After the user pushed parts 2 and 3 (`1f24687`,
`925de38`), the CI's Linux `build` job passed (seen by the user), covering the
shared overlay options, `platform::set_environment` and `glyphshift_vk_owned`
on Linux.

**Step 7, part 4: editor input (2026-10-20).** The counterpart of the X11
editor input surface, `display::win32::editor_input(overlay, target)`
(`src/display/win32/editor_input.{hpp,cpp}`), given to the overlay's
`editor::Frame` like the X11 source:

- Insert and F10 are hotkeys of the overlay window (`RegisterHotKey`,
  `MOD_NOREPEAT`), registered only while the target, or the overlay with the
  editor open, is the foreground window and released otherwise, so other
  applications keep those keys; like the X11 passive grab, the target does
  not receive them meanwhile. A registration refused because another
  application holds the key is reported once on stderr and in the panel. A
  hotkey delivers its press only, so the source adds the release.
- While the editor is open the overlay stops being input-transparent and
  activatable-only-by-us (`WS_EX_TRANSPARENT | WS_EX_NOACTIVATE` removed) and
  takes the foreground (a process that received the hotkey may); keys
  (virtual key, scan code as the physical key, text from `ToUnicode` in the
  current layout without disturbing dead-key state) and pointer events (the
  overlay's client coordinates are the target's) go to the editor model; the
  pointer is captured while a button is held. Closing restores the styles and
  returns the foreground to the target once the editor's keys and buttons are
  released, never taking it from another application. Losing the foreground
  while open closes the editor (`focus_lost`), as on X11.
- `src/display/win32/keys.hpp` translates virtual keys (with the text passed
  in) to `editor::Key`; the new CTest `win32-keys` is the counterpart of
  `x11-keys`.
- `GLYPHSHIFT_OVERLAY_NO_FOCUS=1` (tests, the counterpart of
  `GLYPHSHIFT_FIXTURE_NO_FOCUS`): no hotkeys are registered and the
  foreground is never taken; tests post `WM_HOTKEY` (identifiers 1 Insert,
  2 F10), keys and clicks to the overlay window.
- The overlay now also renders when the frame's settings change: the capture
  delivers frames only when the target's content changes, so with a paused
  game a style chosen in the editor, a saved preset or a live config edit
  would otherwise wait for the next game frame.

Smoke (`windows_run.py`, editor phase, the counterpart of `run.py`'s editor
checks, on the overlay attached to the fixture in the no-focus mode, at the
same panel coordinates): the effect transforms the image; F10 shows the
original; Insert opens the editor over it, the open overlay is not
input-transparent and a hit test finds it; Escape closes only the editor and
input passes through again; strength 0 (by Ctrl-click numeric entry) gives
exactly the original pixels, 1 and 0.37 differ from it and from each other;
the binary set chosen and applied through the editor changes the image; the
disabled effect saved as `night` through the UI writes `night.conf`
(`enabled=false`, `glyph_strength=0.37`, `character_set=binary`) and
`active`; a restarted overlay on a new fixture shows the original (the saved
disabled effect) and, after F10, exactly the image of the binary set; six
resizes with the editor opened and closed each keep the image; no validation
message. `--only PHASE[,PHASE]` runs selected phases.

The first runs failed at strength 0 although the panel, opened in a scratch
probe, accepted the same entry: the test posted the typed digit, Enter and
Escape at once, and the editor model treats Escape as a hotkey that closes the
editor and drops input its ImGui frame has not consumed yet, as it would on
X11 with the same timing. The X11 driver paces keys (0.05 s pressed, 0.12 s
after) and clicks (0.06 s, 0.18 s); `win32.key` and `win32.click` now do the
same. The panel was first seen running on Windows in those probe captures.

Verification: both Windows presets built without warnings; `windows_run.py`
passed twice in each (fixture, overlay, tracking, editor, oracles; about
55 s). CTest passed 10 of 11 in both presets: `illustration` failed its two
font-dependent shape checks ("a centered bright column matches |", then the
`_` case). During this part the JetBrains Mono family, including a real Bold,
was installed for the user (22:55); discovery now selects
`JetBrainsMono-Bold.ttf` (preferred over Consolas), and a probe of the two
checks gave no match for either glyph with JetBrains Mono Bold or ExtraBold
and both matches with Consolas Bold. The code these checks exercise did not
change in this part. Not verified: the hotkeys and the foreground hand-over
with real input (they take the keyboard and need the user's permission; the
focused drivers come with the daemon part), IME and dead keys. After the user
pushed `bcdacec`, the CI's Linux `build` job passed (seen by the user).

**The illustration test's font-dependent checks (2026-10-20).** At the user's
choice, the two shape checks no longer compare the discovered font's `|` and
`_` with an ideal centered column and bottom row, which held for DejaVu, Hack
and Consolas but not for JetBrains Mono, Courier New or Cascadia Mono's
default instance. They take the glyphs' own measured 4×4 descriptors, require
a font-independent shape (`|`: at least twice the ink in the two central
columns as in the outer ones, and ink in the top and bottom rows; `_`: at least
twice the ink in the bottom row as in the top one) and require each descriptor
to match back to its glyph. The placement of `_`, `.` and `'` in the cell
stays covered by the `characters` test. A scratch probe applied the new checks
to Hack Bold (the Linux machine's font, from the scratch download of step 6),
Consolas Bold, Courier New Bold, Lucida Console, JetBrains Mono Bold and
ExtraBold and Cascadia Mono: all passed. Two temporary mutations failed the
test as intended: shape matching that never matches, and the two glyphs'
descriptors swapped (caught by "| is a central column spanning the cell"; a
first version of that check, without the top and bottom rows, let a narrow
`_` pass as a column and was strengthened). Both Windows presets passed 11/11
CTest tests with JetBrains Mono Bold discovered. Not verified: the Linux run
with DejaVu (CI).

**Step 7, part 5: the background daemon (2026-10-20).** The counterpart of M6
step 4 and of step 5's single instance and start at login (the tray follows
in part 6). `glyphshift-overlay [--hotkey KEYS] [--preset NAME] [--log]`
without `--window` runs in the background (`apps/overlay/daemon_win32.cpp`,
same `run_daemon` interface and messages as Linux): Ctrl+Alt+G by default
attaches a `glyphshift-overlay --window HANDLE` process to the foreground
window, or detaches it; overlays that end are reaped; on a stop request the
daemon stops its overlays, waits for them and exits 0.

- `display::win32::GlobalHotkey` (`src/display/win32/global_hotkey.{hpp,cpp}`):
  the same hotkey syntax (`ctrl`, `shift`, `alt`, `super`/`win` and a letter,
  digit, F1–F24 or one of the X11-style key names listed in the header),
  `RegisterHotKey` on a hidden daemon window, presses within 400 ms ignored.
  One daemon per session: a named mutex (`Local\GlyphShiftDaemon`); the
  daemon window's title publishes its hotkey, so a second launch reports
  "GlyphShift is already running. Press Ctrl+Alt+G in a game..." and exits 0.
  A hotkey held by another application ends the daemon with status 1 and the
  `--hotkey` hint. `focused_window` is the foreground top-level window unless
  it is the desktop, the taskbar or a GlyphShift window; `window_title` names
  it for messages.
- `platform::Process` on Windows: `spawn_child` (`CreateProcessW` with
  arguments quoted for the Microsoft C runtime,
  `src/platform/windows/command_line.hpp`), each child in a job object with
  kill-on-close, so overlays end with the daemon even when it is killed (the
  counterpart of `PR_SET_PDEATHSIG`), created suspended until assigned;
  `stop_child` signals the child's named stop event (`Local\GlyphShiftStop-PID`,
  the counterpart of SIGTERM), or ends it when it does not listen;
  `catch_stop_requests` creates that event for the current process and
  watches it besides the console's Ctrl+C, Ctrl+Break and close;
  `wait_child`, `ended_children`. Children share the daemon's console, or get
  none (`CREATE_NO_WINDOW`).
- Start at sign-in: `glyphshift-overlay --autostart on|off [--hotkey]
  [--preset]` writes or removes the value `GlyphShift` under
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` with the absolute
  program and options (`src/platform/windows/desktop.cpp`);
  `GLYPHSHIFT_AUTOSTART_VALUE` names another value (tests).
  `platform::notify` does nothing on Windows yet; the notification-area icon
  of part 6 will show those messages.
- Launched from the menu or at sign-in, Windows gives the console program its
  own console window; the daemon releases a console that only it uses
  (`GetConsoleProcessList` = 1, `FreeConsole`), so it may flash briefly.
  Started from a terminal it keeps writing there.
- The hotkey's display name ("Ctrl+Alt+G") moved to the shared
  `apps/overlay/options.cpp`, and the Linux daemon uses it.
- CMake: the compilation-database link check compared Windows's NT-style link
  target (`\??\C:\...`) with a forward-slash path and warned on every
  configuration; it now normalizes the target first (no effect on Linux).

Tests:

- `platform-windows` gained the process and start-at-sign-in parts of the
  Linux `platform` test, with helper modes of its own executable as children:
  nine arguments (spaces, quotes, trailing backslashes, empty, tab, non-ASCII)
  quoted by `command_line` and parsed back unchanged by `CommandLineToArgvW`;
  the Run value written with the program quoted, replaced, a relative program
  rejected, removed once (under a test-only value name; no `GlyphShift` value
  was left); a child listening for stop requests exits 0 after `stop_child`;
  a child that does not listen is ended and reported by `ended_children`; a
  program that cannot start and an empty command are refused with -1 (Linux
  reports a failed exec as a child that ends); a child's own child ends when
  that child is killed; the process's own stop event becomes a stop request;
  the terminal check.
- `windows_run.py` gained the daemon phase (overlay.py's single instance,
  without keyboard input, hotkeys `ctrl+alt+shift+F11`/`F12`): a second daemon
  exits 0 naming the first one's hotkey; of two started together exactly one
  stays; a new daemon runs after the first exits; each stops with status 0
  through its stop event. The editor phase now ends by stopping the overlay
  through its stop event (the daemon's detach): it exits 0, its window is
  gone, the target keeps running and the screen shows its own image.

Verification: both Windows presets built without warnings and passed 11/11
CTest tests; `windows_run.py` passed twice in each (fixture, overlay,
tracking, editor, daemon, oracles; about 60 s). Not verified: attaching and
detaching with the real Ctrl+Alt+G on a focused game (it takes the keyboard;
the focused driver needs the user's permission), sign-in start by Windows
itself, and the Linux build of the shared hotkey name (CI). After the user
pushed `93f2b6b`, `4b2ebb6` and `6ff449a`, the CI's Linux `build` job passed
for each (seen by the user).

**Step 7, part 6: the notification-area icon (2026-10-21).** The counterpart
of the M6 system tray. `display::win32::Tray` (`src/display/win32/tray.{hpp,cpp}`)
adds the daemon's icon with `Shell_NotifyIcon` from a hidden top-level window
(class `GlyphShiftTray`), so it also receives the `TaskbarCreated` broadcast
and adds the icon again when Explorer restarts its taskbar (or the DPI
changes). The icon is the application icon's "G" of glyph cells on its dark
rounded tile, drawn at the small-icon size of the window's DPI; the tooltip
names the hotkey.

- Any click on the icon (`NIN_SELECT`, `NIN_KEYSELECT`, `WM_CONTEXTMENU`,
  `NOTIFYICON_VERSION_4`) opens a popup menu with the Linux rows and choices:
  the hotkey reminder; "Detach from TITLE" per attached game, or "No game
  attached"; "Style for newly attached games" with "Each game's saved style"
  and the nine included styles (checked when current; it changes the
  `--preset` of overlays started afterwards); "Start at login" (checked when
  the `Run` value exists; toggling writes it with the current hotkey and
  style, or removes it); "Quit GlyphShift" (the daemon stops its overlays and
  exits 0). Rows that cannot be chosen are grayed; ampersands in window
  titles stay literal. The menu is opened from the daemon's loop, not inside
  the icon's callback; the daemon takes the foreground while it is open, the
  documented pattern for these menus (otherwise it would not close on a click
  elsewhere), except under `GLYPHSHIFT_OVERLAY_NO_FOCUS=1` (tests). The menu
  is modal: the daemon's loop waits until it closes.
- Notifications: Windows shows them only from a notification-area icon, which
  is window-system code, so `platform::notify` stays a no-op on Windows and the
  daemon notifies through its icon (balloons, shown by Windows 11 as toasts):
  "GlyphShift is running" when started without a terminal; "GlyphShift could
  not start" when the hotkey is taken, the daemon keeping its icon until the
  notification is hidden (at most 10 s) before exiting 1; and "GlyphShift is
  already running" for a second launch without a terminal, which posts a
  registered message to the running daemon's window
  (`GlobalHotkey::remind_running`) so the running daemon's icon shows it.
  `GLYPHSHIFT_NO_NOTIFICATIONS=1` (tests) turns notifications off.

Tests: `windows_run.py` gained the tray phase, the counterpart of
`overlay_tray.py`, with posted messages only (no keyboard or pointer, no
foreground change, no notifications), on a daemon with
`ctrl+alt+shift+F12` and a test-only `Run` value (`GLYPHSHIFT_AUTOSTART_VALUE`):
the icon is present (`Shell_NotifyIconGetRect`); after the test removes it
from the taskbar (`NIM_DELETE` with the daemon's window and id, what an
Explorer restart does) and posts `TaskbarCreated`, it is present again; a
second launch with stderr to a file exits 0 and the running daemon logs its
reminder; the menu, opened by posting the icon's callback message, is read
through `MN_GETHMENU` (labels, grayed and checked states) and driven with
posted Down/Return/Escape: the hotkey row, "No game attached" and the style
heading grayed, "Each game's saved style" checked; choosing Comic changes the
style for new attachments and moves the check; "Start at login" writes
`PATH\glyphshift-overlay.exe --hotkey ctrl+alt+shift+F12 --preset art-comic`
and, chosen again, removes it; Escape closes the menu without a choice; Quit
ends the daemon with status 0 and its icon window is gone. It is skipped when
another daemon runs. With the `TaskbarCreated` handling removed, the phase
failed with "the icon was not added again".

Verification: both Windows presets built without warnings and passed 12/12
CTest tests, `smoke-windows` included (about 68 s, fixture, overlay,
tracking, editor, daemon, tray, oracles); the format check passed. A scratch
probe without the test variables showed the three notifications in screen
captures (toasts titled with the program name and the icon's "G"), and the
daemon with a hotkey held by another process exited 1 within 7 s, once its
notification was hidden. Here a new icon goes to the taskbar's overflow area;
`Shell_NotifyIconGetRect` still reports it. Not verified: opening the menu
with a real click and closing it by clicking elsewhere (the user's pointer),
"Detach from" (it needs an attachment by the real hotkey, so it belongs to
the `overlay_daemon` counterpart), a real Explorer restart, the icon at
other DPIs, and notifications under Do Not Disturb. After the user pushed
`6aaddd5`, the CI's Linux `build` job passed (seen by the user).

**Step 7, part 7: the focused daemon driver with real input (2026-10-21).**
The first `SendInput` driver of the test plan, the counterpart of
`overlay_daemon.py`, run with the user's permission while they did not use
the keyboard and pointer.

- `tests/smoke/real_input_win32.py`, the counterpart of `real_input.py`:
  refuses to start while a modifier or mouse button is held; brings the test
  window to the foreground (a no-op relative mouse move first, so this
  process sent the latest input and Windows lets it set the foreground);
  checks before every key and click that the foreground is a test window, and
  that a click lands on a test window or the input-transparent overlay; keys
  go with their scan codes, modifiers pressed in order and released in
  reverse; at the end it gives the foreground back to the window that had it
  (unless the user has switched elsewhere) and moves the pointer back.
- `tests/smoke/overlay_daemon_win32.py`: the daemon (default Ctrl+Alt+G,
  `--preset art-comic`, notifications off) and the Win32 Vulkan fixture with
  validation, started on its own and focused. Unattached, 0.000% of the
  fixture's client area differed on screen from its own pixels; the real
  Ctrl+Alt+G started one overlay process (a child of the daemon, found
  through a process snapshot) and 59.9% then differed; with the overlay
  attached a real `A` and a left click reached the fixture in order (its
  input log) while it kept the foreground; Ctrl+Alt+G detached (the screen
  matched the fixture again, under 1%) and attached again. The icon's menu,
  opened through its callback message as Explorer does after a click (the
  test allows the daemon to take the foreground), took the foreground and
  listed "Detach from GlyphShift Vulkan fixture …"; a real click on the game
  closed it without detaching; opened again, real Down presses to that row
  and Return detached the game and the screen returned. The hotkey then
  attached once more, closing the fixture (status 0, no validation message)
  ended its overlay while the daemon kept running, and the daemon's stop
  event ended it with status 0.
- `win32.py` gained `children(pid)` (process snapshot).

Verification: the driver passed twice in a row on the Release build (about 20
s each); afterwards the foreground was the user's editor again and no
GlyphShift process was left. The Debug smoke's daemon and tray phases passed
again with the extended `win32.py`; the format check passed. Not verified: a
real click on the icon itself (on this machine it sits in the taskbar's
overflow area, so the menu was opened with the callback message Explorer
sends), and the hotkey on a real game (the `overlay_game` counterpart).

**Step 7, part 8: the focused editor driver with real input (2026-10-21).**
`tests/smoke/editor_apps_win32.py`, the counterpart of
`editor_apps.py --application overlay-fixture` (with `--illustrated` and
`--presets`), run with the user's permission: the Win32 Vulkan fixture with
validation and the overlay attached (live config `color_mode=mono`,
`glyph_strength=1`; Vulkan validation in the overlay), driven with
`SendInput` at the same panel coordinates as on Linux: Insert opens the
editor and it takes the foreground; a Ctrl-click numeric edit sets cell width
21; the binary set is chosen and applied in the characters section; Ctrl+A
and typing name the preset `app-editor`, saved with `cell_width=21` and
`character_set=binary`; Escape closes the editor and returns the foreground
to the fixture; F10 shows the original and back; with the editor open the
fixture is resized three times; moving the foreground to a test-owned window
closes the editor; the panel's close button closes it; then a real `A` and a
click are exactly the fixture's only input events (nothing typed or clicked in
the panel and no hotkey reached it), Escape ends the fixture with status 0
and the overlay with it, without a validation or editor-input message.
`real_input_win32.py` gained key down/up, text typed through the layout
(`VkKeyScanW`), and the test-owned sentinel window.

It found a defect the posted-message smoke could not: with Control really
held, `ToUnicode` returns a control code for a letter (Ctrl+A gives U+0001),
and the editor names shortcut keys by their text, so Ctrl+A (and Ctrl+C, V,
X, Z) did nothing in text fields; the preset name became `defaultapp-editor`
and no file was saved (reproduced twice). The Win32 editor input now asks for
the key's text again without Control when the first answer is a control code
(as X11 keysyms ignore Control); AltGr text, which is not a control code, is
unchanged. The smoke's posted keys carry their text, which is why it passed.

Verification: on the Release build the driver passed twice without options
(once with temporary extra captures), once with `--illustrated` and once with `--presets` (nine included
styles with distinct scene images, copies of the first and last saved and
matching their files, the saved copy selected); the foreground went back to
the user's window and no GlyphShift process was left. Both Windows presets
then built without warnings and passed 12/12 CTest tests and every smoke
phase; the format check passed. Before that, with the user's browser playing
a live video in the foreground, the Release editor smoke phase failed
intermittently (6 of 8 runs in one loop) at different screen comparisons (F10
original, zero strength, the open editor under the point, the restarted
overlay's image), none of which involves Control; afterwards it passed 7 runs
in a row and in the full runs. The cause was not established (the fixture
was on top of the z-order when probed); recorded as an open intermittent
failure of the screen comparisons. Not verified: dead keys and IME in the
editor, and keyboard layouts other than this one.

**Step 7, part 9: a real game, SuperTux for Windows (2026-10-21).** The
first real game under the Windows overlay, with the user's permission; the
counterpart of `overlay_game.py` on native SuperTux.

- `test_env.py` gained its Windows part: the environment lives in
  `%LOCALAPPDATA%\glyphshift-test`; `setup` downloads the pinned
  `SuperTux-v0.7.0-win64-portable.zip` (the archive and checksum the Proton
  test uses), extracts it, copies `welcome_antarctica.stl` as the test level,
  and writes the test profile (`supertux-windows-user`: networking, release
  check, Discord, sound and pausing on focus loss off, windowed 800×600);
  `supertux_windows()` builds the command line. `--offline` is refused on
  Windows (no network namespace); the profile is the offline guarantee, as
  the test plan says. No firewall rule was added.
- `tests/smoke/overlay_game_win32.py`: the daemon (default Ctrl+Alt+G,
  monochrome live config) and SuperTux started on its own on the level,
  driven with `SendInput`. Unattached, 0.62% of the screen over the game (the
  level's title card) was neither black nor white; Ctrl+Alt+G attached one
  overlay and the screen over the game became 0.000% such pixels (glyphs
  only); after the introduction, holding Right for 2.5 s changed 45% of the
  game's own image (idle: 0%) while the screen stayed glyphs only; Insert
  opened the editor over the game and Escape closed it, the foreground going
  back to the game; resized to 640×480, the overlay window matched the game's
  client area; Ctrl+Alt+G detached (the original image returned) and attached
  again; closing the game (status 0) ended its overlay, the daemon kept
  running and stopped with status 0 on request. Captures of the glyph image
  over the level and of the editor over the game were inspected.
- Two driver defects found on the way: `SendInput` arrows without
  `KEYEVENTF_EXTENDEDKEY` reached SDL as numeric-keypad keys (the scan code
  without its extended prefix), so Tux did not walk; `real_input_win32.py`
  now marks the extended keys (arrows, Insert, Delete, Home, End, Page
  Up/Down, right Control and Alt, Windows keys). And the daemon ignores a
  hotkey press within 400 ms of the previous one, which a quick detach then
  attach hit; both hotkey drivers now wait 0.5 s before each press.

Verification: `overlay_game_win32.py` passed twice on the Release build;
`overlay_daemon_win32.py` and `editor_apps_win32.py` passed again with the
changed input module. Not verified: an offline Steam game (next), SuperTux
in borderless fullscreen, and latency and GPU cost (the overlay on Windows
has no GPU timing yet).

**Step 7, part 10: ELDEN RING, and the overlay topmost while the game is in
front (2026-10-21).** For the second real game the test plan proposed an
offline Steam game without anti-cheat. The user chose ELDEN RING, which ships
with Easy Anti-Cheat, reasoning that the capture overlay never enters the game
process (Windows.Graphics.Capture and a separate window; no injection, no
hooks), unlike the Linux launcher. Decided by the user on 2026-10-21 for the
capture overlay only, played offline; the exclusions of the injected mode and
of online play are unchanged.

- `overlay_game_win32.py` gained `--running EXE`: it attaches to a game the
  user started and left in front (windowed or borderless), and neither
  resizes nor closes it; it detaches at the end and the game keeps running.
  `--hold-key` names the input that must reach the game: a letter, Right, or
  `Mouse`, a relative pointer motion that turns a 3D camera
  (`RealInput.turn`). The game's reaction is now measured on a 32×18 grid of
  mean brightness (more than 6 levels counts as changed), since counting
  pixels that change by more than 40 levels missed a camera turn in a dark
  scene full of embers; it must exceed 30% and three times the idle change.
  On a failed editor check the driver saves the screen and prints the
  foreground window, the overlay window's state and the top of the z-order.
  `win32.py` gained `processes(exe)`, `main_window(pid)` and `window_title`.
- Runs on ELDEN RING (Direct3D 12, borderless 1920×1080, offline): Ctrl+Alt+G
  attached and the whole screen became glyphs only (0.000% neither black nor
  white, from about 94–99% unattached). Holding L (the camera key on default
  bindings) did not turn the camera (9% against 18% idle); the pointer motion
  did, by 55–83% against 12–20% idle, with the compass moving from N to E/S.
  Insert opened the editor over the game and the overlay took the foreground.
  Escape closed the editor (the game's menu did not open), but the screen
  then showed the game unchanged for 15 s: the dump showed the overlay alive
  and visible but below ELDEN RING, the foreground, in the z-order.
- The fix (`overlay_window.cpp`): while the target or the open editor is the
  foreground window, the overlay is placed in the topmost band, as the M7
  description asks ("a topmost, click-through window"); otherwise it stays
  directly above the target in the target's band, as part 3 did, so another
  application in front still covers both. The likely cause is that the game
  raises its own window when it gets the foreground back, beating the
  overlay's restacking at its level; that cause was inferred, not traced.
- With the fix, the user ran the overlay on ELDEN RING by hand: the effect
  stayed on top after opening and closing the editor, and Ctrl+Alt+G
  detached and attached again ("funcionou perfeito"). A capture the user
  shared showed the scene in green glyphs.

Verification: both Windows presets built without warnings and passed 12/12
CTest tests and every smoke phase (tracking included); the format check
passed. Not verified: the focused drivers with real input after the fix
(`overlay_game_win32.py` on SuperTux and on ELDEN RING, `overlay_daemon_win32.py`,
`editor_apps_win32.py`); they were not rerun before this commit. The user
noted that distant detail is lost in 3D scenes; recorded as a candidate goal
after M7, not started.

**Step 7, part 11: GlyphShift as a Windows application (2026-10-22).** At the
user's request ("open it as an app, not from the terminal"), the counterpart
of M6 step 5's packaging:

- `GlyphShift.exe` (`apps/overlay/launcher_win32.cpp`, GUI subsystem): starts
  `glyphshift-overlay.exe` from its own directory with the same arguments and
  `CREATE_NO_WINDOW`, then exits; a failure to start it is shown in a message
  box. Windows gives a console program launched from Explorer or the Start
  menu a console window (with Windows Terminal as the default terminal, a
  terminal window); the launcher avoids it. `glyphshift-overlay.exe` is
  unchanged for terminal use.
- Start at sign-in (the icon's menu and `--autostart on`) now writes the
  `Run` value for `GlyphShift.exe` when it sits next to the overlay
  (`apps/overlay/background_win32.hpp`), else for the overlay as before.
- Resources (`apps/overlay/glyphshift.rc.in`, both programs): the application
  icon and version information, whose description "GlyphShift" names the
  application in notifications (before: "glyphshift-overlay.exe"). The icon,
  `data/glyphshift.ico`, is written by `tools/windows_icon.py` from
  `data/glyphshift.svg` (its rounded rectangles, solid fills and per-shape
  diagonal gradient rasterized with 4×4 supersampling; PNG images of 16–256
  px in the ICO); the notification-area icon now loads it, and falls back to
  the drawn one. The MSVC warning and conformance options are now C++-only,
  since the resource compiler takes none of them.
- Installation (`cmake --install`, Windows): both programs, the DLLs vcpkg
  copies next to them, the license notices under `licenses/`, and a Start
  menu shortcut `GlyphShift.lnk` to the installed launcher, written by
  `cmake/install_shortcut.cmake.in` through PowerShell's `WScript.Shell`
  (`GLYPHSHIFT_SHORTCUT_DIR` names another directory, `none` skips it). For
  one user: `--prefix "$env:LOCALAPPDATA\Programs\GlyphShift"`. There is no
  uninstall step; the README says what to delete.

Tests: the new CTest `install` on Windows (`tests/unit/install_windows_test.py`,
the counterpart of the Linux `install` test) installs into a temporary prefix
with a space, with the shortcut in a temporary directory, and checks the two
programs, FreeType's DLL (`freetyped.dll` in Debug), both license notices, an
icon resource and the "GlyphShift" description in both programs, the
shortcut's target; that the installed overlay loads and rejects bad arguments
with usage; that `--autostart on` writes a test-only `Run` value naming the
installed `GlyphShift.exe` and `off` removes it; and that the installed
launcher returns at once, starts the background overlay (its daemon window
found by hotkey), opens no console or terminal window, and that the daemon
stops with its stop event. The smoke's tray phase now expects `GlyphShift.exe`
in the `Run` value.

Verification: both Windows presets built without warnings and passed 13/13
CTest tests and every smoke phase; the format check passed. Two earlier full
runs in this step failed: the Debug `install` test on the DLL name (fixed),
and screen and z-order checks of the tracking and editor phases while the
user was using the desktop (a video playing in a browser, windows changing);
those phases passed when rerun alone and in the final full run. The user's
own build was installed with that command; the Start menu shortcut was
created. Not verified: starting from the Start menu by a click, and start at
sign-in by Windows itself.

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
