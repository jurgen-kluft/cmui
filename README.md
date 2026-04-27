# cmui

State: WIP

Remote UI Server for ESP32 microcontrollers, running a software 2D renderer. The server compresses the frame to efficiently update the display on the client side, sending minimum amount of data. Communication between the server and clients is done using a simple TCP protocol, allowing for low latency and high throughput.

## Features

- TCP based communication (ctx, simple) protocol for low latency and high throughput
- Clients are ESP32 based devices
- Block based frame-buffer diff engine for efficiently updating the UI display on the client

## 2D rendering with cgx2

The server uses the cgx2 library for 2D rendering, which provides a simple and efficient way to draw graphics on a memory based framebuffer. The library supports basic drawing operations such as lines, rectangles, circles, sprites and text rendering.

## Framebuffer Compression

To efficiently update the display on the client side, the server compresses the framebuffer, see the design of this compression in the `docs/design/FrameEncoder.md` document.
