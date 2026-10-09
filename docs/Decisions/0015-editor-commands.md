# 0015 - Editor Commands, Undo and Play Mode

## Status
Accepted

## Context
Every editing operation needs undo and redo, including those MCP clients make (see [AI Control](../AIControl.md)), and the editor UI and MCP must see and change the same state. Play mode runs the simulation on the scene being edited, and stopping must restore it exactly. The editor's state isn't thread-safe, but MCP requests arrive on transport threads.

## Decision
- **One editor context** - `EditorContext` holds the project, the open document (a scene, or a prefab opened for editing), the selection, the undo history, the camera and play mode. The UI and the MCP tools are both thin layers over it
- **Commands** - every change to a document is a `Command` run by the context's `CommandHistory`: create, delete, duplicate, reparent, add and remove components, set fields, and instance prefabs. Commands refer to entities by UUID, never by handle, so they survive deletes and undos. A command either succeeds or changes nothing, and running it again (redo) gives the same result, UUIDs included. Selection and the camera aren't commands: they don't change the document
- **Continuous edits merge** - a gizmo drag or a dragged field sends a command every frame; consecutive continuous `SetFieldsCommand`s on the same fields merge into one undo step, until the UI ends the merge when the drag ends
- **Unsaved changes** - the history gives each state an ID; the document has unsaved changes when the current ID differs from the one it was saved at, so undoing back to the saved state counts as saved. Tools that would replace a document with unsaved changes refuse unless told to discard them
- **Play mode** - starting saves a snapshot of the scene (every registered component, internal ones too, and entity handles - see [Decision 0012](0012-simulation-and-rollback.md)) and runs a `Simulation` on the scene itself; stopping restores the snapshot. Commands, undo and redo are refused while playing, since anything changed during play is thrown away. Modules that take part in play mode (physics, scripting) add systems with `AddPlaySystem()`. Input comes from MCP's queue first - one command per tick, so agents can playtest deterministically, starting paused if they like - then from the viewport while it has focus
- **The main thread owns the state** - MCP transports hand each request to `MainThreadDispatcher`, which the editor's main loop pumps, and wait for the answer. Requests still waiting when the editor stops get nothing, so the transports can shut down

## Alternatives
- **Snapshot undo** (save the whole scene before each change) - simple and always correct, but costs memory and time in proportion to the scene for every edit, including every frame of a drag
- **Running MCP tools on their transport threads with a lock** - needs every UI path to take the lock too, and makes rendering and tools contend; one owning thread is simpler and can't deadlock
- **Copying the scene for play mode** (play on a copy, keep the edited scene aside) - needs every system that refers to the scene (the viewport, tools) to switch scenes; restoring a snapshot in place keeps entity handles stable instead

## Consequences
- A new editing operation is a new command, with tests that undo restores the scene exactly and redo repeats it exactly
- Undo history is per document and cleared when another document opens
- Live edits during play mode (tweaking a value while the game runs) aren't possible; they'd need changes that bypass the history and are kept or thrown away explicitly
