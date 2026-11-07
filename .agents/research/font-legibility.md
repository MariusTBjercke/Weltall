# Font legibility on modern screens

Question: the original message font is hard to read at modern resolutions. What can be changed, and at
what cost? Investigated 2026-11-07 against 1.0.1. No code was changed for this note.

## How text is drawn (verified)

The message font is not a font in the usual sense. It is the PlayStation kernel's bitmap font, and the
port reproduces the kernel exactly:

- A glyph is **11 rows of 12 pixels** (`FMesFont::GlyphColumns` 12, `GlyphRows` 11,
  `Source/WeltallCore/Public/Text/MesFont.h`), stored as 11 `uint16` rows.
- The kernel draws the body plus a 1-pixel outline into a **12 x 13, 2-bit cell**. The outline is added
  by the game ("draw only the letter, because the game adds the dark outline").
- Text advances **2 units** for a narrow glyph (8 pixels) and **3 units** for a wide one (12 pixels); a
  unit is 4 pixels, or 1 pixel for the French translation's variable-width font
  (`FMesFont::WidthUnits`, `UnitPixels`, `SetVariableWidths`).
- A message is **composed into a 2-bit plane of a VRAM image** (`RenderMesString`, `DrawMesTypedLines`
  in `Text/MesRender.h`), then shown as **4-bit indexed VRAM sprites with a CLUT per row**
  (`FieldScreenLayer::Text` → `MakeSprite(..., ETextureDepth::Indexed4, ...)`).

So on screen, text is a VRAM texture. Its sharpness is bounded by the emulated 320x240 frame, not by the
window size: at 1440p one glyph texel covers roughly eight screen pixels.

## What a player font sheet can and cannot do

The modding path exists and works: **Options → Modding → Export text** writes `font.png` (12x11 cells,
16 per row) and `font.tsv`, and a copy in `Assets/font` replaces the font at the next start
(`Docs/Modding.md`, `TextModFiles.cpp`, `FMesFontSheet`).

Limits, from the code and the docs:

- A sheet may be the grid **1 to 16 times larger** (`MesFontSheet::MostScale`), but it is **reduced to
  12 x 11 per cell** — the larger size only helps authoring, never sharpness.
- An 8-pixel-advance letter must keep its body **within the first 6 columns** of its cell, or the next
  letter cuts off its outline.
- Changing a glyph's **width** changes where lines break. The widths are the original text's layout, and
  the oracle tests digest frames; keeping 8/12 advances is the safe path.

## Paths, in rising cost

| | Path | Code | Payoff | Risk |
|---|---|---|---|---|
| A | **Redraw the glyphs** within the existing cells (same advances). Bolder stems, taller x-height, open counters, high contrast. | none — asset only | real but bounded by 6x11 for narrow letters | none: reversible, it is the supported modding path |
| B | **Offer several sheets** and pick one in Options, instead of the single `Assets/font` folder. | small: a settings row plus loading one of several folders | lets a player trade the original look for clarity per use | low |
| C | **Draw text larger on screen** (scale the message sprites). | medium | none on its own: bigger but no sharper, and it overflows the fixed message boxes | high: line breaks, window geometry |
| D | **A high-resolution text path**: re-rasterize the composed lines at screen resolution in the Unreal layer and substitute that texture for the sprite. Glyph rows are already available (`FMesFont::Rows`), as are each glyph's position and the per-line strips, so the layout and the VRAM composition stay untouched — only the appearance changes. Would also allow a TrueType face. | large: draw-list/renderer changes, per-frame rasterization and caching, interaction with integer scaling and the CRT filter | the only path to genuinely crisp text | medium: additive and optional, but it touches rendering that the frame-digest tests watch |

Path C is listed to be rejected: it makes the complaint worse.

## Try before writing code

1. **Options → Graphics → Sprite filter: Sharp** (or Crisp) rather than Smooth, and **Integer scaling:
   on**. Soft filtering is the usual reason bitmap text looks mushy; sharp filtering with whole-number
   scaling makes each texel an even block. This may be enough on its own.
2. **Turn the CRT filter off** while judging it; it reduces contrast on small text.
3. Then judge again from `D:\Games\Weltall`.

## What upstream has done

- [#33](https://github.com/Roman215/Weltall/issues/33) asked for xBRZ-style filters on sprites including
  fonts. The maintainer answered only with integer scaling (added in 1.0); no filter was added.
- [#24](https://github.com/Roman215/Weltall/issues/24) complained that texture packs' fonts are harder to
  read; 1.0 answered with per-category pack toggles, so a pack's font can be turned off. The font is
  treated as replaceable art, not as something the port improves.
- [#5](https://github.com/Roman215/Weltall/issues/5) (HD portrait pack) notes that HD text and some UI
  were the parts that would not work — consistent with text being VRAM-bound.

No upstream work targets legibility of the port's own text.

## The French translation is the precedent to look at

`FMesFont::SetVariableWidths` gives each code a width **in pixels**, and `UnitPixels()` becomes 1: that is
an existing, data-driven font-metric mode used by a real translation. It shows the metrics are already
parameterised rather than hard-coded, which helps path D but is not a solution by itself.
