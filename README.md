# cmui

State: WIP

Remote UI Server for ESP32-S3 microcontrollers, running ImGui on Raylib. The server uses a block based diff engine to efficiently update the UI on the client side, sending only the changed blocks of the framebuffer. Communication between the server and clients is done using a simple TCP protocol, allowing for low latency and high throughput.

## Features

- TCP based communication (ctx, simple) protocol for low latency and high throughput
- Clients are ESP32 based devices
- Block based frame-buffer diff engine for efficiently updating the UI display on the client

## 2D rendering with cgx2

The server uses the cgx2 library for 2D rendering, which provides a simple and efficient way to draw graphics on a memory based framebuffer. The library supports basic drawing operations such as lines, rectangles, circles, sprites and text rendering.

## Framebuffer Diff Engine

The diff engine divides the framebuffer into blocks of 16x16 pixels and compares the current framebuffer with the previous one to identify which blocks have changed. Only the changed blocks are sent to the client, reducing bandwidth usage and improving performance.

There is an optimization on a row where if a block is detected as changed, the engine checks if the next blocks in the same row are also changed. If they are, all consecutive changed blocks are sent together in a single update, further reducing the number of updates needed.
