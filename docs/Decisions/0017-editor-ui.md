# 0017 - Editor UI: Dear ImGui on NVRHI, and a Debug Scene View

## Status
Accepted

## Context
Milestone 4 needs a docked editor - viewport, hierarchy, inspector, content browser, console - with a gizmo, before the scene renderer exists (Milestone 5). The UI renders through NVRHI on Vulkan (see [Decision 0007](0007-rendering-backend.md)), must be testable without a GPU where possible, and its asserts must never open dialogs that hang CI or agents.

## Decision
- **Dear ImGui (docking branch) with ImGuizmo** - drawn by our own NVRHI renderer (`ImGuiRenderer`), which supports Dear ImGui 1.92's texture protocol (it creates and updates Dear ImGui's textures, such as the font atlas, when asked) and shows engine textures with `ImGui::Image`. Dear ImGui's GLFW backend feeds it input. Its asserts go through the engine's assert handler (`IMGUI_USER_CONFIG`), like EnTT's
- **The inspector is generated from the reflection registry** - a widget per field type (rotations as Euler angles in degrees), read-only fields disabled, and every edit a command; dragging is one undo step
- **A debug scene view until Milestone 5** - `DebugSceneRenderer` draws a ground grid and a lit cube per entity at its world transform, the selection highlighted; `SceneView` renders it into the viewport's render target and into a separate one for screenshots. Clicking picks the nearest cube under the cursor
- **Layout** - a default layout is built with Dear ImGui's dock builder the first time; afterwards Dear ImGui saves it per user (`%APPDATA%/Lodestone`, `~/Library/Application Support/Lodestone` or `$XDG_CONFIG_HOME/lodestone`), not in projects
- **No native file dialogs yet** - projects are created and opened by typing a path, and documents are saved under the project's `Assets` directory by relative path, as MCP tools address them

## Alternatives
- **Dear ImGui's Vulkan backend** - bypasses NVRHI's resource tracking and state, so the viewport texture and the UI would need manual synchronization
- **A retained-mode UI toolkit (Qt)** - heavy, LGPL, and another rendering path
- **nativefiledialog-extended** for file dialogs - can be added later; typed paths work for now, and agents don't need dialogs

## Consequences
- The UI's panels are tested headless: Dear ImGui runs frames without a renderer, and tests check panels draw in every state and shortcuts act (`Tests/Editor/EditorUITests.cpp`). The renderers have reference-image tests on lavapipe
- Milestone 5 replaces the debug scene view's renderer with the scene renderer; `SceneView` keeps its interface (viewport, screenshots, picking - which will then use real geometry)
- The stats overlay shows frame time and draw calls for now; CPU and GPU timings come with the renderer
