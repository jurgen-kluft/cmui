# UI Server

1. TCP Server listens for incoming connections from clients
   a. client connects and sends info about screen size, color format, page id, etc.
   b. server creates a connection instance for the client and stores the info, and
      also registers a UI instance for the renderer.
2. Render a UI instance:
   a. background = foreground 
   b. render to texture (foreground)
   b. diff background and foreground
   c. send diff to client

## Two Threads

1. Main thread: 
   a. UI Rendering
   b. Diffing
2. Network thread: 
   a. Listen for incoming connections
   b. Registers, Updates and Removes UI instances


## Future Optimizations

- Block Cache (Server Side decides which blocks to cache and assumes they are cached on the client)
  - identify blocks by SHA1 hash, 20 bytes
  - instruct device to cache certain blocks, and only send block hashes
