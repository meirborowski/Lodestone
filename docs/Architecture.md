# Architecture

## Overview
- Scenes are made of entities and components, using EnTT
- Game logic is kept separate from rendering and audio: the simulation (scripts, physics, animation, networking) must be able to run without a window, renderer or audio device. This is what makes dedicated servers and headless tests possible. The [target layout](#targets-and-layering) enforces it
- The editor is a standalone executable, so exported games can't contain editor code (see [Targets and Layering](#targets-and-layering))
- Multiplayer is server-authoritative (see [Networking](Features/Networking.md))
- Ability to "export" a game - an executable that runs the game without editing ability, which we can distribute (see [Export](Features/Export.md))
- The editor needs to be fully controllable by AI agents - I should be able to ask you to build me a game like Tetris, and you should have all the tools available to do so without my intervention (see [AI Control](AIControl.md))
- Significant design decisions are recorded in [Decisions](Decisions/README.md)

## Targets and Layering
The layering is enforced by CMake target dependencies, so the linker - not discipline - keeps the simulation independent of windows, GPUs and audio devices.

| Target | Type | Contains | Links |
|---|---|---|---|
| `LodestoneCore` | Static library | ECS, scenes, assets, and the simulation: input commands, physics, scripting, animation sampling, networking | No GLFW, nvrhi or miniaudio |
| `LodestoneClient` | Static library | Window, device input, renderer, audio | `LodestoneCore` |
| `LodestoneEditor` | Executable | Editor UI, MCP server, headless mode | `LodestoneCore`, `LodestoneClient` |
| `LodestoneRuntime` | Executable | The player for exported games | `LodestoneCore`, `LodestoneClient` |
| `LodestoneServer` | Executable | The headless dedicated server | `LodestoneCore` |

- Editor code lives only in `LodestoneEditor`, which isn't built in Dist - so Dist binaries can't contain editor code
- Tests link only what they test: `LodestoneCore` tests run without a window, GPU or audio device

## Simulation
- The simulation runs on a fixed tick (default 60 Hz, configurable per project), independent of the frame rate. Rendering interpolates between the last two ticks
- Physics steps once per tick. The network sends snapshots every Nth tick (see [Networking](Features/Networking.md))
- Device input is sampled into a per-tick input command for each player. The simulation reads only input commands, never devices - the same path serves local play, client input on the server, prediction replays and input sent through MCP
- The simulation state (components, plus physics through Jolt's state recording) can be saved and restored, so clients can roll back and re-simulate ticks for prediction (see [Prediction](Features/Networking.md#prediction))
- How ticks, input commands, interpolation and snapshots work: [Decision 0012](Decisions/0012-simulation-and-rollback.md)

## Components and Reflection
- Each component and its fields are registered once in a reflection registry: names, types, defaults, valid ranges, and flags such as whether a field replicates. It's a small custom registry (`ComponentRegistry`), not `entt::meta` - see [Decision 0010](Decisions/0010-component-reflection.md)
- Serialization, the inspector, MCP tools, script bindings and network replication are all driven by the registry, so a new component gets them from its registration. A component can add custom handling where the generic path isn't enough

## Asset System
- Every asset has a stable UUID. Scenes, prefabs and other assets reference each other by UUID, never by file path
- An asset registry maps UUIDs to files and stores import settings in a metadata file next to each asset
- Importers for glTF models (including skeletons and animation clips), textures (PNG/JPG), HDRIs (.hdr), audio (WAV/FLAC/MP3), fonts (TTF) and Lua scripts
- Hot reload in the editor for scripts, shaders and textures
- Export packs all assets used by the game into a pack file

## File Formats
- Every file the engine writes - scenes, prefabs, the project file and asset metadata - has a `version` field
- Loading an older version runs migrations one version at a time. Each migration has a test against a fixture file in the old format
- A file newer than the engine supports fails to load with a clear error - never a partial load
- The formats themselves - scenes (`.lscene`), asset metadata (`.meta`) - and how loading validates them: [Decision 0011](Decisions/0011-file-formats.md)

## Repository Layout
- Sample game projects, including the [Milestone 13](Milestones.md#13-validation) games, live in `Samples/` in this repository. They double as test fixtures
