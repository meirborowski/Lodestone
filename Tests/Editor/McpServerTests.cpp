#include "Lodestone/Editor/Mcp/McpServer.h"

#include "Common/LogLevelScope.h"

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>

namespace Lodestone {

	namespace {

		// A server with an echo tool and a tool that throws
		struct Fixture
		{
			Fixture()
				: Server("Test Server", "1.2.3", "Test instructions")
			{
				Server.AddTool({.Name = "echo",
					.Title = "Echo",
					.Description = "Returns its text",
					.InputSchema = {{"type", "object"}, {"properties", {{"text", {{"type", "string"}}}}}},
					.ReadOnly = true,
					.Handler = [](const Json::Value& arguments)
					{
						return McpToolResult::Structured(
							{{"text", arguments.contains("text") ? arguments["text"] : Json::Value("")}});
					}});
				Server.AddTool({.Name = "explode",
					.Title = "Explode",
					.Description = "Throws",
					.InputSchema = {{"type", "object"}},
					.Destructive = true,
					.Handler = [](const Json::Value&) -> McpToolResult { throw std::runtime_error("Boom"); }});
			}

			Json::Value Request(const std::string& method, Json::Value params = Json::Value::object(), int id = 1)
			{
				const Json::Value message = {
					{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}};
				const auto response = Server.HandleMessage(message, Session);
				REQUIRE(response.has_value());
				return response.value_or(Json::Value());
			}

			void Initialize()
			{
				const Json::Value response = Request("initialize",
					{{"protocolVersion", "2025-06-18"}, {"capabilities", Json::Value::object()},
						{"clientInfo", {{"name", "Tests"}, {"version", "1"}}}});
				REQUIRE(response.contains("result"));
			}

			McpServer Server;
			McpSession Session;
		};

		int GetErrorCode(const Json::Value& response)
		{
			REQUIRE(response.contains("error"));
			return response["error"]["code"].get<int>();
		}

	}

	TEST_CASE("Initializing agrees on a protocol version and describes the server")
	{
		Fixture fixture;

		const Json::Value response = fixture.Request("initialize",
			{{"protocolVersion", "2025-06-18"}, {"capabilities", Json::Value::object()},
				{"clientInfo", {{"name", "Claude Code"}, {"version", "2"}}}});

		CHECK(response["jsonrpc"] == "2.0");
		CHECK(response["id"] == 1);
		const Json::Value& result = response["result"];
		CHECK(result["protocolVersion"] == "2025-06-18");
		CHECK(result["serverInfo"]["name"] == "Test Server");
		CHECK(result["serverInfo"]["version"] == "1.2.3");
		CHECK(result["instructions"] == "Test instructions");
		CHECK(result["capabilities"].contains("tools"));
		CHECK(fixture.Session.Initialized);
		CHECK(fixture.Session.ClientName == "Claude Code");
		CHECK(fixture.Session.ProtocolVersion == "2025-06-18");
	}

	TEST_CASE("An unknown protocol version gets the newest one the server speaks")
	{
		Fixture fixture;

		const Json::Value response = fixture.Request("initialize", {{"protocolVersion", "1999-01-01"}});

		CHECK(response["result"]["protocolVersion"] == McpServer::GetSupportedProtocolVersions().front());
	}

	TEST_CASE("Every supported protocol version can be agreed on")
	{
		for (const std::string& version : McpServer::GetSupportedProtocolVersions())
		{
			Fixture fixture;
			CAPTURE(version);
			CHECK(
				fixture.Request("initialize", {{"protocolVersion", version}})["result"]["protocolVersion"] == version);
		}
	}

	TEST_CASE("Requests other than initialize and ping need an initialized session")
	{
		Fixture fixture;

		CHECK(GetErrorCode(fixture.Request("tools/list")) == -32002);
		CHECK(GetErrorCode(fixture.Request("tools/call", {{"name", "echo"}})) == -32002);
		CHECK(fixture.Request("ping")["result"] == Json::Value::object());
	}

	TEST_CASE("Tools are listed with their schemas and hints")
	{
		Fixture fixture;
		fixture.Initialize();

		const Json::Value tools = fixture.Request("tools/list")["result"]["tools"];

		REQUIRE(tools.size() == 2);
		CHECK(tools[0]["name"] == "echo");
		CHECK(tools[0]["title"] == "Echo");
		CHECK(tools[0]["description"] == "Returns its text");
		CHECK(tools[0]["inputSchema"]["type"] == "object");
		CHECK(tools[0]["annotations"]["readOnlyHint"] == true);
		CHECK(tools[0]["annotations"]["destructiveHint"] == false);
		CHECK(tools[1]["annotations"]["destructiveHint"] == true);
	}

	TEST_CASE("Calling a tool returns its content and structured content")
	{
		Fixture fixture;
		fixture.Initialize();

		const Json::Value result =
			fixture.Request("tools/call", {{"name", "echo"}, {"arguments", {{"text", "hello"}}}})["result"];

		CHECK(result["isError"] == false);
		CHECK(result["structuredContent"]["text"] == "hello");
		REQUIRE(result["content"].size() == 1);
		CHECK(result["content"][0]["type"] == "text");
		CHECK(result["content"][0]["text"] == R"({"text":"hello"})");
	}

