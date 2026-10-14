# AI Control

The editor needs to be fully controllable by AI agents - I should be able to ask you to build me a game like Tetris, and you should have all the tools available to do so without my intervention.

- The editor embeds an MCP (Model Context Protocol) server so AI agents like Claude Code can drive it. Check a `.mcp.json` into the repo so Claude Code connects to it automatically
- The MCP tools cover everything a person can do in the editor: creating/editing/deleting entities and components, importing assets, writing scripts, creating and instancing prefabs, saving/loading scenes, entering/exiting play mode, sending input during play mode (so the agent can playtest), capturing screenshots of the viewport, reading logs, and exporting
- For multiplayer, the MCP tools can start multiplayer play mode (a host or dedicated server plus clients), set network simulation (latency, jitter, packet loss), and send input to, screenshot, and read logs from each instance separately
- MCP operations go through the same command and undo system as the editor UI
- A headless mode (`--headless`) runs the editor without a window, rendering offscreen, so agents and CI can use it without a display
- Every milestone adds MCP tools for its new editor-facing features (see the definition of done in [Milestones](Milestones.md))

## Transports
- **Streamable HTTP**, bound to localhost only, for attaching to a running editor
- **stdio** (`--headless --mcp-stdio`), so `.mcp.json` can launch a headless editor directly

`.mcp.json` configures both:
- `lodestone-editor` - the editor that's running, at `http://127.0.0.1:7850/mcp`. The windowed editor and `--headless` listen on port 7850 unless `--mcp-port` says otherwise (0 picks a free port, which the editor logs); when 7850 is taken, the windowed editor runs without HTTP and logs a warning
- `lodestone-headless` - a headless editor that the client starts and talks to over stdio (`build/debug/Source/Editor/LodestoneEditor --headless --mcp-stdio`, so build the `debug` preset first). It renders screenshots offscreen, starting its graphics device on the first one, and exits when the client closes its standard input. It serves no HTTP unless given `--mcp-port`

Other options: `--project <directory>` opens a project at startup, and `--vulkan-driver <library>` renders with a specific Vulkan driver (e.g. lavapipe, where there's no GPU). The headless editor without stdio runs until `editor_quit`, Ctrl+C or SIGTERM. See [Decision 0016](Decisions/0016-mcp-server.md).

## Security
Binding to localhost isn't enough on its own: a web page in the user's browser can reach localhost servers through DNS rebinding. The HTTP transport rejects any request whose `Host` header isn't `localhost` or `127.0.0.1` with the server's port, and any request carrying an `Origin` header that isn't a localhost origin. It binds its port exclusively, so no other process can share or take it over, and limits requests to 16 MB.

Tools only touch files inside the open project's `Assets` directory: paths are relative to it and can't leave it. `project_create`, `project_open` and `asset_import` (its source file) are the exceptions, since they name places outside any project.

## Tools
- Component tools are generic and driven by the reflection registry (see [Components and Reflection](Architecture.md#components-and-reflection)): agents query the component schemas and get or set fields, so a new component gets MCP support from its registration, without a new tool
- Features that aren't components - play mode, input, screenshots, logs, export and multiplayer - have their own tools

The tools so far (`Source/Editor/Lodestone/Editor/Mcp/EditorTools.cpp`):

| Area | Tools |
|---|---|
| Project and editor | `project_info`, `project_create`, `project_open`, `editor_quit` |
| Documents | `scene_new`, `scene_open`, `prefab_open`, `document_save`, `scene_hierarchy` |
| Entities | `entity_create`, `entity_delete`, `entity_duplicate`, `entity_set_parent`, `entity_get`, `entity_find`, `selection_set` |
| Components | `component_types`, `component_add`, `component_set`, `component_remove` |
| Prefabs and models | `prefab_create`, `prefab_instantiate`, `model_instantiate` |
| Undo | `edit_undo`, `edit_redo`, `edit_history` |
| Play mode | `play_start` (optionally paused), `play_stop`, `play_pause`, `play_step`, `play_status`, `input_send` |
| Animation | `animation_clips`, `animation_preview` |
| Physics | `physics_raycast`, `physics_events` |
| View and rendering | `viewport_screenshot`, `render_stats`, `camera_set`, `camera_frame` |
| Logs and assets | `log_read`, `asset_list`, `asset_import`, `asset_rescan` |

Entities are addressed by UUID. Tools that replace the open document refuse while it has unsaved changes, unless given `discardChanges: true`. A tool that fails returns an error result saying why, so agents can correct themselves. To add a tool, see the `add-mcp-tool` skill.

Rendering is set up with the generic component tools: `MeshRenderer` (a model asset's UUID and a mesh index), `Camera`, `DirectionalLight`, `PointLight`, `SpotLight`, `Environment` (an HDR environment asset) and `RenderSettings` (exposure, tonemapper, ambient occlusion, shadows). `asset_import` brings models (.gltf, .glb) and environments (.hdr) into the project, `model_instantiate` places a model's nodes as entities, `viewport_screenshot` shows the lit result with the editor's overlay, and `render_stats` reports draw calls, triangles, meshes seen and CPU and GPU times.

Animated models play their clips through an `Animator` on the model instance's root (set its `Clip`, `Time`, `Speed`, `Playing` and `Loop` fields with `component_set`); `animation_clips` lists a model's clips by index, name and length, with the animator's state, and `animation_preview` poses the model with a clip at a time - in the viewport and in screenshots - without changing the document, as the editor's timeline does (see [Decision 0020](Decisions/0020-skeletal-animation.md)).

Physics is set up with the generic component tools too: `RigidBody` (static, kinematic or dynamic; a trigger with `IsTrigger`), the colliders `BoxCollider`, `SphereCollider`, `CapsuleCollider` and `MeshCollider`, `CharacterController` (a character driven by a player's input: WASD or the left stick walk, the mouse or the right stick turn, Space or gamepad A jump) and `PhysicsSettings` (gravity). Play mode simulates them; `physics_raycast` casts a ray into the running world, or while editing into the document's colliders where they are, and reports the entity, point, normal and distance it hits; `physics_events` lists play mode's latest collision and trigger events with their ticks, so an agent can check what touched what during a playtest (see [Decision 0021](Decisions/0021-physics.md)).

## Testing
- Every MCP tool has in-process tests that call it directly (`Tests/Editor/EditorToolsTests.cpp`), including with missing and wrongly typed arguments
- Each transport has end-to-end tests: the HTTP transport over loopback, with the security checks (`Tests/Editor/HttpTransportTests.cpp`), and the agent smoke test, which drives the real headless editor over stdio and over HTTP from a separate process (`Tests/Editor/McpAgentSmokeTest.py`): it imports a model and builds a scene with entities, components - meshes and lights among them - prefab instances and a model instance, saves and reloads it, screenshots it, reads the renderer's stats, plays it with input, previews an animated model's clips, and drops a body through a trigger onto a collider, checking raycasts and physics events
