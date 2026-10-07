# Scripting

- Scripting with Lua via sol2. Lua is open for discussion - if you'd recommend something else, propose it with your reasoning before starting
- Control entities/components, run in the update loop, and handle entity creation/destruction
- Spawn new entities/prefabs
- Access to input, physics, animation, audio, UI and networking from scripts
- Scripts hot reload in the editor
- Script errors are reported in the console with file and line number, and never crash the editor or the game
- The test scene exercises the entire scripting API (see [Testing & CI](../Testing.md))
