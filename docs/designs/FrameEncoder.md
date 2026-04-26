Please build the following tool in Golang and provide a download link for the source code.

The Encoder and Decoder

✅ Command-Line
  - Previous Image: PNG image file (optional)
  - Next Image: PNG image file (required)
  - Run-Change sizes to use: 16, 32, 64
  - Fill-Color, no prev image -> use this color to fill the prev image (default: black)
  - Prev-Shift, no prev image -> copy next image as prev but shift down by 1 line
✅ Build color histogram from RGBA8888 image, as a RGB565 palette counting the colors, then sort by frequency
✅ P0 takes the first 4 most frequent colors, encoded as 2-bit symbols (0-3) referencing the palette
✅ P1 takes the next 16 most frequent colors, encoded as 4-bit symbols (0-15) referencing the palette
✅ P2 takes the remaining colors, encoded as 8-bit symbols (0-255) referencing the palette
✅ This results in a palette of up to 276 colors (4 in P0, 16 in P1, 256 in P2) with the most frequent colors
✅ Any color that is not in the palete is encoded as SELECTOR_RAW in the selector stream, and the full RGB565 value is written to the P3 stream
✅ Selector stream has the following 4 symbols:
  - SELECTOR_P0 (0): pixel value is in P0 stream, read 2-bit symbol from P0 stream to get palette index
  - SELECTOR_P1 (1): pixel value is in P1 stream, read 4-bit symbol from P1 stream to get palette index
  - SELECTOR_P2 (3): pixel value is in P2 stream, read 8-bit symbol from P2 stream to get palette index
  - SELECTOR_RAW (2): pixel value is not in palette, read full RGB565 value from P3 stream
✅ Line‑change stream → real BitStream → SRLEN
✅ Run‑change stream → real BitStream → SRLEN
✅ Selector stream (2‑bit symbols) → real BitStream → SRLEN
✅ Payload streams:
  - P0: 2‑bit packed stream (SRLEN)
  - P1: 4‑bit packed stream (SRLEN)
  - P2: 8‑bit packed byte stream (SRLEN)
  - P3: real 16‑bit RGB565 stream (no SRLEN)
✅ Selector chooses which payload stream consumes data
✅ No counters pretending to be streams, payload streams are real streams
✅ Full report written to console after encoding, showing:
  - Image dimensions
  - Number of pixels
  - RAW size of image in bytes (width * height * 2 for RGB565)
  - Size of line stream in bytes, before and after SRLEN + % compression ratio
  - Size of run stream in bytes, before and after SRLEN + % compression ratio
  - Size of selector stream in bytes, before and after SRLEN + % compression ratio
  - Size of P0 stream in bytes, before and after SRLEN + % compression ratio
  - Size of P1 stream in bytes, before and after SRLEN + % compression ratio
  - Size of P2 stream in bytes, before and after SRLEN + % compression ratio
  - Size of RAW stream in bytes
  - Total size of all streams combined, before and after SRLEN + % compression ratio
  - Compression ratio of total size after SRLEN compared to RAW size
✅ Encoder and decoder implementation
✅ Round-trip test to verify that encoding and then decoding produces the original image
✅ All in one source file, no external dependencies, can be built with a single command

