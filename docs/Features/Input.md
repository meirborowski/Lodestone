# Input

- Keyboard, mouse (position, buttons, scroll, cursor lock) and gamepads (via GLFW)
- Device input is sampled once per simulation tick into an input command (see [Simulation](../Architecture.md#simulation)). The simulation and gameplay scripts read input only through it, so input works the same locally, on a server, during prediction replays and when sent through MCP
- Scripts can query whether a key/button was pressed, held or released this tick
- Simple named action mapping (e.g. `Jump` → Space / gamepad A)
- AI agents can send input during play mode through MCP, so they can playtest (see [AI Control](../AIControl.md))
