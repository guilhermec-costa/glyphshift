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

These examples use the editable `presets/readable.conf` preset. It may have
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

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env \
  GLYPHSHIFT_CONFIG="$PWD/presets/readable.conf" \
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

The wrapper blocks IPv4/IPv6 socket creation in the process and its children,
while allowing Unix/X11 sockets. It does not isolate the filesystem. If the
wrapper is missing, restore the test environment files before running the game;
do not remove it from the command as a workaround.

**Insert and the GlyphShift hotkeys have not been validated under Wine; Insert
did not open the editor in the tested SuperTux setup.** Adjust the file specified
by `GLYPHSHIFT_CONFIG` instead. Edit and save it while the game is running;
changes apply live without restarting the game.

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

The supplied presets are [readable](../presets/readable.conf),
[mono-readable](../presets/mono-readable.conf), and
[classic](../presets/classic.conf). To start with a different preset, change the
`GLYPHSHIFT_CONFIG` path before running the command.

Earlier tests covered the menu and a level introduction; character movement
during gameplay was not validated. A later run confirmed live style switching,
but the game refused the requested resize. That run does not count as a complete
resize/clean-exit pass. This test does not establish general Proton game
compatibility.

## Character sets, custom sequences, and fonts

In either native fixture, open **Insert → Characters and font**. Choose
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
