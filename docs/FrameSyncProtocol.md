# Frame Sync Protocol

## Overview

This protocol is client-driven for rendering.

1. Client requests a frame render with `FRAME_BEGIN(frame_index, previous_frame_index)`.
2. Server renders that page for `frame_index`, compares against `previous_frame_index`, then returns:
   1. `FRAME_BEGIN(frame_index, root_hash, ...)`
   2. Zero or more row payloads (`ROW_BEGIN` + `ROW_TILES`)
   3. `FRAME_END(frame_index, ...)`
3. Client verifies row hashes and root hash, then responds with `FRAME_END` as acknowledgment:
    1. Full row bitmap, server can determine which rows need retransmission if mismatches were found
4. Server retransmits only failed rows, ends with another `FRAME_END`, and repeats until no more mismatches or retry/time budget is exceeded.
5. On no mismatches, server goes back to waiting for next client message.

The client can also send input events with `EVENTS_BEGIN`, one or more `EVENT_INFO`, and `EVENTS_END`.
These events are only sent outside of `FRAME_BEGIN`/`FRAME_END` sequences and are applied to session/UI state for future renders.

## Fixed Parameters

1. Endianness: little-endian for all multi-byte fields.
2. Hash function: BLAKE3-128 (16 bytes).
3. Tile size: 16 x 16 pixels.
4. Tile addressing: per-row by `tile_x`.
5. Hash hierarchy:
   1. tile hash (computed on server, sent with tile)
   2. row hash = hash of ordered tile hashes in the row
   3. root hash = hash of ordered row hashes
6. Maximum payload (`length`) per message: 65536 bytes.

## Message Header

```c++
struct msg_header_t
{
    u16 type;      // message type
    u16 flags;     // reserved, set to 0
    u32 length;    // payload bytes after this header
    u32 seq;       // monotonic per direction, starts at 1
    u32 epoch;     // incremented when session is reset
};
```

Validation rules:

1. `length <= 65536`
2. Unknown message type closes connection.
3. Per direction, receiver tracks `expected_seq`.
4. If `seq == expected_seq`: accept and increment `expected_seq`.
5. If `seq < expected_seq`: treat as duplicate and ignore.
6. If `seq > expected_seq`: trigger session recovery.
7. Session recovery is defined as: send `DISCONNECT` (best effort), close TCP connection, and require a fresh `CLIENT_HELLO`/`SERVER_HELLO` handshake on reconnect.

## Message Types

1. `1  = CLIENT_HELLO`
2. `2  = SERVER_HELLO`
3. `3  = FRAME_BEGIN`        // request and response, role-dependent payload
4. `4  = ROW_BEGIN`
5. `5  = ROW_TILES`
6. `6  = FRAME_END`          // server frame end and client verification ack
7. `7  = EVENTS_BEGIN`
8. `8  = EVENT_INFO`
9. `9  = EVENTS_END`
10. `10 = DISCONNECT`

## Handshake Payloads

```c++
struct client_hello_t
{
    u16 protocol_version;   // current: 1
    u16 hardware_id;
    u16 color_format;       // RGB565, RGBA8888, ...
    u16 screen_width;
    u16 screen_height;
    u16 tile_w;             // must be 16
    u16 tile_h;             // must be 16
    u32 max_payload;
};

struct server_hello_t
{
    u16 protocol_version;
    u16 accepted;           // 1=yes, 0=no
    u16 color_format;
    u16 tile_w;
    u16 tile_h;
    u32 max_payload;
    u32 epoch;
};
```

## Frame Payloads

`FRAME_BEGIN` uses a single message type (`type=3`) in both directions.
Direction is the discriminator:

1. Client -> Server: payload must decode as `frame_begin_req_t`.
2. Server -> Client: payload must decode as `frame_begin_rsp_t`.
3. Any other payload layout for the current direction is a protocol error and closes connection.

`FRAME_BEGIN` request from client:

```c++
struct frame_begin_req_t
{
    u32 frame_index;
};
```

`FRAME_BEGIN` response from server:

```c++
struct frame_begin_rsp_t
{
    u32 frame_index;
    u16 tiles_x;
    u16 tiles_y;
    u32 changed_tile_count;
    u8  root_hash[16];
};
```

Row metadata:

```c++
struct row_begin_t
{
    u32 frame_index;
    u16 row_id;
    u16 changed_tile_count;  // tiles sent for this row
    u8  row_hash[16];
};
```

Per-row tiles:

```c++
struct tile_entry_t
{
    u16 tile_x;
    u16 data_len;            // tile data bytes
    u8  tile_hash[16];
    // u8 data[data_len];
};

struct row_tiles_t
{
    u32 frame_index;
    u16 row_id;
    u16 entry_count;
    // tile_entry_t entries[];
};
```

`FRAME_END` from server (end of frame payload):

```c++
struct frame_end_server_t
{
    u32 frame_index;
    u32 sent_tile_count;
    u8  root_hash[16];
};
```

`FRAME_END` from client (verification acknowledgment):

```c++
struct frame_end_client_t
{
    u32 frame_index;         // must match server frame_index
    u16 status;              // 1=OK, 0=Mismatch
    u16 row_count;           // number of rows in this frame, needed to interpret bitmap
    u16 bitmap_byte_count;   // number of u8 bytes that follow
    u16 reserved;            // reserved for future use, set to 0
    u8  expected_root[16];   // server root from frame_end_server_t
    u8  actual_root[16];     // computed by client
    // u8 row_ok_bitmap[bitmap_byte_count];
    // bit i: 1=row hash matched, 0=row hash mismatch
};
```

