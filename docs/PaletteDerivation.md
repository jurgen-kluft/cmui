# PaletteDerivation.md

## Overview

This document defines the **final, simplified palette derivation model** for a tile‑based RGB565 UI renderer using **global palettes only**.

The system targets **16×16 tiles**, deterministic encoding, small embedded decoders (ESP32‑class), and predictable bandwidth. All palettes are **static, global, immutable**, and shared between encoder and decoder.

The design deliberately avoids adaptive palettes, entropy coding, and non‑byte‑aligned pixel formats.

---

## Final Palette Set

Only the following tile formats exist:

```text
TILE_SOLID_COLOR   // implicit fill, 1 color
TILE_PALETTE_4     // global, 2 bpp
TILE_PALETTE_16    // global, 4 bpp
TILE_PALETTE_256   // global, 8 bpp
TILE_RAW           // fallback
```

### Explicit exclusions

The following formats are **intentionally not supported**:

- PALETTE_1 as a 1‑bit indexed format (SOLID_COLOR replaces this)
- PALETTE_8 (3 bpp, decoder complexity)
- PALETTE_32 / 64 / 128 (marginal gain, high complexity)

This leaves **only byte‑aligned pixel formats**.

---

## Tile Sizes (16×16)

| Format | Bits / pixel | Tile bytes | Notes |
|------|---------------|------------|-------|
| SOLID_COLOR | — | 4 | header + color |
| PALETTE_4 | 2 | ~67 | header + id + 64 | 
| PALETTE_16 | 4 | ~131 | header + id + 128 |
| PALETTE_256 | 8 | ~259 | header + id + 256 |
| RAW | — | 514 | strict worst‑case |

**Hard invariant:** no tile ever exceeds **514 bytes**.

---

## Role of Each Palette

### TILE_SOLID_COLOR (Palette‑1 semantic)

- Entire tile is a single color
- Used for backgrounds, panels, empty areas
- No pixel stream present
- Highest compression win

This replaces any notion of a 1‑bit palette.

---

### TILE_PALETTE_4

- Dominant format for UI text and chrome
- Covers backgrounds + text + accent colors
- Encodes 4 pixels per byte
- Decoder: trivial shift & mask

---

### TILE_PALETTE_16

- Used for icons, anti‑aliased glyphs, soft shading
- Encodes 2 pixels per byte
- Natural fallback when PALETTE_4 does not match

---

### TILE_PALETTE_256

- Dense fallback palette
- Used for visually complex tiles
- Guarantees smaller size than RAW
- Always 1 byte per pixel

---

## Global‑Only Palette Rules

- All palettes are **defined offline**
- No palettes are transmitted at runtime
- Palette ordering is **numerical by RGB565 value**
- Palettes are versioned implicitly by firmware build

Subset rule:

```
Tile palette P is compatible with global palette G iff:
P ⊆ G
```

---

## Derivation Process (Offline)

### Step 1 — Collect Data

- Capture 2–5 representative UI screens
- Convert to RGB565
- Split into 16×16 tiles

---

### Step 2 — Extract Tile Palettes

For each tile:

- Collect unique RGB565 colors
- Sort numerically
- Count occurrences

---

### Step 3 — Derive SOLID_COLOR Frequency

- Identify tiles with exactly 1 unique color
- Record most common fill colors

These do not belong to PALETTE_4.

---

### Step 4 — Derive PALETTE_4

- Consider tile palettes of size 2–4
- Group by frequency
- Promote supersets that cover many tiles

Goal: maximize coverage with 4 colors.

---

### Step 5 — Derive PALETTE_16

- Consider tile palettes of size 5–16
- Merge frequent and visually related palettes
- Cover icon shading ramps and glyph AA colors

Goal: cover ~65–75% of tiles.

---

### Step 6 — Derive PALETTE_256

- Collect colors from tiles not covered by PALETTE_16
- Build histogram
- Select most frequent K colors

Constraint:

```
K < 128
```

Typical value: **64–96 colors**

---

## Runtime Tile Classification

At encode time:

```text
if tile has 1 color:
    SOLID_COLOR
else if palette ⊆ PALETTE_4:
    PALETTE_4
else if palette ⊆ PALETTE_16:
    PALETTE_16
else if palette ⊆ PALETTE_256:
    PALETTE_256
else:
    RAW
```

Selection is purely size‑driven and deterministic.

---

## Worked Example (Measured UI Screens)

Two real UI screens were analyzed.

### Screen A

- Tiles: 192
- Max colors per tile: 46
- Avg colors per tile: ~15.7

Distribution:
- ≤4 colors: 31%
- ≤16 colors: 51%
- All tiles ≤256

---

### Screen B

- Tiles: 1036
- Max colors per tile: 114
- Avg colors per tile: ~13.6

Distribution:
- ≤4 colors: 52%
- ≤16 colors: ~70%
- All tiles ≤256

---

### Result

- Majority of tiles map to SOLID / PALETTE_4
- PALETTE_16 handles most remaining tiles
- PALETTE_256 absorbs the rest
- RAW unused in practice

---

## Properties Achieved

- Deterministic decoding
- No bit‑level complexity
- Minimal decoder branches
- Predictable bandwidth
- Optimal for embedded UI rendering

---

## Summary

This palette derivation model is intentionally **minimal, robust, and data‑driven**.

By using only:

- SOLID_COLOR
- PALETTE_4
- PALETTE_16
- PALETTE_256

…the system achieves excellent compression, trivial decoding, and long‑term maintainability on constrained hardware.
