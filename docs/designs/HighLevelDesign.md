# UI Server

UI Server renders UI pages on demand for connected clients. The frame lifecycle is client-driven.

At startup, UI Server reads configuration JSON for all page IDs, loads sprite/font resources, and builds a read-only shared UI context. Each connection gets a mutable session state (current page, previous frame index, per-client buffers, protocol state).

The detailed wire protocol is specified in [docs/FrameSyncProtocol.md](docs/FrameSyncProtocol.md).

## Architecture Summary

1. Client requests rendering by sending `FRAME_BEGIN(page_id, frame_index)`.
2. Server renders `page_id` at `frame_index`, compares against `frame_index - 1`, and returns:
   1. `FRAME_BEGIN` response with frame metadata and root hash
   2. Specific data payloads
   3. `FRAME_END` (server end-of-frame marker)
3. Client verifies hashes and root hash, then responds with `FRAME_END` acknowledgment:
   1. `status=OK`, or
   2. `status=INVALID` where bit=0 indicates mismatching hash.
4. On `INVALID`, server retransmits only failed data and sends another `FRAME_END` for the same frame index.
5. Server repeats until client sends `OK` or retry/time budget is exceeded.
6. On `OK`, server waits for the next client message.

## Constraints

- LAN only
- No authentication or encryption (trusted home network assumption)
- TCP transport (in-order byte stream)
- Little-endian encoding
- Client display resolution is always below 2048 x 2048
- Client does not run UI logic; it renders server-provided frame data
- Client may send input events to server, which are applied to session/UI state for future renders
- Server is authoritative for rendering and verification; client is a passive renderer and verifier

## Threading Model

1. Main thread:
   1. Load configuration JSON and initialize shared UI context.
   2. Start listening socket.
2. Network accept thread:
   1. Accept incoming connections.
   2. Spawn one connection thread per client.
3. Connection thread (single owner of session state):
   1. Handle protocol I/O.
   2. Render requested frames.
   3. Generate 'compressed' frame data and send to client.
   4. Run row-repair loop until frame acknowledged.

This keeps per-client UI/render/protocol state owned by a single thread.

## Session Flow

1. Handshake:
   1. Client sends `CLIENT_HELLO`.
   2. Server validates and replies `SERVER_HELLO`.
2. Idle wait:
   1. Server waits for either frame request or event batch.
3. Frame request path:
   1. Client sends `FRAME_BEGIN` request.
   2. Server returns frame payload and `FRAME_END`.
   3. Client returns `FRAME_END` acknowledgment (`OK` or `INVALID`).
   4. Server retransmits failed data until `OK`.
4. Event path:
   1. Client sends `EVENTS_BEGIN`.
   2. Client sends one or more `EVENT_INFO` messages.
   3. Client sends `EVENTS_END`.
   4. Server applies events to session/UI state for future renders.

## Verification Model

- Server sends hashes with payloads.
- Client computes hashes for data to verify integrity.
- Client computes root hash from data hashes.
- Client acknowledges frame with `FRAME_END`:
  - `OK` when hashes and root hash match.
  - `INVALID` when one or more hashes mismatch.

## Reliability and Recovery

- Protocol uses sequence numbers in message headers.
- Row-level repair loop is bounded by retry/time budgets.
- On repeated failures, server falls back to keyframe behavior.
- On persistent failure, session can be disconnected.

## Input Events

Event batching is explicit:

1. `EVENTS_BEGIN(batch_id, event_count)`
2. `EVENT_INFO` entries (touch, up/down/left/right, button press/release)
3. `EVENTS_END(batch_id, sent_event_count)`

The event batch updates server-side UI state and is reflected by subsequent client-driven frame requests.

## Data Structures (High-Level)

Per-session state includes:

- Connection/socket state
- Protocol sequence/epoch state
- Current page ID
- Previous acknowledged frame index
- Client display metadata
- Staging/display frame data for verification/commit
- Hashes for frame data verification

## Reference

For message IDs, payload structs, validation rules, retry constants, and full client/server state machines, see [docs/FrameSyncProtocol.md](docs/FrameSyncProtocol.md).
