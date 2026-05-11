# cmui

State: WIP

Remote UI Server for ESP32 microcontrollers, running a software 2D renderer. The server compresses the frame to efficiently update the display on the client side, sending minimum amount of data. 

Communication between the server and clients is done using TCP, sending small packets containing the compressed framebuffer data. The clients are responsible for decompressing the data and rendering it on their displays.

Steps:

- [ ] Load UI description, `UI.json`
- [ ] Loading necessary paks
- [ ] Client-Server
- [ ] Communication Protocol (TCP)
- [x] 2D Rendering (lines, rectangles, circles, sprites)
- [ ] Font Rendering
- [x] Frame Compression

## Features

- Clients are ESP32 based devices, preferably an ESP32-S3 with 8MiB of PSRAM
- TCP based communication (ctx, simple) protocol
- Frame-buffer delta (previous, current) compressor
- 2D rendering capabilities (lines, rectangles, circles, sprites)
- Font rendering support

## 2D rendering with cgx2

The server uses the cgx2 library for 2D rendering, which provides a simple and efficient way to draw graphics on a memory based framebuffer. The library supports basic drawing operations such as lines, rectangles, circles, sprites and text rendering.

## Framebuffer Compression

To efficiently update the display on the client side, the server compresses the framebuffer, see the design of this compression in the `docs/design/FrameEncoder.md` document.
