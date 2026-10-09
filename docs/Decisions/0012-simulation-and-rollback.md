# 0012 - Simulation, Input Commands and Rollback

## Status
Accepted

## Context
The simulation runs on a fixed tick, reads input only through per-tick input commands, and can save and restore its state so clients can roll back and re-simulate for prediction (see [Simulation](../Architecture.md#simulation) and [Prediction](../Features/Networking.md#prediction)). Milestone 3 decides how ticks are driven, what an input command holds, and how state is saved and restored exactly.

## Decision
- **Fixed ticks** - `Simulation` runs its systems in order, once per tick, with a constant delta time (60 Hz by default). `Advance()` adds real time and runs the ticks that are due, asking for each tick's input commands; it runs at most `MaxTicksPerFrame` ticks per frame and skips the rest of a long frame, so a slow machine falls behind real time instead of spiraling
- **Interpolation one tick behind** - before each tick, every entity's transform is copied into an internal `PreviousTransform` component. Rendering interpolates from it to the current transform by how far real time is towards the next tick (positions and scales linearly, rotations by slerp)
- **Input commands** - one per player per tick: keys, mouse buttons and gamepad buttons held at the end of the tick plus those pressed and released during it, the mouse position and movement, scrolling, and the player's gamepad axes. A tap shorter than a tick still shows as pressed and released. `InputCommandBuilder` (in `LodestoneClient`) accumulates device input across frames into the next command; the input codes moved to `LodestoneCore`, which never touches devices
- **Snapshots** - `Scene::SaveSnapshot()` copies the whole registry - every entity, including the free list of destroyed entities, and every registered component, internal ones included - with EnTT's snapshot into an in-memory archive, plus the root order and the scene's ID generator. Restoring rebuilds the registry from it, so entity handles, iteration order and the handles of entities created afterwards all match the original run
- **Deterministic IDs** - each scene generates entity UUIDs from its own xoshiro256** generator, seeded randomly when the scene is created and saved in snapshots. Re-simulating after a restore creates entities with the same UUIDs as the first time
- **Determinism scope** - the same state and the same input commands give the same result on the same machine, which is what rollback needs. Results may differ between machines and compilers (floating point), so the server stays authoritative

## Alternatives
- **Variable time steps** - simpler, but physics and prediction depend on identical steps
- **Serializing snapshots through the reflection registry** - smaller and cross-machine, but slower, and it would lose data that components hold without reflecting it. Snapshots stay in memory, so copies are enough
- **Saving only changed components** - faster for large scenes; worth revisiting when profiling shows snapshot cost matters (Milestone 11)

## Consequences
- All simulation state lives in registered components; anything else (a system's own member variables) isn't rolled back. Systems iterate entities through EnTT, never through hash maps, whose order isn't part of the snapshot
- Entities created in a tick have no previous transform, so they render where they are until the next tick
