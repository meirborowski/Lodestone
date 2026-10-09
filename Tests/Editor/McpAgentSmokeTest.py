"""The agent smoke test: Milestone 4's acceptance test (see docs/Milestones.md).

An agent - this script - starts the headless editor and builds a scene with MCP tools alone: it creates entities, sets
their components, makes a prefab and instances it, saves the scene, reloads it, plays it with input, and screenshots
it, checking the result of every step. It talks to the editor as Claude Code would, over stdio (the editor started with
--headless --mcp-stdio) or over HTTP (the editor started with --headless --mcp-port 0), so it also tests both
transports end to end.

    python McpAgentSmokeTest.py --editor <LodestoneEditor> --transport stdio|http [--vulkan-driver <library>]
"""

import argparse
import base64
import json
import pathlib
import queue
import re
import struct
import subprocess
import sys
import tempfile
import threading
import urllib.error
import urllib.request
import zlib

# How long the editor may take to answer one request, in seconds. Screenshots on a CPU renderer are the slowest
TIMEOUT = 120
PROTOCOL_VERSION = "2025-11-25"


class SmokeTestFailure(Exception):
    pass


def check(condition, message):
    if not condition:
        raise SmokeTestFailure(message)


class EditorProcess:
    """The headless editor, with its output collected on threads so its pipes never fill up."""

    def __init__(self, arguments, stdout_is_log):
        self.process = subprocess.Popen(arguments, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE)
        self.stdout_lines = queue.Queue()
        self.log = []

        def take_stdout_line(line):
            # Over stdio, the standard output carries only MCP messages; otherwise it's the log
            if stdout_is_log:
                self.log.append(line)
            self.stdout_lines.put(line)

        threading.Thread(target=self._read, args=(self.process.stdout, take_stdout_line), daemon=True).start()
        threading.Thread(target=self._read, args=(self.process.stderr, self.log.append), daemon=True).start()

    @staticmethod
    def _read(stream, sink):
        for line in iter(stream.readline, b""):
            sink(line.decode("utf-8", errors="replace").rstrip("\r\n"))
        sink(None)

    def next_stdout_line(self):
        try:
            line = self.stdout_lines.get(timeout=TIMEOUT)
        except queue.Empty:
            raise SmokeTestFailure(f"The editor didn't answer within {TIMEOUT} s") from None
        check(line is not None, "The editor closed its standard output")
        return line

    def wait(self):
        try:
            return self.process.wait(timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            self.process.kill()
            raise SmokeTestFailure("The editor didn't exit") from None

    def kill(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()


class StdioClient:
    """MCP over the editor's standard input and output: one JSON-RPC message per line."""

    def __init__(self, editor, driver_arguments):
        self.editor = EditorProcess([editor, "--headless", "--mcp-stdio", *driver_arguments], stdout_is_log=False)

    def send(self, message):
        self.editor.process.stdin.write((json.dumps(message) + "\n").encode("utf-8"))
        self.editor.process.stdin.flush()
        if "id" not in message:
            return None
        response = json.loads(self.editor.next_stdout_line())
        check(response.get("id") == message["id"], f"The response {response} doesn't answer {message['id']}")
        return response

    def close(self):
        # The stdio editor exits when its client closes standard input
        self.editor.process.stdin.close()
        return self.editor.wait()


class HttpClient:
    """MCP over Streamable HTTP, with a session, against the editor's localhost server."""

    def __init__(self, editor, driver_arguments):
        self.editor = EditorProcess([editor, "--headless", "--mcp-port", "0", *driver_arguments], stdout_is_log=True)
        # The editor logs its URL once it's listening; port 0 picks a free port
        while True:
            line = self.editor.next_stdout_line()
            match = re.search(r"MCP server listening on (http://127\.0\.0\.1:\d+/mcp)", line)
            if match:
                self.url = match.group(1)
                break
        self.session = None

    def post(self, message, headers=None):
        request = urllib.request.Request(self.url, data=json.dumps(message).encode("utf-8"), method="POST")
        request.add_header("Content-Type", "application/json")
        request.add_header("Accept", "application/json, text/event-stream")
        if self.session:
            request.add_header("Mcp-Session-Id", self.session)
            request.add_header("MCP-Protocol-Version", PROTOCOL_VERSION)
        for name, value in (headers or {}).items():
            request.add_header(name, value)
        with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
            if response.headers.get("Mcp-Session-Id"):
                self.session = response.headers["Mcp-Session-Id"]
            body = response.read()
            return response.status, json.loads(body) if body else None

    def send(self, message):
        status, response = self.post(message)
        if "id" not in message:
            check(status == 202 and response is None, f"A notification got {status}: {response}")
            return None
        check(status == 200, f"A request got {status}")
        check(response.get("id") == message["id"], f"The response {response} doesn't answer {message['id']}")
        return response

    def check_security(self):
        # A web page's request - another Origin - is refused before anything else happens
        try:
            self.post({"jsonrpc": "2.0", "id": 0, "method": "ping"}, {"Origin": "https://evil.example.com"})
            raise SmokeTestFailure("A request from another origin was accepted")
        except urllib.error.HTTPError as error:
            check(error.code == 403, f"A request from another origin got {error.code}, not 403")

    def close(self, agent):
        # Asked to quit, the HTTP editor answers and exits
        agent.tool("editor_quit", {"discardChanges": True})
        return self.editor.wait()


class Agent:
    def __init__(self, client):
        self.client = client
        self.next_id = 0

    def request(self, method, params=None):
        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        if params is not None:
            message["params"] = params
        response = self.client.send(message)
        check("error" not in response, f"{method} failed: {response.get('error')}")
        return response["result"]

    def tool(self, name, arguments=None):
        result = self.request("tools/call", {"name": name, "arguments": arguments or {}})
        texts = [content["text"] for content in result["content"] if content["type"] == "text"]
        check(not result["isError"], f"{name} failed: {' '.join(texts)}")
        return result

    def data(self, name, arguments=None):
        result = self.tool(name, arguments)
        check("structuredContent" in result, f"{name} returned no structured content")
        return result["structuredContent"]

    def failing_tool(self, name, arguments):
        result = self.request("tools/call", {"name": name, "arguments": arguments})
        check(result["isError"], f"{name} should have failed with {arguments}")


def decode_png(data):
    """The size and RGBA pixels of an 8-bit RGBA PNG without interlacing, as the editor writes them."""
    check(data[:8] == b"\x89PNG\r\n\x1a\n", "The screenshot isn't a PNG")
    position = 8
    width = height = None
    compressed = b""
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        chunk = data[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            check(depth == 8 and color == 6 and interlace == 0, "The screenshot isn't 8-bit RGBA")
        elif kind == b"IDAT":
            compressed += chunk
        elif kind == b"IEND":
            break
    raw = zlib.decompress(compressed)
    stride = width * 4
    pixels = bytearray()
    previous = bytearray(stride)
    for row in range(height):
        start = row * (stride + 1)
        kind = raw[start]
        line = bytearray(raw[start + 1:start + 1 + stride])
        for index in range(stride):
            left = line[index - 4] if index >= 4 else 0
            up = previous[index]
            up_left = previous[index - 4] if index >= 4 else 0
            if kind == 1:
                line[index] = (line[index] + left) & 0xFF
            elif kind == 2:
                line[index] = (line[index] + up) & 0xFF
            elif kind == 3:
                line[index] = (line[index] + (left + up) // 2) & 0xFF
            elif kind == 4:
                estimate = left + up - up_left
                distances = (abs(estimate - left), abs(estimate - up), abs(estimate - up_left))
                predictor = (left, up, up_left)[distances.index(min(distances))]
                line[index] = (line[index] + predictor) & 0xFF
        pixels += line
        previous = line
    return width, height, bytes(pixels)


def screenshot(agent, width, height):
    result = agent.tool("viewport_screenshot", {"width": width, "height": height})
    images = [content for content in result["content"] if content["type"] == "image"]
    check(len(images) == 1 and images[0]["mimeType"] == "image/png", "The screenshot has no PNG")
    size_x, size_y, pixels = decode_png(base64.b64decode(images[0]["data"]))
    check((size_x, size_y) == (width, height), f"The screenshot is {size_x}x{size_y}, not {width}x{height}")
    return pixels


def find_entities(hierarchy, name):
    return [entity for entity in hierarchy["entities"] if entity["name"] == name]


def run(agent, project_directory, client):
    # The handshake, and the tools an agent discovers
    initialized = agent.request("initialize", {"protocolVersion": PROTOCOL_VERSION, "capabilities": {},
                                               "clientInfo": {"name": "Lodestone smoke test", "version": "1"}})
    check(initialized["protocolVersion"] == PROTOCOL_VERSION, "The editor didn't agree on the protocol version")
    check(initialized["serverInfo"]["name"] == "Lodestone Editor", "The server isn't the editor")
    agent.client.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
    tools = {tool["name"] for tool in agent.request("tools/list")["tools"]}
    for needed in ["project_create", "entity_create", "component_set", "prefab_create", "prefab_instantiate",
                   "document_save", "scene_open", "viewport_screenshot", "play_start", "input_send"]:
        check(needed in tools, f"The editor has no {needed} tool")

    # A project, and what components exist
    agent.tool("project_create", {"directory": str(project_directory), "name": "Smoke Test"})
    components = {component["name"] for component in agent.data("component_types")["components"]}
    check({"Name", "Transform", "Hierarchy"} <= components, f"Missing core components in {components}")

    # Entities with components: a ground, and a tower with a light on top
    ground = agent.data("entity_create", {"name": "Ground",
                                          "components": {"Transform": {"Scale": [12, 0.2, 12]}}})["id"]
    tower = agent.data("entity_create", {"name": "Tower"})["id"]
    agent.data("component_set", {"entity": tower, "component": "Transform",
                                 "fields": {"Position": [-3, 1.5, 0], "Scale": [1, 3, 1]}})
    agent.data("entity_create", {"name": "Light", "parent": tower,
                                 "components": {"Transform": {"Position": [0, 0.6, 0], "Scale": [0.5, 0.1, 0.5]}}})
    agent.failing_tool("component_set", {"entity": tower, "component": "Transform", "fields": {"Position": "up"}})

    # A prefab of the tower, instanced twice
    prefab = agent.data("prefab_create", {"entity": tower, "path": "Prefabs/Tower.lprefab"})
    instances = []
    for x in (0, 3):
        instance = agent.data("prefab_instantiate", {"path": "Prefabs/Tower.lprefab"})["id"]
        agent.data("component_set", {"entity": instance, "component": "Transform", "fields": {"Position": [x, 1.5, 0]}})
        instances.append(instance)

    # Edits can be undone
    agent.data("entity_delete", {"entity": ground})
    agent.data("edit_undo")

    # Saved, then reloaded into a fresh document
    agent.tool("document_save", {"path": "Scenes/Main.lscene"})
    agent.tool("scene_new")
    check(agent.data("project_info")["document"]["entityCount"] == 0, "The new scene isn't empty")
    agent.tool("scene_open", {"path": "Scenes/Main.lscene"})

    hierarchy = agent.data("scene_hierarchy")
    names = [entity["name"] for entity in hierarchy["entities"]]
    check(names == ["Ground", "Tower", "Light", "Tower", "Light", "Tower", "Light"], f"The scene reloaded as {names}")
    for instance in instances:
        described = agent.data("entity_get", {"entity": instance})
        check(described["components"]["PrefabInstance"]["Prefab"] == prefab["asset"],
              "An instance doesn't record its prefab")
        check(len(described["children"]) == 1, "An instance lost its child")
    light = agent.data("entity_get", {"entity": find_entities(hierarchy, "Light")[2]["id"]})
    check(all(abs(actual - expected) < 1e-4 for actual, expected in zip(light["worldPosition"], [3, 3.3, 0])),
          f"The last light is at {light['worldPosition']}")
    assets = {asset["path"] for asset in agent.data("asset_list")["assets"]}
    check(assets == {"Prefabs/Tower.lprefab", "Scenes/Main.lscene"}, f"The project's assets are {assets}")

    # Screenshots: the scene from the camera, and the selection highlighted
    agent.tool("camera_set", {"position": [8, 7, 10], "target": [0, 1, 0]})
    plain = screenshot(agent, 320, 180)
    agent.tool("selection_set", {"entity": instances[1]})
    selected = screenshot(agent, 320, 180)
    background = plain[:4]
    check(abs(background[0] - 31) <= 2 and abs(background[2] - 36) <= 2 and background[3] == 255,
          f"The top left corner isn't the background: {tuple(background)}")
    drawn = sum(1 for index in range(0, len(plain), 4) if plain[index:index + 3] != background[:3])
    check(drawn > len(plain) // 4 // 10, f"Only {drawn} pixels show the scene")
    check(plain != selected, "Selecting an entity didn't highlight it")

    # Play: input reaches the simulation, and stopping restores the scene
    check(agent.data("play_start", {"paused": True}) == {"state": "Paused", "tick": 0, "queuedInput": 0},
          "Play mode didn't start paused")
    agent.data("input_send", {"hold": ["W"], "tap": ["Space"], "ticks": 5})
    status = agent.data("play_step", {"ticks": 10})
    check(status["tick"] == 10 and status["queuedInput"] == 0, f"Play mode status is {status}")
    agent.failing_tool("entity_delete", {"entity": ground})
    agent.data("play_stop")
    check([entity["name"] for entity in agent.data("scene_hierarchy")["entities"]] == names,
          "Play mode didn't restore the scene")

    # Nothing went wrong along the way
    errors = agent.data("log_read", {"level": "error"})["entries"]
    check(not errors, f"The editor logged errors: {[entry['message'] for entry in errors]}")
    if isinstance(client, HttpClient):
        client.check_security()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", required=True, help="The LodestoneEditor executable")
    parser.add_argument("--transport", required=True, choices=["stdio", "http"])
    parser.add_argument("--vulkan-driver", help="Render screenshots with this Vulkan driver, e.g. lavapipe")
    arguments = parser.parse_args()
    driver_arguments = ["--vulkan-driver", arguments.vulkan_driver] if arguments.vulkan_driver else []

    with tempfile.TemporaryDirectory(prefix="LodestoneSmokeTest-") as directory:
        project = pathlib.Path(directory) / "Game"
        client = (StdioClient if arguments.transport == "stdio" else HttpClient)(arguments.editor, driver_arguments)
        agent = Agent(client)
        try:
            run(agent, project, client)
            exit_code = client.close() if arguments.transport == "stdio" else client.close(agent)
            check(exit_code == 0, f"The editor exited with {exit_code}")
            check((project / "Assets" / "Scenes" / "Main.lscene").is_file(), "The scene file wasn't written")
        except Exception:
            client.editor.kill()
            print("The editor's log:\n" + "\n".join(line for line in client.editor.log if line), file=sys.stderr)
            raise
    print(f"The agent smoke test passed over {arguments.transport}")


if __name__ == "__main__":
    main()
