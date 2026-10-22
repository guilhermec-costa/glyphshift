# Testing GlyphShift

This guide covers the fixtures, visual adjustments, and SuperTux through Proton.
The [main README](../README.md) introduces the software and getting started.
The [implementation reference](../docs/implementation/README.md) records scope,
architecture, remaining work and validation results.

Run the following commands **from the repository root**, in an X11 session,
as a regular user and **without sudo**.

## Build

```bash
cmake --preset dev
cmake --build --preset dev
```

The fixture examples use the editable `presets/readable.conf` preset. It may have
been modified during earlier tests; its current values will be used. To
experiment without modifying a tracked file, copy it to
`/tmp/glyphshift-manual.conf` and use that path in `GLYPHSHIFT_CONFIG`.

## OpenGL/GLX fixture: editor and visual comparison

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env GLYPHSHIFT_CONFIG="$PWD/presets/readable.conf" \
  ./build/dev/glyphshift -- ./build/dev/glyphshift-glx-fixture
```

The window displays an animated gradient transformed into glyphs.

| Control | Expected result |
| --- | --- |
| Insert | Open or close the editor. |
| Glyph strength | Adjust intensity from 0 to 1 live. Ctrl-click to enter a value. |
| Cell width / Cell height | Adjust cell dimensions independently. |
| Monochrome | Switch between source color and monochrome. |
| F10 | Toggle the effect to compare with the original image. |
| Escape with the editor open | Close only the editor and return input to the fixture. |
| Escape with the editor closed | Exit the fixture. |

Try strengths of `0`, `0.25`, `0.45`, `0.75`, and `1`. Resize the window and
confirm that the effect and editor still work. On i3, make the window floating
before testing specific dimensions.

To test persistence, adjust the controls, name the preset, and click
**Save preset**. Close the fixture and reopen it **without the external config
file** to check the saved values:

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env -u GLYPHSHIFT_CONFIG \
  ./build/dev/glyphshift -- ./build/dev/glyphshift-glx-fixture
```

The selected preset and saved values should be restored. A file specified by
`GLYPHSHIFT_CONFIG` takes precedence at startup and whenever its contents change;
unset that variable when checking preset persistence.

## Vulkan fixture

Use the same controls and visual comparison procedure for Vulkan:

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env GLYPHSHIFT_CONFIG="$PWD/presets/readable.conf" \
  ./build/dev/glyphshift -- ./build/dev/glyphshift-vulkan-fixture
```

With the Khronos validation layer installed and available to the loader,
enable validation:

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env GLYPHSHIFT_CONFIG="$PWD/presets/readable.conf" \
  VK_LAYER_VALIDATE_SYNC=1 \
  ./build/dev/glyphshift -- ./build/dev/glyphshift-vulkan-fixture --validation
```