	TEST_CASE("A tool that throws reports an error result instead of failing the request")
	{
		const Testing::LogLevelScope quiet(LogLevel::Off);
		Fixture fixture;
		fixture.Initialize();

		const Json::Value result = fixture.Request("tools/call", {{"name", "explode"}})["result"];

		CHECK(result["isError"] == true);
		CHECK(result["content"][0]["text"].get<std::string>().contains("Boom"));
	}

	TEST_CASE("Bad tool calls are invalid params errors")
	{
		Fixture fixture;
		fixture.Initialize();

		CHECK(GetErrorCode(fixture.Request("tools/call", {{"name", "missing"}})) == -32602);
		CHECK(GetErrorCode(fixture.Request("tools/call", Json::Value::object())) == -32602);
		CHECK(GetErrorCode(fixture.Request("tools/call", {{"name", 5}})) == -32602);
		CHECK(GetErrorCode(fixture.Request("tools/call", {{"name", "echo"}, {"arguments", "text"}})) == -32602);
	}

	TEST_CASE("Unknown methods are method-not-found errors")
	{
		Fixture fixture;
		fixture.Initialize();

		CHECK(GetErrorCode(fixture.Request("resources/list")) == -32601);
	}

	TEST_CASE("Malformed messages are answered with JSON-RPC errors")
	{
		Fixture fixture;
		McpSession& session = fixture.Session;
		McpServer& server = fixture.Server;
		const auto error = [&server, &session](const Json::Value& message)
		{
			const auto response = server.HandleMessage(message, session);
			REQUIRE(response.has_value());
			return GetErrorCode(*response);
		};

		CHECK(error(Json::Value::array()) == -32600);
		CHECK(error({{"id", 1}, {"method", "ping"}}) == -32600);
		CHECK(error({{"jsonrpc", "1.0"}, {"id", 1}, {"method", "ping"}}) == -32600);
		CHECK(error({{"jsonrpc", "2.0"}, {"id", 1}, {"method", 7}}) == -32600);
		CHECK(error({{"jsonrpc", "2.0"}, {"id", 1.5}, {"method", "ping"}}) == -32600);
		CHECK(error({{"jsonrpc", "2.0"}, {"id", Json::Value::object()}, {"method", "ping"}}) == -32600);
		CHECK(error({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ping"}, {"params", 3}}) == -32602);
	}

	TEST_CASE("Notifications and responses get no answer")
	{
		Fixture fixture;

		CHECK_FALSE(
			fixture.Server.HandleMessage({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}, fixture.Session)
				.has_value());
		CHECK_FALSE(
			fixture.Server.HandleMessage({{"jsonrpc", "2.0"}, {"method", "unknown"}}, fixture.Session).has_value());
		CHECK_FALSE(fixture.Server
				.HandleMessage({{"jsonrpc", "2.0"}, {"id", 4}, {"result", Json::Value::object()}}, fixture.Session)
				.has_value());
	}

	TEST_CASE("Requests keep their IDs, numbers or strings")
	{
		Fixture fixture;

		const auto response =
			fixture.Server.HandleText(R"({"jsonrpc": "2.0", "id": "abc", "method": "ping"})", fixture.Session);

		REQUIRE(response.has_value());
		const auto json = Json::Parse(response.value_or(std::string()));
		REQUIRE(json.has_value());
		CHECK((*json)["id"] == "abc");
	}

	TEST_CASE("Text that isn't JSON gets a parse error")
	{
		Fixture fixture;

		const auto response = fixture.Server.HandleText("{not json", fixture.Session);

		REQUIRE(response.has_value());
		const auto json = Json::Parse(response.value_or(std::string()));
		REQUIRE(json.has_value());
		CHECK((*json)["error"]["code"] == -32700);
		CHECK((*json)["id"].is_null());
	}

	TEST_CASE("Tool results come in text, structured, failure and image forms")
	{
		const McpToolResult text = McpToolResult::Text("Done");
		CHECK(text.Content[0]["text"] == "Done");
		CHECK_FALSE(text.StructuredContent.has_value());
		CHECK_FALSE(text.IsError);

		const McpToolResult failure = McpToolResult::Failure("Nope");
		CHECK(failure.IsError);
		CHECK(failure.Content[0]["text"] == "Nope");

		const McpToolResult image = McpToolResult::Image("AAAA", "image/png", "A picture");
		REQUIRE(image.Content.size() == 2);
		CHECK(image.Content[0]["text"] == "A picture");
		CHECK(image.Content[1]["type"] == "image");
		CHECK(image.Content[1]["data"] == "AAAA");
		CHECK(image.Content[1]["mimeType"] == "image/png");
	}

	TEST_CASE("Calling a tool in-process works without a session")
	{
		const Fixture fixture;

		const McpToolResult echoed = fixture.Server.CallTool("echo", {{"text", "direct"}});
		CHECK(echoed.StructuredContent.value_or(Json::Value::object())["text"] == "direct");
		CHECK(fixture.Server.CallTool("missing", Json::Value::object()).IsError);
		CHECK(fixture.Server.FindTool("echo") != nullptr);
		CHECK(fixture.Server.FindTool("missing") == nullptr);
	}

}
