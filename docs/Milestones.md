# Milestones

Work through these in order. Don't start a milestone until the previous one is done.

A milestone is done when:
- Its features are complete
- Tests pass on CI on all three platforms
- The test scene covers the new features
- AGENTS.md, the docs and the skills are updated
- The work has been code reviewed, committed and pushed

Tick a milestone (`[x]`) when it meets the definition of done.

## Before Starting
Lua is the planned scripting language, but this is open for discussion - if you'd recommend something else, propose it with your reasoning before starting Milestone 1 (see [Scripting](Features/Scripting.md)).

## Progress
- [ ] **1. Foundation** - CMake project, dependency fetching, logging, asserts, doctest, GitHub Actions CI on all platforms, AGENTS.md / CLAUDE.md / skills, THIRD_PARTY_LICENSES.md, Git LFS
- [ ] **2. Window, input and rendering backend** - GLFW window, input, nvrhi Vulkan device and swapchain, shader compilation, offscreen/headless rendering, first reference-image test
- [ ] **3. ECS, scenes and assets** - EnTT, core components (ID, name, transform, hierarchy), scene save/load with round-trip tests, asset registry and UUIDs
- [ ] **4. Editor and AI control** - editor panels, gizmo, undo/redo, play mode, prefabs, MCP server and headless mode
- [ ] **5. 3D renderer** - glTF import, PBR, IBL, soft shadows, SSAO, HDR and tonemapping
- [ ] **6. Animation** - glTF skeleton and clip import, animator component, GPU skinning, editor preview
- [ ] **7. Physics** - Jolt integration, components and events
- [ ] **8. Scripting** - full scripting API (including animation and physics) and hot reload
- [ ] **9. Audio** - components, spatial audio, scripting API
- [ ] **10. In-game UI and text** - fonts, UI elements, layout, scripting API
- [ ] **11. Networking** - ENet transport, replication, interpolation, prediction, RPCs, listen and dedicated servers, multiplayer play mode, network simulation
- [ ] **12. Export** - asset packing, Dist runtime and dedicated server builds, export from editor/MCP/CLI
- [ ] **13. Validation** - using only the MCP tools, build and export two complete small games: a single-player game (e.g. Tetris), and a multiplayer 3D game with animated characters. Fix every gap found along the way
