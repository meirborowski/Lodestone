#include "Lodestone/Editor/Mcp/HttpTransport.h"

#include "Common/LogLevelScope.h"

#include <doctest/doctest.h>
#include <httplib.h>

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>

namespace Lodestone {

	namespace {

		using namespace std::chrono_literals;

		constexpr const char* InitializeRequest =
			R"({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "HTTP tests"}}})";
		constexpr const char* JsonType = "application/json";

		// An MCP server over HTTP on a free port, with its main thread pumping requests in the background - the
		// editor's main loop, in miniature
		class Fixture
		{
		public:
			Fixture()
				: m_Server("Test Server", "1.0.0", "")
			{
				m_Server.AddTool({.Name = "echo",
					.Title = "Echo",
					.Description = "Returns its text",
					.InputSchema = {{"type", "object"}},
					.Handler = [this](const Json::Value& arguments)
					{
						++m_EchoCalls;
						return McpToolResult::Structured({{"text", arguments.value("text", "")}});
					}});
				auto transport = HttpTransport::Start(m_Server, m_Dispatcher, 0);
				REQUIRE(transport.has_value());
				m_Transport = std::move(*transport);
				m_MainLoop = std::thread(
					[this]
					{
						while (!m_Stop.load())
						{
							m_Dispatcher.WaitForWork(10ms);
							m_Dispatcher.RunPending();
						}
					});
			}

			~Fixture()
			{
				m_Stop = true;
				m_MainLoop.join();
				m_Dispatcher.Shutdown();
				m_Transport.reset();
			}

			Fixture(const Fixture&) = delete;
			Fixture& operator=(const Fixture&) = delete;
			Fixture(Fixture&&) = delete;
			Fixture& operator=(Fixture&&) = delete;

			uint16_t GetPort() const { return m_Transport->GetPort(); }
			int GetEchoCalls() const { return m_EchoCalls.load(); }
			const HttpTransport& GetTransport() const { return *m_Transport; }

			// A client whose Host header is the transport's own
			httplib::Client MakeClient() const
			{
				httplib::Client client("127.0.0.1", GetPort());
				client.set_connection_timeout(5s);
				client.set_read_timeout(10s);
				return client;
			}

			httplib::Headers MakeHeaders(const std::string& session = {}) const
			{
				httplib::Headers headers = {{"Accept", "application/json, text/event-stream"}};
				if (!session.empty())
					headers.emplace("Mcp-Session-Id", session);
				return headers;
			}

			// Starts a session and returns its ID
			std::string Initialize() const
			{
				httplib::Client client = MakeClient();
				const auto response = client.Post("/mcp", MakeHeaders(), InitializeRequest, JsonType);
				REQUIRE(response);
				REQUIRE(response->status == 200);
				REQUIRE(response->has_header("Mcp-Session-Id"));
				return response->get_header_value("Mcp-Session-Id");
			}

		private:
			McpServer m_Server;
			MainThreadDispatcher m_Dispatcher;
			Scope<HttpTransport> m_Transport;
			std::thread m_MainLoop;
			std::atomic<bool> m_Stop = false;
			std::atomic<int> m_EchoCalls = 0;
		};

		Json::Value ParseBody(const httplib::Result& response)
		{
			REQUIRE(response);
			auto body = Json::Parse(response->body);
			REQUIRE(body.has_value());
			return *body;
		}

	}

	TEST_CASE("The HTTP transport listens on 127.0.0.1, at the URL it reports")
	{
		const Fixture fixture;

		CHECK(fixture.GetPort() != 0);
		CHECK(fixture.GetTransport().GetUrl() == "http://127.0.0.1:" + std::to_string(fixture.GetPort()) + "/mcp");
	}

