# cmui

Remote UI Server for ESP32-S3 microcontrollers.

## Features

- Remote UI server for ESP32-S3 microcontrollers
- Block based diff engine for efficient updates
- TCP based communication protocol for low latency and high throughput
- Simple TCP protocol for communication

## Framebuffer Diff Engine

The diff engine divides the framebuffer into blocks of 16x16 pixels and compares the current framebuffer with the previous one to identify which blocks have changed. Only the changed blocks are sent to the client, reducing bandwidth usage and improving performance.

There is an optimization on a row where if a block is detected as changed, the engine checks if the next blocks in the same row are also changed. If they are, all consecutive changed blocks are sent together in a single update, further reducing the number of updates needed.
