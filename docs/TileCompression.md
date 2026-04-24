# TileCompression.md

## Overview

This document defines the **tile compression wire format** for a **16×16 RGB565 tile renderer** using a **small, fixed set of global palettes**, augmented with **optional row-empty masks** and a **FLAG_ALL_BG optimization**.

The design goals are:

- Deterministic, allocation-free encoding and decoding
- Extremely simple decoder suitable for ESP32-class MCUs
- Predictable bandwidth with a strict worst-case bound
- High compression efficiency for UI-style framebuffers

All palettes are **static, global, and immutable**, derived offline as described in `PaletteDerivation.md`.

---

## Tile Formats

Only the following tile formats exist:

```text
TILE_PALETTE_1     // embedded 2-color palette (bg + fg) with row mask
TILE_PALETTE_4     // global palette, 2 bits per pixel
TILE_PALETTE_16    // global palette, 4 bits per pixel
TILE_PALETTE_256   // global palette, 8 bits per pixel
TILE_RAW           // uncompressed fallback
```

### Explicitly excluded formats

The following formats are intentionally not supported:

- TILE_SOLID_COLOR (superseded by TILE_PALETTE_1 + FLAG_ALL_BG)
- PALETTE_8 (3 bpp, non-byte-aligned)
- PALETTE_32 / 64 / 128 (low benefit, higher complexity)

This ensures **only byte-aligned pixel streams** in all decoders.

---

## Tile Geometry

- Tile size: **16 × 16 pixels**
- Pixel format: **RGB565 (16-bit)**
- Uncompressed tile size: **512 bytes**

All compressed tiles begin with a **2-byte base header**.

```cpp
struct tile_header_t
{
    u8 format;   // tile_format_t
    u8 flags;    // bit flags, see below
};
```

### Header Flags

```text
FLAG_HAS_ROW_MASK = 0x01   // followed by u16 row_empty_mask
FLAG_ALL_BG       = 0x02   // tile is entirely background color
```

`FLAG_ALL_BG` is only valid for `TILE_PALETTE_1` and indicates an optimized solid-fill case.

---

## Encoded Tile Sizes (16×16)

| Format | Description | Typical Bytes | Worst Case | Notes |
|-------|-------------|---------------|------------|-------|
| PALETTE_1 + ALL_BG | Solid fill | 4 | 4 | Replaces TILE_SOLID_COLOR |
| PALETTE_1 | 2-color tile | 20–40 | ~72 | 1 bpp, row-mask driven |
| PALETTE_4 | Global palette | ~67 | ~67 | 2 bpp |
| PALETTE_16 | Global palette | ~131 | ~131 | 4 bpp |
| PALETTE_256 | Global palette | ~259 | ~259 | 8 bpp |
| RAW | Uncompressed | — | **514** | Absolute bound |

**Hard invariant:** no tile ever exceeds **514 bytes**.

---

## TILE_PALETTE_1 (Embedded Two-Color Palette)

### Semantics

- Palette is embedded in the tile
- `palette[0]` = background color
- `palette[1]` = foreground color
- Tiles may range from fully solid to sparse binary glyphs

This format fully replaces the former `TILE_SOLID_COLOR`.

---

### Encoding Layout

```text
[base header]
[u16 palette_bg]
[u16 palette_fg]
(optionally) [u16 row_empty_mask]
(optionally) [row bitmap data]
```

---

### FLAG_ALL_BG Optimization

When `FLAG_ALL_BG` is set:

- The tile is entirely filled with `palette_bg`
- `row_empty_mask` is implicitly all rows empty
- No row mask field is serialized
- No bitmap data is present

Encoding:

```text
[base header | FLAG_ALL_BG]
[u16 palette_bg]
```

This recovers the **4-byte solid tile case**, exactly matching the former `TILE_SOLID_COLOR` size.

---

### Row Mask (PALETTE_1)

If `FLAG_ALL_BG` is **not** set, `FLAG_HAS_ROW_MASK` must be set.

- `row_empty_mask` is a 16-bit value
- Bit y = 1 → row y is entirely background
- Bit y = 0 → row y contains bitmap data

Only non-empty rows have bitmap data emitted.

---

### Bitmap Row Encoding

Each non-empty row encodes:

- **16 bits** (1 bit per pixel)
- Bit = 0 → background color
- Bit = 1 → foreground color

Each row contributes **2 bytes**.

This encoding is byte-aligned and requires no cross-row or cross-byte state.

---

## TILE_PALETTE_4 / 16 / 256 (Global Palettes)

These formats are unchanged except for optional row-empty masks.

- Pixel indices reference a global palette
- Row masks may be used when `FLAG_HAS_ROW_MASK` is set
- Pixel data is emitted only for non-empty rows

Row cost per non-empty row:

| Format | Bytes per row |
|-------|---------------|
| PALETTE_4 | 4 |
| PALETTE_16 | 8 |
| PALETTE_256 | 16 |

---

## TILE_RAW

- Uncompressed RGB565 tile
- No row masks
- Always exactly 514 bytes on the wire

---

## Tile Classification Order

At encode time:

```text
if tile uses ≤ 2 colors:
    TILE_PALETTE_1
    if all pixels == background:
        set FLAG_ALL_BG
    else:
        set FLAG_HAS_ROW_MASK
else if palette ⊆ PALETTE_4:
    TILE_PALETTE_4
else if palette ⊆ PALETTE_16:
    TILE_PALETTE_16
else if palette ⊆ PALETTE_256:
    TILE_PALETTE_256
else:
    TILE_RAW
```

Row masks are emitted only if they reduce size.

---

## Properties Achieved

- Unified representation for solid and binary tiles
- Recovery of the optimal 4-byte solid tile case
- Major bandwidth reduction for glyphs and outlines
- Extremely simple, predictable decoder logic
- Strict worst-case bound preserved

---

## Summary

By introducing **TILE_PALETTE_1** with the **FLAG_ALL_BG optimization**, the compression format:

- Eliminates a special TILE_SOLID_COLOR case
- Gains strong compression for 2-color UI tiles
- Retains the minimal 4-byte solid tile encoding
- Preserves deterministic, byte-aligned decoding

This is the final, robust foundation for palette-based UI tile compression on embedded systems.
