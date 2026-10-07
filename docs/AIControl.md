# AI Control

The editor needs to be fully controllable by AI agents - I should be able to ask you to build me a game like Tetris, and you should have all the tools available to do so without my intervention.

- The editor embeds an MCP (Model Context Protocol) server, bound to localhost only, so AI agents like Claude Code can drive it. Check a `.mcp.json` into the repo so Claude Code connects to it automatically
- The MCP tools cover everything a person can do in the editor: creating/editing/deleting entities and components, importing assets, writing scripts, creating and instancing prefabs, saving/loading scenes, entering/exiting play mode, sending input during play mode (so the agent can playtest), capturing screenshots of the viewport, reading logs, and exporting
- For multiplayer, the MCP tools can start multiplayer play mode (a host or dedicated server plus clients), set network simulation (latency, jitter, packet loss), and send input to, screenshot, and read logs from each instance separately
- MCP operations go through the same command and undo system as the editor UI
- A headless mode (`--headless`) runs the editor without a window, rendering offscreen, so agents and CI can use it without a display
- Every MCP tool has automated tests
