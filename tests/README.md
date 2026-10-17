# Testing GlyphShift

This guide covers the fixtures, visual adjustments, and SuperTux through Proton.
The [main README](../README.md) remains the reference for scope, dependencies,
and validation results.

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

If the layer is in an external directory, add `VK_LAYER_PATH` to `env`.
In this session, it was extracted to
`/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d`.
`--validation` fails explicitly if the layer is unavailable.

## Windows SuperTux through GE-Proton, without Steam

This command uses portable files **already prepared on this machine**:

- GE-Proton11-9 at `/tmp/glyphshift-m5-apps/GE-Proton11-9-x86_64`;
- SuperTux 0.7.0 for Windows at `/tmp/glyphshift-m5-apps/supertux/SuperTux-portable`;
- Proton prefix at `/tmp/glyphshift-m5-proton-prefix`;
- empty Steam directory at `/tmp/glyphshift-m5-steam-empty`;
- game test data at `/tmp/glyphshift-m5-supertux-user`;
- network-blocking wrapper at `/tmp/glyphshift-offline`.

These files are not part of the checkout and may disappear after a reboot or
cleanup of `/tmp`. The command does not download or install Steam, Proton, or
the game. The SuperTux test configuration was prepared with networking,
update checks, and Discord integration disabled. Use only this offline run,
with no active anti-cheat or online services.

The command below starts with **Illustrated rendering**, 8×12 cells, glyph
strength 1, and contours over fill, using `presets/illustrated.conf`.

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env \
  GLYPHSHIFT_CONFIG="$PWD/presets/illustrated.conf" \
  STEAM_COMPAT_DATA_PATH=/tmp/glyphshift-m5-proton-prefix \
  STEAM_COMPAT_CLIENT_INSTALL_PATH=/tmp/glyphshift-m5-steam-empty \
  PROTONFIXES_DISABLE=1 \
  WINEDEBUG=-all \
  SteamGameId=0 \
  /tmp/glyphshift-offline \
  ./build/dev/glyphshift --profile proton-supertux -- \
  /usr/bin/python3 /tmp/glyphshift-m5-apps/GE-Proton11-9-x86_64/proton run \
  /tmp/glyphshift-m5-apps/supertux/SuperTux-portable/bin/supertux2.exe \
  --userdir 'Z:\tmp\glyphshift-m5-supertux-user' \
  --window --geometry 800x600 --disable-sound --disable-music
```

Close any running SuperTux instance and relaunch with this command to select
the illustrated config. In **Insert → Illustration**, confirm that
**Illustrated rendering** and **Contours over fill** are checked. Press
**F10** to compare against the original scene, preferably inside a level.
For dark ink on warm paper, change the config path to
`$PWD/presets/illustrated-ink.conf`; for coarser text and blocks, use
`$PWD/presets/illustrated-blocks.conf`. To return to the earlier classic
rendering example, use `$PWD/presets/readable.conf`.

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
TTF/OTF path; empty uses system monospace (classic keeps its built-in bitmap).
Press **Apply characters**, then **Save preset** if you want to keep the result.
Changing the draft alone does not apply or save it.

For SuperTux, add these keys to the file already used by GLYPHSHIFT_CONFIG:

```ini
character_set=custom
characters=" .01ABCé░▒▓█"
glyph_order=density
glyph_reverse=false
glyph_font=""
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

Use the `setpriv --inh-caps=-all --ambient-caps=-all --` prefix shown in this
guide. It removes inheritable and ambient capabilities only for the child
execution, without changing the terminal or system settings. The launcher still
refuses root and privileged processes; do not use sudo to address this error.

### Display unavailable

Run from a terminal in the X11 session with `DISPLAY` and X11 authorization
available. Native Wayland input is not supported. Smoke tests return skip code
77 when they cannot access the display; a skip does not establish rendering
coverage.

### Game or wrapper not found

Check the `/tmp` paths listed in the SuperTux section. They belong to the test
environment prepared during development and are not installed by CMake.
The fixtures can be tested independently of these files.

## Automated checks

CPU, configuration, preset, launcher, and interposition tests:

```bash
ctest --preset dev
```

Sequential graphics smoke tests, with X11/GPU access and the Khronos layer
available:

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  cmake --build --preset dev --target smoke
```

With this machine's temporary validation layer, use:

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env VK_LAYER_PATH=/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d \
  cmake --build --preset dev --target smoke
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

If the Khronos layer is not installed system-wide, insert the same
`env VK_LAYER_PATH=...` shown above before `ctest`.

Observed results (historical runs and the character-selection suite: 11 Debug
tests, 13 Release tests, Mesa GLX, and Mesa
Vulkan/Proton limitations) are recorded in the [main README](../README.md).
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

The Vulkan fixture needs the validation layer, including VK_LAYER_PATH when
using the temporary extraction. Use the same capability-dropping prefix as
above if required by your environment. Both fixtures resolve their event
functions privately in this test, so the legacy Xlib hooks cannot supply editor
input. The driver checks mouse/keyboard edits, character-set selection, saved
preset contents, hotkeys,
resize while open, focus loss, title-bar close, ordinary host input restoration
and clean exit. Captures and application logs are saved under `--output`.

With the M5 applications still available, `--application glxgears` and
`--application vkcube` exercise real native applications. To test the authorized
Windows SuperTux offline, preserving the existing network restriction:

```bash
setpriv --inh-caps=-all -- /tmp/glyphshift-offline /usr/bin/python3 \
  tests/smoke/editor_apps.py --build build/dev --application supertux \
  --output /tmp/glyphshift-editor-supertux
```

These tests require the `/tmp` application paths documented above; they do not
download or install dependencies. They are opt-in because they use desktop focus.

Add `--level` to the SuperTux test to use the previously prepared test level.
After checking the editor, it sends Enter to advance the introduction, verifies
that the colored world appears, and captures the reopened panel with source
color, 6×10 cells and strength 0.35. This also checks that keyboard input returns
to Wine. The [recorded live capture](../docs/editor-supertux-play.png) shows that
running level; it is not a fixed-image preview or a gameplay performance test.

## Illustrated glyph drawing

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
**Apply contours**. Use **Save preset** to retain the result. Existing fill
sets/custom characters/fonts continue to work independently.

The old mode remains available as `render_style=classic`. At 4×4, filtering
preserves average ink, but letters may still be too small to recognize. The
new mode currently uses spatial filtering, not temporal hysteresis.

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
setpriv --inh-caps=-all -- /tmp/glyphshift-offline /usr/bin/python3 \
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
pbuffer limitation are recorded in the main README's illustrated-renderer
verification section.
