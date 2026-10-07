# Networking & Multiplayer

## Model
- [ENet](https://github.com/lsalzman/enet) for transport (UDP, with reliable and unreliable channels)
- Server-authoritative: the server runs the game simulation, including physics and scripts. Clients send input and receive state
- Two ways to host: a listen server (a player hosts and plays at the same time) and a dedicated server (headless - no window, renderer or audio)
- Players connect directly by IP and port
- Designed for small sessions (2-16 players)
- Fixed network tick rate (default 30 Hz, configurable per project)

## Replication
- Components are marked as replicated, per component and per field. Only changed data is sent
- Entities spawned or destroyed on the server (including prefabs) are spawned or destroyed on clients, matched by network ID
- Each entity has an owner (the server or a client). Clients can only send input and requests for entities they own
- Remote entities are interpolated between received states so they move smoothly
- Locally controlled entities use client-side prediction with server reconciliation, so player movement feels responsive under latency
- Animation state replicates (see [Animation](Animation.md))

## Scripting
- Query the network role (`IsServer`, `IsClient`, `IsHost`) and entity ownership
- RPCs: client → server, server → one client, and server → all clients
- Events for players connecting and disconnecting
- Host, join and disconnect from scripts, so games can build their own connect menus

## Security & Robustness
- The server validates every message from clients and never trusts client state
- Message sizes are bounded; malformed or malicious packets are dropped and logged, and never crash the server or a client
- Disconnects and timeouts are handled cleanly on both sides

## Editor & Tooling
- Multiplayer play mode: run a host (or dedicated server) plus extra clients on this machine, each in its own window
- Network simulation settings for latency, jitter and packet loss
- Network stats overlay: ping, bandwidth, packet loss, and replicated entity count
- All of this is available through MCP (see [AI Control](../AIControl.md))

## Testing
- Unit tests for replication serialization, interpolation and prediction/reconciliation
- Integration tests that run a server and several clients over loopback, in CI on all platforms
- Tests under simulated latency, jitter, packet loss and reordering
- Fuzz tests that feed malformed packets to the server and clients
