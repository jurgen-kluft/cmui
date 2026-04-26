✅ SRLE with per symbol run-bit table (SRLEN)
  - 1-bit symbols:
    - read 1-bit, index into symbol table (2 entries)
    - symbol table entry:
      - 1-bit symbol value (0-1)
      - run-bits count (0-15 or 0-31 depending on run-change size)
  - 2-bit symbols:
    - read 2-bits, index into symbol table (4 entries)
    - symbol table entry:
      - 2-bit symbol value (0-3)
      - run-bits count (0-15 or 0-31 depending on run-change size)
  - 4-bit symbols:
    - read 4-bits, index into symbol table (16 entries)
    - symbol table entry:
      - 4-bit symbol value (0-15)
      - run-bits count (0-15 or 0-31 depending on run-change size)
  - 8-bit symbols:
    - read 8-bits, index into symbol table (256 entries)
    - symbol table entry:
      - 8-bit symbol value (0-255)
      - run-bits count (0-15 or 0-31 depending on run-change size)
✅ SRLEN encoder specification:
  ✅ Runs are bucketed using {32, 16, 8, 4, 2, 1} (greedy split)
  ✅ For each symbol, we evaluate all run‑bit options:  rb ∈ {0, 1, 2, 3, 4, 5}
  ✅ rb = 0 means RAW (no run encoding)
  ✅ For each symbol, we pick the rb that minimizes encoded size
  ✅ Encoding uses per‑symbol rb
  ✅ This guarantees no expansion per symbol
  ✅ Write unittests for SRLEN that encodes known streams and then decodes and compares, do this for each symbol size (1-bit, 2-bit, 4-bit, 8-bit)
