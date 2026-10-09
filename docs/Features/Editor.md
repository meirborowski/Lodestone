# Editor

- Docked layout with a viewport, scene hierarchy, inspector, content browser, and console/log panel
- Gizmo to translate, rotate and scale entities in the viewport
- Undo/redo for every editing operation, including those made through MCP
- Play mode runs on a copy of the scene; stopping restores the edited scene exactly as it was
- Prefabs: create from an entity, edit, and instance into scenes (or spawn from scripts)
- Stats overlay showing frame time, CPU/GPU timings and draw calls
- Animation preview with a timeline to scrub through clips (see [Animation](Animation.md))
- Multiplayer play mode, network simulation settings, and a network stats overlay (see [Networking](Networking.md))
- Everything the editor can do must also be available to AI agents through MCP (see [AI Control](../AIControl.md))

## Using the Editor
Run `build/<preset>/Source/Editor/LodestoneEditor`, optionally with `--project <directory>`. Create or open a project from the File menu; scenes and prefabs are saved in its `Assets` directory. The layout is saved per user, and View > Reset Layout restores the default.

- **Viewport** - right drag orbits, middle drag pans, the wheel zooms, and clicking selects. With the viewport focused, W, E and R pick the gizmo's operation (move, rotate, scale) and F frames the selection; the toolbar switches between local and world axes, and turns snapping and the stats overlay on and off. Prefabs dragged from the content browser are instanced. While playing, the viewport takes the keyboard and mouse when it has focus
- **Hierarchy** - drag entities onto each other to reparent them, or onto the empty space below to make them roots; right-click for create, duplicate, unparent and delete
- **Inspector** - every component's fields, from the reflection registry. Right-click a component's header to remove it
- **Content browser** - double-click scenes and prefabs to open them; right-click to instance prefabs or copy paths and UUIDs
- **Shortcuts** - Ctrl+N new scene, Ctrl+S save, Ctrl+Shift+S save as, Ctrl+Z undo, Ctrl+Y or Ctrl+Shift+Z redo, Ctrl+D duplicate, Delete delete, Ctrl+P play and stop (Cmd instead of Ctrl on macOS)

Until the scene renderer arrives (Milestone 5), entities show as lit cubes on a ground grid (see [Decision 0017](../Decisions/0017-editor-ui.md)). Every action here is also an MCP tool (see [AI Control](../AIControl.md)).
