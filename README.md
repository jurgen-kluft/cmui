# cmui

State: WIP

Remote UI Server for ESP32-S3 microcontrollers, running ImGui on Raylib. The server uses a block based diff engine to efficiently update the UI on the client side, sending only the changed blocks of the framebuffer. Communication between the server and clients is done using a simple TCP protocol, allowing for low latency and high throughput.

## Features

- TCP based communication (simple) protocol for low latency and high throughput
- ESP32-S3 microcontroller clients
- Block based frame-buffer diff engine for efficiently updating the UI display on the client

## Framebuffer Diff Engine

The diff engine divides the framebuffer into blocks of 16x16 pixels and compares the current framebuffer with the previous one to identify which blocks have changed. Only the changed blocks are sent to the client, reducing bandwidth usage and improving performance.

There is an optimization on a row where if a block is detected as changed, the engine checks if the next blocks in the same row are also changed. If they are, all consecutive changed blocks are sent together in a single update, further reducing the number of updates needed.

## ImGui on Raylib

The server is able to run produce multiple UI's for different clients. The UI is built using ImGui and rendered using Raylib. The server can handle multiple clients simultaneously, allowing for a collaborative UI experience.
