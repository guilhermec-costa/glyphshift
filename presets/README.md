# Ready-to-use styles

Run commands from the repository root. These configs work with the current
illustrated renderer; no additional fonts or downloads are needed beyond the
project's normal monospace font requirement.

| Config | Style | Cell size | Glyph strength | Preview |
| --- | --- | --- | --- | --- |
| [art-soft-color.conf](art-soft-color.conf) | Gentle color treatment with original detail blended in; start here for gameplay. | 6×10 | 0.65 | [Image](../docs/presets/art-soft-color.png) |
| [art-newsprint.conf](art-newsprint.conf) | Black ink, warm white paper, letter shading and block silhouettes. | 8×12 | 1 | [Image](../docs/presets/art-newsprint.png) |
| [art-green-terminal.conf](art-green-terminal.conf) | Green terminal letters and structural blocks on a dark background. | 8×12 | 1 | [Image](../docs/presets/art-green-terminal.png) |
| [art-blueprint.conf](art-blueprint.conf) | Cyan directional outlines on navy paper, with surface fill disabled. | 6×10 | 1 | [Image](../docs/presets/art-blueprint.png) |
| [art-amber-braille.conf](art-amber-braille.conf) | Amber Braille dots on dark paper; deliberately dense and textured. | 6×10 | 1 | [Image](../docs/presets/art-amber-braille.png) |
| [art-mosaic.conf](art-mosaic.conf) | Source-colored quadrant blocks for a geometric pixel mosaic. | 8×12 | 1 | [Image](../docs/presets/art-mosaic.png) |

![Six styles rendered from the same SuperTux frame](../docs/presets/overview.png)

These are GPU previews of the same [captured frame](../docs/illustration-original.png),
not six new live gameplay tests. Except for Soft color, the styles use strength 1:
all visible detail comes from cell palettes and glyph masks. Blueprint deliberately
omits most surface detail; Mosaic deliberately uses blocks instead of letters.

## Try a style

For the native demo:

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env GLYPHSHIFT_CONFIG="$PWD/presets/art-newsprint.conf" \
  ./build/dev/glyphshift -- ./build/dev/glyphshift-glx-fixture
```

For SuperTux, use the complete [offline launch command](../tests/README.md#windows-supertux-through-ge-proton-without-steam)
and change its `GLYPHSHIFT_CONFIG` path to the selected config above.
Keep the offline wrapper and other Proton arguments from that command.
Press **F10** to compare with the original scene and **Insert** to adjust the style.

## Switch styles without restarting

First prepare one live config:

```bash
cp presets/art-soft-color.conf /tmp/glyphshift-style.conf
```

Launch the demo using that file:

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env GLYPHSHIFT_CONFIG=/tmp/glyphshift-style.conf \
  ./build/dev/glyphshift -- ./build/dev/glyphshift-glx-fixture
```

For SuperTux, set `GLYPHSHIFT_CONFIG=/tmp/glyphshift-style.conf` in its complete
launch command instead. Once the application is running, copy another style
onto the same live file from a second terminal:

```bash
cp presets/art-newsprint.conf /tmp/glyphshift-style.conf
cp presets/art-green-terminal.conf /tmp/glyphshift-style.conf
cp presets/art-mosaic.conf /tmp/glyphshift-style.conf
```

Run one copy command at a time and inspect the result. GlyphShift reloads the
file automatically; the tracked presets stay unchanged. This switches settings,
so it does not require a rebuild or application restart if the current libraries
are already loaded.

## Keep a favorite in the editor

The supplied files are live configs, not automatically installed entries in the
editor's preset dropdown. Load one, open the editor, give it a name and click
**Save preset**. Repeat for other favorites, then launch the same `--profile`
without `GLYPHSHIFT_CONFIG` to use saved presets through the dropdown. An external
live config takes precedence at startup and whenever that file changes.

The existing [illustrated](illustrated.conf), [ink](illustrated-ink.conf), and
[coarse blocks](illustrated-blocks.conf) configs remain available too.