	TEST_CASE("A session starts with initialize, and later requests name it")
	{
		const Fixture fixture;
		const std::string session = fixture.Initialize();
		CHECK(session.size() == 36);
		httplib::Client client = fixture.MakeClient();

		const auto notification = client.Post("/mcp", fixture.MakeHeaders(session),
			R"({"jsonrpc": "2.0", "method": "notifications/initialized"})", JsonType);
		REQUIRE(notification);
		CHECK(notification->status == 202);
		CHECK(notification->body.empty());

		const auto call = client.Post("/mcp", fixture.MakeHeaders(session),
			R"({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": "echo", "arguments": {"text": "hi"}}})",
			JsonType);
		REQUIRE(call);
		CHECK(call->status == 200);
		CHECK(call->get_header_value("Content-Type") == JsonType);
		CHECK(ParseBody(call)["result"]["structuredContent"]["text"] == "hi");
	}

	TEST_CASE("Sessions are separate")
	{
		const Fixture fixture;
		const std::string first = fixture.Initialize();
		const std::string second = fixture.Initialize();

		CHECK(first != second);
	}

	TEST_CASE("Requests without a session, or with an unknown one, are refused")
	{
		const Fixture fixture;
		httplib::Client client = fixture.MakeClient();
		const std::string ping = R"({"jsonrpc": "2.0", "id": 1, "method": "ping"})";

		const auto missing = client.Post("/mcp", fixture.MakeHeaders(), ping, JsonType);
		REQUIRE(missing);
		CHECK(missing->status == 400);

		const auto unknown = client.Post("/mcp", fixture.MakeHeaders("not-a-session"), ping, JsonType);
		REQUIRE(unknown);
		CHECK(unknown->status == 404);
	}

	TEST_CASE("A session that's deleted is gone")
	{
		const Fixture fixture;
		const std::string session = fixture.Initialize();
		httplib::Client client = fixture.MakeClient();

		const auto deleted = client.Delete("/mcp", fixture.MakeHeaders(session));
		REQUIRE(deleted);
		CHECK(deleted->status == 204);

		const auto after = client.Post(
			"/mcp", fixture.MakeHeaders(session), R"({"jsonrpc": "2.0", "id": 1, "method": "ping"})", JsonType);
		REQUIRE(after);
		CHECK(after->status == 404);
		const auto again = client.Delete("/mcp", fixture.MakeHeaders(session));
		REQUIRE(again);
		CHECK(again->status == 404);
	}

	TEST_CASE("A failed initialize doesn't start a session")
	{
		const Fixture fixture;
		httplib::Client client = fixture.MakeClient();

		const auto response = client.Post("/mcp", fixture.MakeHeaders(),
			R"({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": 5})", JsonType);

		REQUIRE(response);
		CHECK(response->status == 200);
		CHECK(ParseBody(response)["error"]["code"] == -32602);
		CHECK_FALSE(response->has_header("Mcp-Session-Id"));
	}

	TEST_CASE("Requests with a foreign Host are refused, against DNS rebinding")
	{
		const Fixture fixture;
		httplib::Client client = fixture.MakeClient();

		for (const std::string& host :
			{std::string("evil.example.com"), "evil.example.com:" + std::to_string(fixture.GetPort()),
				std::string("localhost:1"), std::string("localhost")})
		{
			CAPTURE(host);
			httplib::Headers headers = fixture.MakeHeaders();
			headers.emplace("Host", host);
			const auto response = client.Post("/mcp", headers, InitializeRequest, JsonType);
			REQUIRE(response);
			CHECK(response->status == 403);
		}

		httplib::Headers localhost = fixture.MakeHeaders();
		localhost.emplace("Host", "localhost:" + std::to_string(fixture.GetPort()));
		const auto allowed = client.Post("/mcp", localhost, InitializeRequest, JsonType);
		REQUIRE(allowed);
		CHECK(allowed->status == 200);
	}

	TEST_CASE("Requests from web pages on other origins are refused")
	{
		const Fixture fixture;
		httplib::Client client = fixture.MakeClient();

		httplib::Headers foreign = fixture.MakeHeaders();
		foreign.emplace("Origin", "https://evil.example.com");
		const auto refused = client.Post("/mcp", foreign, InitializeRequest, JsonType);
		REQUIRE(refused);
		CHECK(refused->status == 403);

		// Refused before anything else, whatever the method
		const auto refusedGet = client.Get("/mcp", foreign);
		REQUIRE(refusedGet);
		CHECK(refusedGet->status == 403);

		httplib::Headers local = fixture.MakeHeaders();
		local.emplace("Origin", "http://localhost:3000");
		const auto allowed = client.Post("/mcp", local, InitializeRequest, JsonType);
		REQUIRE(allowed);
		CHECK(allowed->status == 200);
	}

	TEST_CASE("Clients must accept JSON and speak a supported protocol version")
	{
		const Fixture fixture;
		httplib::Client client = fixture.MakeClient();

		const auto html = client.Post("/mcp", httplib::Headers{{"Accept", "text/html"}}, InitializeRequest, JsonType);
		REQUIRE(html);
		CHECK(html->status == 406);

		httplib::Headers oldVersion = fixture.MakeHeaders();
		oldVersion.emplace("MCP-Protocol-Version", "2020-01-01");
		const auto versioned = client.Post("/mcp", oldVersion, InitializeRequest, JsonType);
		REQUIRE(versioned);
		CHECK(versioned->status == 400);

		httplib::Headers supported = fixture.MakeHeaders();
		supported.emplace("MCP-Protocol-Version", "2025-06-18");
		const auto ok = client.Post("/mcp", supported, InitializeRequest, JsonType);
		REQUIRE(ok);
		CHECK(ok->status == 200);
	}

	TEST_CASE("The server sends no event stream, and has no other endpoints")
	{
		const Fixture fixture;
		httplib::Client client = fixture.MakeClient();

		const auto stream = client.Get("/mcp", httplib::Headers{{"Accept", "text/event-stream"}});
		REQUIRE(stream);
		CHECK(stream->status == 405);

		const auto other = client.Post("/other", fixture.MakeHeaders(), InitializeRequest, JsonType);
		REQUIRE(other);
		CHECK(other->status == 404);
	}

	TEST_CASE("Bodies that aren't JSON get a parse error")
	{
		const Fixture fixture;
		const std::string session = fixture.Initialize();
		httplib::Client client = fixture.MakeClient();

		const auto response = client.Post("/mcp", fixture.MakeHeaders(session), "{oops", JsonType);

		CHECK(ParseBody(response)["error"]["code"] == -32700);
	}

	TEST_CASE("Requests larger than the limit are refused")
	{
		const Testing::LogLevelScope quiet(LogLevel::Warn);
		const Fixture fixture;
		const std::string session = fixture.Initialize();
		httplib::Client client = fixture.MakeClient();
		const Json::Value request = {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/call"},
			{"params", {{"name", "echo"}, {"arguments", {{"text", std::string(size_t{17} * 1024 * 1024, 'x')}}}}}};

		const auto response = client.Post("/mcp", fixture.MakeHeaders(session), request.dump(), JsonType);

		// The server stops reading at the limit and answers 413. Whether the client sees the answer or a reset
		// connection depends on how much of the body the operating system had buffered when the server closed it;
		// either way the request was refused
		if (response)
			CHECK(response->status == 413);
		CHECK(fixture.GetEchoCalls() == 0);
	}

	TEST_CASE("Many clients can call at once")
	{
		const Fixture fixture;
		const std::string session = fixture.Initialize();
		std::vector<std::future<bool>> calls;
		calls.reserve(16);
		for (int index = 0; index < 16; ++index)
		{
			calls.push_back(std::async(std::launch::async,
				[&fixture, &session, index]
				{
					httplib::Client client = fixture.MakeClient();
					const std::string text = "call " + std::to_string(index);
					const Json::Value request = {{"jsonrpc", "2.0"}, {"id", index}, {"method", "tools/call"},
						{"params", {{"name", "echo"}, {"arguments", {{"text", text}}}}}};
					const auto response = client.Post("/mcp", fixture.MakeHeaders(session), request.dump(), JsonType);
					if (!response || response->status != 200)
						return false;
					const auto body = Json::Parse(response->body);
					return body && (*body)["id"] == index && (*body)["result"]["structuredContent"]["text"] == text;
				}));
		}

		for (std::future<bool>& call : calls)
			CHECK(call.get());
	}

	TEST_CASE("Starting on a port that's taken fails")
	{
		const Fixture fixture;
		McpServer server("Second", "1.0.0", "");
		MainThreadDispatcher dispatcher;

		const auto second = HttpTransport::Start(server, dispatcher, fixture.GetPort());

		REQUIRE_FALSE(second.has_value());
		CHECK(second.error().GetCode() == ErrorCode::IoError);
	}

}