Note: When all rows are valid, `bitmap_byte_count` = 0.

## Event Payloads

Events from the client are logically handled by the server as they arrive, but the client batches them explicitly with `EVENTS_BEGIN` and `EVENTS_END` to allow for more efficient processing and to provide better context for the server when applying them to the session/UI state.

Events are simple input events like touch, directional pad, and button presses/releases, but the protocol can be extended with additional event types and fields as needed.

The primary use case is for the client to send input events that affect which page the server renders, but the protocol does not strictly require this and the server is free to interpret and apply events in any way that makes sense for the application.

```c++
struct events_begin_t
{
    u32 batch_id;
    u16 event_count;         // expected EVENT_INFO count
    u16 reserved;
};

struct event_info_t
{
    u32 batch_id;
    u16 event_type;          // 1=touch, 2=up, 3=down, 4=left, 5=right, 6=button_press, 7=button_release
    u16 event_flags;         // reserved for modifiers
    s16 x;                   // touch x, or 0 when not applicable
    s16 y;                   // touch y, or 0 when not applicable
    u16 button_id;           // button id for press/release
    u16 reserved;
    u32 event_time_ms;
};

struct events_end_t
{
    u32 batch_id;
    u16 sent_event_count;
    u16 reserved;
};
```

## Client-Driven Frame Procedure

1. Client sends `FRAME_BEGIN(frame_begin_req_t)`.
2. Server renders `frame_index`, diffs against `frame_index - 1`.
3. Server sends `FRAME_BEGIN(frame_begin_rsp_t)`.
4. For each changed row, server sends `ROW_BEGIN` followed by one or more `ROW_TILES`.
5. Server sends `FRAME_END(frame_end_server_t)`.
6. Client verifies all row hashes and root hash.
7. Client sends `FRAME_END(frame_end_client_t)`:
   1. `status=OK` if all rows and root verify.
   2. `status=MISMATCH` with per-row result bits otherwise.
8. If server receives `FRAME_END(status=MISMATCH)`, it retransmits rows with bit=0, then sends `FRAME_END(frame_end_server_t)` again for the same `frame_index`.
9. Repeat until client sends `status=OK`, then server waits for next message.

## Verification Rules (Client)

1. Keep `staging_buffer` separate from displayed buffer.
2. On each `ROW_TILES`, for every tile entry:
   1. Recompute `tile_hash = BLAKE3-128(tile_data_bytes)` from received tile data.
   2. Compare recomputed hash with `tile_entry_t.tile_hash`.
   3. If mismatch: mark that row as failed for this verification pass (bit=0) and do not trust that tile hash for row/root recomputation.
   4. If match: apply tile data to `staging_buffer` and store the verified tile hash by `[row_id][tile_x]`.
3. On server `FRAME_END`:
   1. Recompute each row hash from verified tile hashes.
   2. Build `row_ok_bitmap`.
   3. Recompute root from recomputed row hashes.
   4. If all row bits are 1 and root equals server root:
      1. Commit `staging_buffer`.
      2. Send client `FRAME_END(status=OK)`.
   5. Else send client `FRAME_END(status=MISMATCH)` with bitmap and expected/actual roots.

## Retry and Timeout Policy

1. `verify_timeout_ms = 1000`
2. `row_repair_timeout_ms = 500`
3. `row_repair_retry_limit_per_frame = 4`
4. `max_consecutive_frame_failures = 3` then force keyframe or disconnect

Server behavior:

1. While waiting for client `FRAME_END` ack, if timeout occurs, retransmit current frame end marker once or enter keyframe fallback based on retry budget.
2. If row repair retries exceed limit for a frame, send full frame (`keyframe=1`) for same `frame_index` or next `frame_index` (implementation choice, but must be consistent).

## State Machines

Server state machine:

1. `S_WAIT_HELLO`
   1. On valid hello -> `S_WAIT_CLIENT_MESSAGE`
2. `S_WAIT_CLIENT_MESSAGE`
   1. On `FRAME_BEGIN(req)` -> render and send frame payload, go `S_WAIT_FRAME_ACK`
   2. On `EVENTS_BEGIN...EVENTS_END` -> apply events to UI state, stay `S_WAIT_CLIENT_MESSAGE`
   3. On disconnect/error -> `S_CLOSE`
3. `S_WAIT_FRAME_ACK`
   1. On client `FRAME_END(status=OK)` -> `S_WAIT_CLIENT_MESSAGE`
   2. On client `FRAME_END(status=MISMATCH)` -> retransmit bad rows + `FRAME_END`, stay `S_WAIT_FRAME_ACK`
    3. On `EVENTS_BEGIN...EVENTS_END`: protocol error (events are only valid outside active frame verification), close or disconnect session.
    4. On timeout/retry budget exceeded -> keyframe fallback or `S_CLOSE`

Client state machine:

1. `C_CONNECTED`
   1. Send `FRAME_BEGIN(req)` when a frame is needed
   2. Optionally send `EVENTS_BEGIN...EVENTS_END` any time between frame requests
2. `C_RECV_FRAME`
   1. Receive `FRAME_BEGIN(rsp)` + row payloads + server `FRAME_END`
   2. Verify row hashes + root hash
   3. Send client `FRAME_END(status=OK|MISMATCH)`
   4. If `status=MISMATCH`, continue receiving repaired rows and re-verify until OK or local retry limit exceeded
3. `C_CLOSE`
   1. On disconnect/fatal protocol error
