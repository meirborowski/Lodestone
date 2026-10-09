# 0016 - The Editor's MCP Server

## Status
Accepted

## Context
The editor must be fully controllable by AI agents through the Model Context Protocol, over stdio (`--headless --mcp-stdio`, so `.mcp.json` can start a headless editor) and Streamable HTTP on localhost (to attach to a running editor), with protection against DNS rebinding (see [AI Control](../AIControl.md)). Every tool needs in-process tests, and each transport end-to-end tests.

## Decision
- **Our own protocol layer** - `McpServer` handles JSON-RPC 2.0 messages: `initialize` (protocol versions 2025-11-25, 2025-06-18 and 2025-03-26, answering with the newest when the client's is unknown), `ping`, `tools/list` and `tools/call`. Tools are the only capability. A tool returns content (text, images) and, for data, the same as structured content; a tool that fails returns an error result the agent can read and correct, rather than a protocol error. Exceptions in tools become error results, never crashes
- **Transports** - stdio reads one message per line on a thread of its own and writes answers to standard output, which carries nothing else (the log goes to standard error); the editor exits when its standard input closes. HTTP serves `POST /mcp` with cpp-httplib on 127.0.0.1: a session starts with `initialize` and is named by `Mcp-Session-Id` afterwards; `GET` (event streams) is refused, since the server never starts conversations; `DELETE` ends a session
- **Security** - before routing, the HTTP transport refuses any request whose `Host` isn't `localhost` or `127.0.0.1` with the server's port, and any request with an `Origin` that isn't a localhost origin (403), so web pages can't reach it through DNS rebinding. The port is bound exclusively - cpp-httplib's default socket options would let another process share it (`SO_REUSEPORT`) or take it over (`SO_REUSEADDR` on Windows). Requests are limited to 16 MB
- **Port 7850** - the windowed and plain headless editors listen on 7850 by default, which `.mcp.json` points at; `--mcp-port` picks another (0 for any free port, which the editor logs). When 7850 is taken (another editor), the windowed editor runs without HTTP rather than failing. The stdio editor serves no HTTP unless asked, so clients can each start one
- **Tools** - generic component tools driven by the reflection registry (`component_types`, `component_add`, `component_set`, `component_remove`), plus tools for projects, documents, entities, prefabs, undo, play mode, input, screenshots, the camera, logs, assets and quitting. Paths are relative to the project's `Assets` directory and can't leave it. Every change goes through the editor's commands (see [Decision 0015](0015-editor-commands.md))
- **Screenshots** - rendered offscreen from the editor camera at any size, so they never disturb the viewport. The headless editor starts its graphics device on the first screenshot, so it starts fast and runs where there's no GPU until one is asked for
- **Tests** - every tool is called in-process (`LodestoneEditorTests`), including with wrongly typed arguments; the HTTP transport is tested over loopback sockets, security checks included (`LodestoneEditorIntegrationTests`); and an agent smoke test in Python (`Tests/Editor/McpAgentSmokeTest.py`) starts the real headless editor and builds, saves, reloads, plays and screenshots a scene over each transport, as Claude Code would. Python is already a build prerequisite, and its standard library has everything a separate client needs

## Alternatives
- **An MCP SDK** - there's no official C++ SDK, and the third-party ones bring their own JSON and HTTP stacks and lag the protocol; the part we need (tools only, no server-initiated messages) is small
- **Server-sent events on the HTTP transport** - only needed for server-initiated messages (notifications, sampling), which the editor doesn't send. Can be added if tools ever report progress
- **WebSockets** - not an MCP transport

## Consequences
- New editor features add MCP tools with in-process tests (see [Milestones](../Milestones.md)); new components get MCP support from their registration alone
- The tool list in `Tests/Editor/EditorToolsTests.cpp` is the inventory: adding a tool means adding it there, with tests
- Multiplayer play mode (Milestone 11) needs tools that address each instance; they'll take an instance argument rather than separate servers
