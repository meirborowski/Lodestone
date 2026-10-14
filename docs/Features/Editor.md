# Editor

- Docked layout with a viewport, scene hierarchy, inspector, content browser, and console/log panel
- Gizmo to translate, rotate and scale entities in the viewport
- Undo/redo for every editing operation, including those made through MCP
- Play mode runs on a copy of the scene; stopping restores the edited scene exactly as it was
- Prefabs: create from an entity, edit, and instance into scenes (or spawn from scripts)
- Stats overlay showing frame time, CPU/GPU timings and draw calls
- Animation preview with a timeline to scrub through clips (see [Animation](Animation.md))
- Physics in play mode, with the selection's colliders drawn in the viewport (see [Physics](Physics.md))
- Multiplayer play mode, network simulation settings, and a network stats overlay (see [Networking](Networking.md))
- Everything the editor can do must also be available to AI agents through MCP (see [AI Control](../AIControl.md))

## Using the Editor
Run `build/<preset>/Source/Editor/LodestoneEditor`, optionally with `--project <directory>`. Create or open a project from the File menu; scenes and prefabs are saved in its `Assets` directory. The layout is saved per user, and View > Reset Layout restores the default.

- **Viewport** - the scene as the renderer draws it, with a ground grid, a marker for each entity that draws nothing of its own (lights yellow, cameras blue, others grey, with a line showing which way lights and cameras face) and the selection outlined in orange, with its colliders in green (a character's capsule standing on its position). Right drag orbits, middle drag pans, the wheel zooms, and clicking selects what's under the cursor - meshes by their triangles, other entities by their markers. With the viewport focused, W, E and R pick the gizmo's operation (move, rotate, scale) and F frames the selection; the toolbar switches between local and world axes, and turns snapping and the stats overlay on and off. The stats overlay shows the frame time, the renderer's CPU and GPU time (per pass), draw calls, triangles, and meshes and lights. Prefabs and models dragged from the content browser are instanced. While playing, the viewport takes the keyboard and mouse when it has focus
- **Hierarchy** - drag entities onto each other to reparent them, or onto the empty space below to make them roots; right-click for create, duplicate, unparent and delete
- **Inspector** - every component's fields, from the reflection registry. Right-click a component's header to remove it. Fields that refer to assets by UUID - a mesh renderer's model, an environment - show the asset's path, and take an asset dragged onto them from the content browser
- **Animation** - the timeline of the selected entity's animator (its own, or its model instance's): pick a clip, play it or scrub through it, previewed in the viewport without changing the document; while playing, it follows the running animator
- **Content browser** - double-click scenes and prefabs to open them; right-click to instance prefabs and models or copy paths and UUIDs; drag assets into the viewport, the hierarchy or the inspector
- **Shortcuts** - Ctrl+N new scene, Ctrl+S save, Ctrl+Shift+S save as, Ctrl+Z undo, Ctrl+Y or Ctrl+Shift+Z redo, Ctrl+D duplicate, Delete delete, Ctrl+P play and stop (Cmd instead of Ctrl on macOS)

Models (.gltf, .glb) and environments (.hdr) are assets like any other: put them in the project's `Assets` directory, or import them with MCP. A model placed in a scene becomes an entity per node, each drawing its mesh with a `MeshRenderer` (see [Decision 0019](../Decisions/0019-model-import.md)). A model with clips gets an `Animator` on its root, playing its first; skinned meshes bend with their joints, which are the model's node entities (see [Decision 0020](../Decisions/0020-skeletal-animation.md)). Physics components - rigid bodies, colliders, triggers and character controllers - are added in the inspector like any other, and simulate in play mode after gameplay and before animation (see [Decision 0021](../Decisions/0021-physics.md)). Every action here is also an MCP tool (see [AI Control](../AIControl.md)).
