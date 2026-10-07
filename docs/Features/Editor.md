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
