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

`.mcp.json` configures both.

## Security
Binding to localhost isn't enough on its own: a web page in the user's browser can reach localhost servers through DNS rebinding. The HTTP transport rejects any request whose `Host` header isn't `localhost` or `127.0.0.1` with the server's port, and any request carrying an `Origin` header that isn't a localhost origin.

## Tools
- Component tools are generic and driven by the reflection registry (see [Components and Reflection](Architecture.md#components-and-reflection)): agents query the component schemas and get or set fields, so a new component gets MCP support from its registration, without a new tool
- Features that aren't components - play mode, input, screenshots, logs, export and multiplayer - have their own tools

## Testing
- Every MCP tool has in-process tests that call it directly
- Each transport has end-to-end tests