If the layer is not installed system-wide, use the one kept by the
[persistent test environment](#persistent-test-environment): prefix the command
with `python3 tests/smoke/test_env.py run --`, which adds it to `VK_LAYER_PATH`.
`--validation` fails explicitly if the layer is unavailable.

## Persistent test environment

The game and validation tests use files that are not part of the checkout:
GE-Proton11-9, SuperTux 0.7.0 for Windows, the Khronos validation layer
1.4.363, glxgears and vkcube. One command downloads pinned, checksummed copies
once and keeps them, a Proton prefix and SuperTux test profiles in
`~/.local/share/glyphshift-test` (or `$GLYPHSHIFT_TEST_ENV`), so they survive
reboots. Nothing is installed system-wide and Steam is not used. Running it
again only checks what is already there.

```bash
python3 tests/smoke/test_env.py setup
python3 tests/smoke/test_env.py status
```

`test_env.py run -- COMMAND` runs a command with the validation layer added to
`VK_LAYER_PATH`. `test_env.py run --offline -- COMMAND` also runs it in a new
user and network namespace with no network interfaces (`unshare --user
--map-current-user --net`; X11 works through its Unix socket). Proton is only
started that way, and the test drivers refuse to start it when the network is
reachable. The SuperTux test profiles disable networking, release checks,
Discord integration, audio and pausing on focus loss; they are separate from
your own SuperTux profile.

## Native SuperTux (Arch package)

The simplest real-game test: install `supertux` and launch the native build.

```bash
./build/dev/glyphshift --profile supertux -- /usr/bin/supertux2
```

Press **Insert** for the editor and **F10** to compare. SuperTux presents
through SDL; add `--renderer opengl` or `--renderer sdl` to try both of its
video paths. If nothing changes, run with `GLYPHSHIFT_LOG=1` and look for
`SDL ... presentation interposed` in the output.

## Windows SuperTux through GE-Proton, without Steam

This uses the [persistent test environment](#persistent-test-environment), with
no network access. Use only this offline run, with no active anti-cheat or
online services.

The command below starts with **Illustrated rendering**, 6×10 cells, glyph
strength 1, scene-colored ink on light paper, outline shape glyphs and a smooth
paper wash, using `presets/illustrated.conf`.

```bash
python3 tests/smoke/test_env.py run --offline -- env \
  GLYPHSHIFT_CONFIG="$PWD/presets/illustrated.conf" \
  STEAM_COMPAT_DATA_PATH="$HOME/.local/share/glyphshift-test/proton-prefix" \
  STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.local/share/glyphshift-test/steam-empty" \
  PROTONFIXES_DISABLE=1 WINEDEBUG=-all SteamGameId=0 \
  ./build/dev/glyphshift --profile proton-supertux -- \
  /usr/bin/python3 "$HOME/.local/share/glyphshift-test/GE-Proton11-9-x86_64/proton" run \
  "$HOME/.local/share/glyphshift-test/SuperTux-portable/bin/supertux2.exe" \
  --userdir 'Z:\home\YOU\.local\share\glyphshift-test\supertux-proton-user' \
  --window --geometry 800x600 --disable-sound --disable-music
```

Replace `YOU` with your user name in the Windows-style `--userdir` path.

Close any running SuperTux instance and relaunch with this command to select
the illustrated config. In **Insert → Illustration**, confirm that
**Illustrated rendering** and **Contours over fill** are checked. Press
**F10** to compare against the original scene, preferably inside a level.
For dark ink on warm paper, change the config path to
`$PWD/presets/illustrated-ink.conf`; for coarser text and blocks, use
`$PWD/presets/illustrated-blocks.conf`. To return to the earlier classic
rendering example, use `$PWD/presets/readable.conf`.

For nine finished styles, see the [preset gallery](../presets/README.md).
After restarting through the rebuilt launcher, use **Insert → Preset → Included
styles** to switch them directly over the running game. Hover a style for its
description. **Save preset** stores edits as a personal copy; saved names remain
available in the same menu. To restore the last choice on a later launch, remove
the `GLYPHSHIFT_CONFIG` line from the command and omit any explicit `--preset`.

External-file switching is still available as an alternative. First copy a style
to a live file:

```bash
cp presets/art-comic.conf /tmp/glyphshift-style.conf
```

Set `GLYPHSHIFT_CONFIG=/tmp/glyphshift-style.conf` in the complete launch command
above and start the game. In a second terminal, run one of these commands at a
time to switch the running effect:

```bash
cp presets/art-sepia.conf /tmp/glyphshift-style.conf
cp presets/art-newsprint.conf /tmp/glyphshift-style.conf
cp presets/art-chalk.conf /tmp/glyphshift-style.conf
cp presets/art-ink-edges.conf /tmp/glyphshift-style.conf
cp presets/art-blueprint.conf /tmp/glyphshift-style.conf
cp presets/art-midnight.conf /tmp/glyphshift-style.conf
cp presets/art-crt.conf /tmp/glyphshift-style.conf
cp presets/art-digital-rain.conf /tmp/glyphshift-style.conf
cp presets/art-comic.conf /tmp/glyphshift-style.conf
```

These copies update only the temporary live config. The nine included styles
already appear in the dropdown; **Save preset** adds your own named variation.

The wrapper blocks IPv4/IPv6 socket creation in the process and its children,
while allowing Unix/X11 sockets. It does not isolate the filesystem. If the
wrapper is missing, restore the test environment files before running the game;
do not remove it from the command as a workaround.

After rebuilding, restart SuperTux through the launcher above and focus its
window. **Insert** opens the GlyphShift editor; mouse and keyboard edit values,
**Save preset** persists them, and **Escape**, **Insert** or the title-bar close
button returns input to the game. **F10** toggles the effect. This uses the new
window-scoped X11 input path; older builds from M5 did not support the panel
in this Wine setup.

If a hotkey conflicts with another X11 grab, stderr reports an editor-input
diagnostic. The optional file specified by `GLYPHSHIFT_CONFIG` also remains
usable: edit and save it while the game runs to apply changes without restart.

For example:

```ini
enabled=true
cell_width=6
cell_height=10
color_mode=source
glyph_strength=0.45
```

| Value | What to observe |
| --- | --- |
| `glyph_strength=0` | Original scene in source mode; grayscale in mono mode. |
| `glyph_strength=0.25` or `0.45` | More scene detail preserved alongside glyphs. |
| `glyph_strength=0.75` | Stronger glyph presence. |
| `glyph_strength=1` | Pure glyphs; classic is binary in mono mode, while antialiased fonts can have gray edges. |
| `color_mode=source` / `mono` | Switch between source color and monochrome. |
| `enabled=false` | Original image, regardless of color mode. |

The illustrated starting points are [color](../presets/illustrated.conf),
[blocks](../presets/illustrated-blocks.conf), and
[ink](../presets/illustrated-ink.conf). Earlier classic-mode presets are
[readable](../presets/readable.conf),
[mono-readable](../presets/mono-readable.conf), and
[classic](../presets/classic.conf). To start with a different preset, change the
`GLYPHSHIFT_CONFIG` path before running the command.

Earlier tests covered the menu and a level introduction; character movement
during gameplay was not validated. A later run confirmed live style switching,
but the game refused the requested resize. That run does not count as a complete
resize/clean-exit pass. This test does not establish general Proton game
compatibility.

## Character sets, custom sequences, and fonts

In either native fixture or the tested SuperTux/Proton setup, open
**Insert → Characters and font**. Choose
classic, ASCII, alphanumeric, binary, blocks, Braille, or custom. For custom,
enter the sequence in dark-to-light order or enable **Sort by ink density**.
Use **Reverse dark / light** to invert the order. Optionally enter an absolute
TTF/OTF path; empty prefers a bold DejaVu Sans Mono, Hack or similar face, then
the system monospace (classic keeps its built-in bitmap).
Press **Apply characters**, then **Save preset** if you want to keep the result.
Changing the draft alone does not apply or save it. **Glyph weight**, below the
Apply button, thickens font strokes and applies when you release the slider.
In a font set, check that `_` and `.` sit on the cell floor while `'` and
`` ` `` stay near the top.

For SuperTux, add these keys to the file already used by GLYPHSHIFT_CONFIG:

```ini
character_set=custom
characters=" .01ABCé░▒▓█"
glyph_order=density
glyph_reverse=false
glyph_font=""
glyph_weight=0.3
```

Save the file while the game is running. Try character_set values `ascii`,
`blocks`, `braille`, and `binary`, or keep `custom` and edit the sequence. Quotes
preserve spaces and literal `#`; escape backslashes as `\\` and quotes as `\"`.
Manual order is dark-to-light; density order is measured from the actual glyph
coverage. All choices are part of named presets.

You can also point GLYPHSHIFT_CONFIG at the supplied examples:
[ASCII](../presets/characters-ascii.conf),
[blocks](../presets/characters-blocks.conf),
[Braille](../presets/characters-braille.conf),
[custom](../presets/characters-custom.conf).

Custom sequences accept 1..256 Unicode codepoints, at most 1024 UTF-8 bytes.
Blocks and Braille are built in; other symbols need coverage in the selected
font. Missing fonts/glyphs show a diagnostic and fall back to classic. This
supports individual symbols rather than shaped text, ligatures, or color emoji.
The editor's UI font/input method may not display or enter every symbol;
UTF-8 file editing is the reliable path for those sequences.

Reopen a saved fixture preset without GLYPHSHIFT_CONFIG to verify that its
character set, custom sequence, font path, ordering and reversal survive restart.
The graphics smoke tests also select binary through the editor and save/reload
it, then switch classic/ASCII/binary/blocks/Braille/custom/reverse live and test
missing-font fallback on both APIs.

## Troubleshooting

### `refusing to launch from a process with capabilities`

The launcher now clears inheritable-only capabilities itself (for example the
`cap_wake_alarm` some desktop sessions grant), so the `setpriv` prefix in this
guide is no longer required for that case; it remains harmless. This error now
means the shell has effective or permitted capabilities, which are real
privileges and are always refused. Check with `grep Cap /proc/self/status`,
start the launcher from an ordinary user shell, and do not use sudo.

### Display unavailable

Run from a terminal in the X11 session with `DISPLAY` and X11 authorization
available. Native Wayland input is not supported. Smoke tests return skip code
77 when they cannot access the display; a skip does not establish rendering
coverage.

### Game or validation layer not found

Run `python3 tests/smoke/test_env.py status`, then `setup` for anything
missing. These files belong to the persistent test environment and are not
installed by CMake.
The fixtures can be tested independently of these files.

## Automated checks

CPU, configuration, preset, launcher, and interposition tests:

```bash
ctest --preset dev
```

On Windows, from a Visual Studio developer shell, `ctest --preset windows-dev`
runs the portable ones (configuration, presets, characters, editor, glyph
semantics, the illustration logic and the pre-commit hook); the Linux launcher,
injection, X11, platform and install tests are not built there. The GPU image
oracles (`atlas_filter.py`, `temporal.py`, `exposure.py`, `local_contrast.py`,
`anchor.py`) run
there unchanged against the Vulkan offscreen `glyphshift-preview`; set
`GLYPHSHIFT_PREVIEW_VALIDATION=1` to fail on any Khronos validation message,
and keep `--output` short (for example `build/windows-dev/oracles/NAME`),
since the exposure oracle's command line is long. On Linux the same Vulkan
preview is built as `glyphshift-vulkan-preview`.

The Windows graphics smoke runs the Win32 Vulkan fixture with validation
(gradient, resizes, posted input, clean exits), attaches the capture overlay
to it (its pixels must equal the preview of the captured frame, input must
pass through, and it must end with the fixture), checks that the overlay
tracks a second fixture (moves, resizes, another window raised over it,
raising, minimizing, hiding, closing), drives the editor through it (input
posted to the overlay in the `GLYPHSHIFT_OVERLAY_NO_FOCUS=1` test mode),
checks that only one background daemon runs per session, drives the daemon's
notification-area icon and menu with posted messages, and then runs those
oracles (`--only` selects phases),
without taking the keyboard, pointer or focus:

```powershell
cmake --build --preset windows-dev --target smoke-windows
```

Add `-DGLYPHSHIFT_GPU_SMOKE_TESTS=ON` when configuring to include it in CTest.
It disables the OBS, RivaTuner and Steam implicit Vulkan layers for the
processes it starts, so their on-screen drawing cannot change the captures.

These include `platform` (autostart entries and their quoting) and `install`,
which runs `cmake --install` into temporary prefixes and starts the installed
programs. The overlay smoke, part of the GLX graphics smoke, also checks that
only one background daemon runs per display, without keyboard input; it
reports a skip for that phase if your own daemon is already running. The GLX
graphics smoke also runs `overlay_tray.py`, which plays a tray manager and
checks the tray icon and menu with synthetic events (no XTest); it is skipped
when a real tray manager or your own daemon runs, and opening the menu grabs
pointer and keyboard for a moment.

Sequential graphics smoke tests, with X11/GPU access and the Khronos layer
available:

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  cmake --build --preset dev --target smoke
```

With the validation layer of the persistent test environment, use:

```bash
python3 tests/smoke/test_env.py run -- cmake --build --preset dev --target smoke
```

PPM captures and logs are saved under `build/dev/smoke-evidence/`. The tests cover
input/editor behavior, zero/full/fractional strength, preset persistence across
restart, six window sizes, multiple contexts/swapchains, and unchanged
presentation in supported failure cases. Keep test windows visible and
unobscured during captures.

To include graphics smoke tests in Release CTest:

```bash
cmake --preset release -DGLYPHSHIFT_GPU_SMOKE_TESTS=ON
cmake --build --preset release
setpriv --inh-caps=-all --ambient-caps=-all -- ctest --preset release
```

If the Khronos layer is not installed system-wide, run
`VK_LAYER_VALIDATE_SYNC=1 python3 tests/smoke/test_env.py run -- ctest --preset release`.

Software renderers are slower than the smoke's default waits. To run it on
Mesa in a virtual X server, as the CI does (with `xorg-server-xvfb`, `mesa`,
`vulkan-swrast` and `vulkan-validation-layers` installed):

```bash
Xvfb :99 -screen 0 1920x1080x24 -nolisten tcp &
DISPLAY=:99 GLYPHSHIFT_SMOKE_SETTLE=2.5 LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  cmake --build --preset dev --target smoke-glx
DISPLAY=:99 GLYPHSHIFT_SMOKE_SETTLE=2.5 VK_LAYER_VALIDATE_SYNC=1 \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  cmake --build --preset dev --target smoke-vulkan
```

`GLYPHSHIFT_SMOKE_SETTLE` is the time in seconds the smoke waits for a live
change to reach the screen (default 0.25).

Observed results (historical runs and the character-selection suite: 11 Debug
tests, 13 Release tests, Mesa GLX, and Mesa
Vulkan/Proton limitations) are recorded in the
[implementation reference](../docs/implementation/README.md).
This guide documents how to reproduce the tests; creating it does not constitute
a new run of the graphics tests or the game.

## Focused editor acceptance tests

Unlike the no-focus graphics smoke, this driver temporarily focuses a test
application and sends real XTest keyboard/pointer events. Do not interact with
the desktop during the run. It checks focus before injecting events and restores
focus and pointer when they still belong to the test. Install libXtst if your
system does not already provide it; no Python packages are required.

```bash
python3 tests/smoke/editor_apps.py --build build/dev \
  --application glx-fixture --output /tmp/glyphshift-editor-glx
python3 tests/smoke/editor_apps.py --build build/dev \
  --application vulkan-fixture --output /tmp/glyphshift-editor-vulkan
```

The Vulkan fixture needs the validation layer; prefix the command with
`python3 tests/smoke/test_env.py run --` when it is not installed system-wide. Use the same capability-dropping prefix as
above if required by your environment. Both fixtures resolve their event
functions privately in this test, so the legacy Xlib hooks cannot supply editor
input. The driver checks mouse/keyboard edits, character-set selection, saved
preset contents, hotkeys,
resize while open, focus loss, title-bar close, ordinary host input restoration
and clean exit. Captures and application logs are saved under `--output`.

`--application glxgears` and `--application vkcube` exercise real native
applications from the [persistent test environment](#persistent-test-environment).
To test Windows SuperTux, without network access:

```bash
python3 tests/smoke/test_env.py run --offline -- python3 \
  tests/smoke/editor_apps.py --build build/dev --application supertux \
  --output /tmp/glyphshift-editor-supertux
```

These tests do not download or install anything; run `test_env.py setup` once
first. They are opt-in because they use desktop focus.

The capture overlay has two focused drivers. `--application overlay-fixture`
runs the editor script above on a GLX fixture with the overlay attached.
`overlay_daemon.py` starts the background daemon, presses Ctrl+Alt+G on the
focused fixture to attach, detach and attach again, detaches once more through
the tray menu (against a stand-in tray manager, when no real one runs), then
closes the fixture and stops the daemon. Stop your own daemon first: only one
runs per display, and both use the same hotkey.

```bash
python3 tests/smoke/editor_apps.py --build build/dev \
  --application overlay-fixture --output /tmp/glyphshift-overlay-editor
python3 tests/smoke/overlay_daemon.py --build build/dev \
  --output /tmp/glyphshift-overlay-daemon
```

On Windows, `overlay_daemon_win32.py` is its counterpart with `SendInput`
(`real_input_win32.py`): the real Ctrl+Alt+G attaches the overlay to the
focused Win32 Vulkan fixture, a key and a click reach the fixture through it,
the hotkey detaches and attaches again, the icon's menu (opened through the
icon's callback message) closes on a click on the game and detaches it
through "Detach from"; closing the fixture ends its overlay and the daemon
stops with status 0. It takes the keyboard and pointer for about twenty
seconds and gives the foreground and pointer back. Stop your own daemon
first.

```powershell
python tests/smoke/overlay_daemon_win32.py --build build/windows-release
```

`editor_apps_win32.py` is the Windows counterpart of
`editor_apps.py --application overlay-fixture`, with `SendInput` on the Win32
Vulkan fixture and the overlay attached; `--illustrated` and `--presets` add
the same sections. It takes the keyboard and pointer for about half a minute
(longer with `--presets`).

```powershell
python tests/smoke/editor_apps_win32.py --build build/windows-release [--illustrated] [--presets]
```

`overlay_game_win32.py` is the counterpart of `overlay_game.py` on SuperTux
0.7.0 for Windows, run natively from the test environment
(`%LOCALAPPDATA%\glyphshift-test`, prepared once with `test_env.py setup`,
which downloads the pinned, checksummed portable archive; the test profile
turns networking, release checks and Discord off). Stop your own daemon
first.

```powershell
python tests/smoke/test_env.py setup
python tests/smoke/overlay_game_win32.py --build build/windows-release
```

`overlay_game.py` does the same on a real game started on its own: SuperTux on
a level, natively or through Proton, with a monochrome config. It presses
Ctrl+Alt+G to attach, holds Right to check that the game still receives input
(its own image must change far more than while idle), opens and closes the
editor, resizes the game and checks that the overlay follows, detaches and
attaches again, and closes the game.

```bash
python3 tests/smoke/overlay_game.py --build build/release --application supertux \
  --output /tmp/glyphshift-overlay-supertux
python3 tests/smoke/test_env.py run --offline -- python3 tests/smoke/overlay_game.py \
  --build build/release --application supertux-proton \
  --output /tmp/glyphshift-overlay-proton
```

### Overlay latency and GPU cost

`overlay_measure.py` needs no keyboard or pointer input; keep its windows
visible while it runs. `latency` makes the GLX fixture alternate black and white
frames and polls the composited screen to time each change from the swap that
started it, directly, through the launcher and with the overlay. `cost` runs
native SuperTux on its title screen alone, through the launcher and with the
overlay, with `GLYPHSHIFT_GPU_TIMING=1` and `nvidia-smi` samples.

```bash
python3 tests/smoke/overlay_measure.py latency --build build/release \
  --config presets/art-comic.conf --output /tmp/glyphshift-latency
python3 tests/smoke/overlay_measure.py cost --build build/release \
  --config presets/art-comic.conf --size 1920x1080 --output /tmp/glyphshift-cost
```

`GLYPHSHIFT_GPU_TIMING=1` also works by hand with the launcher or the overlay:
every 600 processed frames it prints the GPU time of GlyphShift's own passes
(capture and glyph passes, not the editor) to stderr.

Add `--level` to the SuperTux test to use the previously prepared test level.
After checking the editor, it sends Enter to advance the introduction, verifies
that the colored world appears, and captures the reopened panel with source
color, 6×10 cells and strength 0.35. This also checks that keyboard input returns
to Wine. The [recorded live capture](../docs/editor-supertux-play.png) shows that
running level; it is not a fixed-image preview or a gameplay performance test.

## Illustrated glyph drawing

The color and ink presets now use 6×10 cells and **Match local shapes** to retain
small silhouettes such as Tux. **Shape threshold** controls the minimum local
contrast: lower values match more texture; higher values reserve shape matching
for stronger features. **Scene-colored paper** reduces black holes in source
color mode; it does not change monochrome ink/paper. **Smooth paper wash**
blends that paper between cell centers instead of tiling it per cell. Strength 1 still draws
only cell palettes and glyph masks, without blending original per-pixel detail.
Partial blocks in the selected set are reserved for silhouette matching rather
than repeated surface shading. Remove them to match using only your text glyphs.
Compare [color](../docs/shape-color.png) and [ink](../docs/shape-ink.png), rendered
from the same [original frame](../docs/illustration-original.png).
The [Tux close-up](../docs/shape-tux-comparison.png) shows the previous 8×12
preset on the left and the revised 6×10 preset on the right, enlarged 3× with
nearest-neighbor scaling. It compares the complete presets, including cell size.

Restart the application through the rebuilt launcher before testing new shared
libraries. For SuperTux, use the existing offline launch command above with
`GLYPHSHIFT_CONFIG="$PWD/presets/illustrated.conf"`. Alternative starting points
are `illustrated-blocks.conf` (text + solid block) and `illustrated-ink.conf`
(dark ink on warm paper).

Open **Insert → Illustration → Illustrated rendering** to switch an existing
session. Try 8×12 or 10×14 cells and strength 1 first, then adjust exposure,
contrast and gamma. Compare **Fill surfaces** on/off and contour strength 0/1.
Raise contour threshold to retain fewer, stronger outlines. **Contours over
fill** keeps the underlying block/character and automatically chooses a
contrasting ink/paper stroke; disable it to replace fill in contour cells. Ink colors the
contours and monochrome fill; Paper sets their background. Contour glyphs are
four UTF-8 characters in vertical/horizontal/slash/backslash order and need
**Apply contour / shape glyphs**. Use **Save preset** to retain the result. Existing fill
sets/custom characters/fonts continue to work independently.

**Shape glyphs** (below Contour glyphs) adds characters used only to draw
silhouettes, never as tonal fill. With **Match local shapes** enabled, try
`/\|_()<>^v`',.-LJ7[]T`, apply it, and compare tree and block outlines.
Partial blocks in the fill set fit hard edges exactly and usually win, so
remove them when you want outlines drawn with text characters. In a config:

```ini
shape_matching=true
shape_characters="/\\|_()<>^v`',.-LJ7[]T"
```

A shape glyph is used only where it closely fits the cell; busy texture keeps
the calm tonal ramp. Newsprint and `illustrated-ink.conf` use
this.

In source color mode, **Ink / contours** sets the ink's brightness while the
scene supplies its hue: white ink glows on dark paper; a dark ink with a light
**Paper** prints dark scene-colored glyphs, and silhouettes invert to match.
Use **Reverse dark / light** for the tonal ramp in that case, as the ink
presets do. **Saturation** (0..2) affects source color only. **Glow / ink
bleed** pulls nearby paper toward the ink of surrounding glyphs: a phosphor
glow on dark paper, a soft bleed on light paper. Compare 0 and 0.5 with F10.

**Pixel-level contours (DoG)** traces outlines from every pixel with a difference
of Gaussians: thin outlines become contours while gradients and fine texture do
not. **DoG threshold** keeps only stronger outlines; **Contour votes** is the
share of a cell's pixels that must agree. All included styles use it.

**Crisp pixel glyphs** turns every pixel into ink or paper, with no gray
antialiasing, for a pixel-art look; most included styles enable it.

```ini
saturation=1.6
glow=0.25
paper_wash=true
crisp_glyphs=true
```

The old mode remains available as `render_style=classic`. At 4×4, filtering
preserves average ink, but letters may still be too small to recognize.

**Temporal stability** (0..1) keeps each cell's previous glyph, silhouette or
contour while it still fits the cell, so noise, compression and flickering
light do not make glyphs boil. Included styles use `stability=0.8`. Compare 0
and 1 while the scene moves; a real change, such as an object moving away,
still updates immediately. Camera scrolling can still make glyphs crawl
across the screen-fixed grid.

The GPU temporal oracle renders frame sequences through `glyphshift-preview`
and checks static identity, flicker suppression and that real changes are not
held; add `--report` to print its measurements only:

```bash
python3 tests/smoke/temporal.py --build build/dev \
  --output /tmp/glyphshift-temporal
```

To render your own sequence, pass extra input/output pairs to the preview tool;
each pair is rendered as the next frame with the previous frame's history:

```bash
./build/dev/glyphshift-preview a.ppm presets/illustrated.conf a-out.ppm \
  b.ppm b-out.ppm c.ppm c-out.ppm
```

The expanded graphics smoke checks the new controls on GLX/Vulkan, including
fill/contour separation, custom Unicode contours, tone/colors, small cells,
source identity at strength 0 and restoring classic output. GLX additionally
runs an independent analytic GPU oracle for half/quadrant block masks at seven
cell sizes, with source-color normalization and colored ink/paper composition:

```bash
python3 tests/smoke/atlas_filter.py --build build/dev \
  --output /tmp/glyphshift-art-filter
```

All 14 cases must differ from the analytic rectangle coverage by at most one
8-bit intensity step per component. Two additional contour-compositing cases
check adaptive light/dark strokes without erasing the filled cell. This test uses the actual shared fragment
shader in the preview tool, with readback confined to developer verification.

To enable illustration using actual mouse input, save it and exercise the
focused editor workflow, add `--illustrated`:

```bash
python3 tests/smoke/editor_apps.py --build build/dev \
  --application glx-fixture --illustrated --output /tmp/glyphshift-art-editor-glx
python3 tests/smoke/test_env.py run --offline -- python3 \
  tests/smoke/editor_apps.py --build build/release --application supertux \
  --level --illustrated --output /tmp/glyphshift-art-supertux
```

Use the validation environment described above for Vulkan. The level test
captures `world-original.ppm` with the effect disabled and `world-styled.ppm`
with the supplied illustrated config, then opens the editor over the running
level. Its original frame can also be fed to `glyphshift-preview` for exact
same-frame style comparisons without further game launches.

Recorded same-frame comparisons: [original](../docs/illustration-original.png),
[classic ASCII](../docs/illustration-classic.png),
[illustrated color](../docs/illustration-color.png),
[blocks](../docs/illustration-blocks.png), [ink](../docs/illustration-ink.png).
These styled images are GPU previews of the same source frame.
The [live editor capture](../docs/illustration-live-editor.png) separately shows
the final mode running through Proton. Full commands/results and the Mesa
pbuffer limitation are recorded in the implementation reference's
[illustrated-renderer verification section](../docs/implementation/README.md#illustrated-renderer-verification-and-comparisons).
