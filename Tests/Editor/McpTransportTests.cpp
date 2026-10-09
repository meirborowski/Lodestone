#include "Common/LogLevelScope.h"
#include "Lodestone/Editor/Mcp/HttpTransport.h"
#include "Lodestone/Editor/Mcp/MainThreadDispatcher.h"
#include "Lodestone/Editor/Mcp/StdioTransport.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace Lodestone {

	namespace {

		using namespace std::chrono_literals;

		McpServer MakeServer()
		{
			McpServer server("Test Server", "1.0.0", "");
			server.AddTool({.Name = "add",
				.Title = "Add",
				.Description = "Adds two numbers",
				.InputSchema = {{"type", "object"}},
				.Handler = [](const Json::Value& arguments)
				{
					return McpToolResult::Structured({{"sum", arguments["a"].get<int>() + arguments["b"].get<int>()}});
				}});
			return server;
		}

		// Runs the dispatcher's work on this thread until a condition holds
		template <typename Condition>
		void PumpUntil(MainThreadDispatcher& dispatcher, Condition condition)
		{
			const auto deadline = std::chrono::steady_clock::now() + 10s;
			while (!condition())
			{
				REQUIRE(std::chrono::steady_clock::now() < deadline);
				dispatcher.WaitForWork(10ms);
				dispatcher.RunPending();
			}
		}

	}

	TEST_CASE("The dispatcher runs work from other threads on the thread that pumps it")
	{
		MainThreadDispatcher dispatcher;
		const std::thread::id mainThread = std::this_thread::get_id();
		std::atomic<bool> done = false;
		std::optional<std::thread::id> ranOn;

		std::thread worker(
			[&]
			{
				ranOn = dispatcher.Invoke([] { return std::this_thread::get_id(); });
				done = true;
			});
		PumpUntil(dispatcher, [&done] { return done.load(); });
		worker.join();

		REQUIRE(ranOn.has_value());
		CHECK(*ranOn == mainThread);
	}

	TEST_CASE("The dispatcher runs work in the order it was posted")
	{
		MainThreadDispatcher dispatcher;
		std::vector<int> order;
		std::atomic<int> finished = 0;
		// One worker at a time, so the posting order is known
		for (int index = 0; index < 5; ++index)
		{
			std::thread worker(
				[&, index]
				{
					dispatcher.Invoke(
						[&order, index]
						{
							order.push_back(index);
							return 0;
						});
					++finished;
				});
			PumpUntil(dispatcher, [&finished, index] { return finished.load() == index + 1; });
			worker.join();
		}

		CHECK(order == std::vector<int>{0, 1, 2, 3, 4});
		CHECK(dispatcher.RunPending() == 0);
	}

	TEST_CASE("What the dispatched work throws reaches its caller, not the main thread")
	{
		MainThreadDispatcher dispatcher;
		std::atomic<bool> done = false;
		// Read at runtime, so the work doesn't throw unconditionally: that would make the code after it unreachable,
		// which MSVC's optimizer reports as a warning
		std::atomic<bool> fail = true;
		bool caught = false;

		std::thread worker(
			[&]
			{
				try
				{
					static_cast<void>(dispatcher.Invoke(
						[&fail]() -> int
						{
							if (fail.load())
								throw std::runtime_error("Failed");
							return 0;
						}));
				}
				catch (const std::runtime_error&)
				{
					caught = true;
				}
				done = true;
			});
		PumpUntil(dispatcher, [&done] { return done.load(); });
		worker.join();

		CHECK(caught);
	}

	TEST_CASE("Shutting the dispatcher down releases waiting callers and refuses new work")
	{
		MainThreadDispatcher dispatcher;
		std::atomic<bool> waiting = false;
		std::optional<int> result = 1;

		std::thread worker(
			[&]
			{
				waiting = true;
				result = dispatcher.Invoke([] { return 5; });
			});
		while (!waiting.load())
			std::this_thread::yield();
		// The work may or may not be queued yet; either way the caller gets nothing
		std::this_thread::sleep_for(20ms);
		dispatcher.Shutdown();
		worker.join();

		CHECK_FALSE(result.has_value());
		CHECK_FALSE(dispatcher.Invoke([] { return 1; }).has_value());
		// Waiting returns right away once it's shut down
		dispatcher.WaitForWork(10s);
	}

	TEST_CASE("The stdio transport answers each line with a line, and finishes when the input ends")
	{
		McpServer server = MakeServer();
		MainThreadDispatcher dispatcher;
		std::istringstream input(
			R"({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18"}})"
			"\r\n"
			"\n"
			R"({"jsonrpc": "2.0", "method": "notifications/initialized"})"
			"\n"
			R"({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": "add", "arguments": {"a": 2, "b": 3}}})"
			"\n"
			"not json\n");
		std::ostringstream output;
		{
			const Testing::LogLevelScope quiet(LogLevel::Warn);
			StdioTransport transport(server, dispatcher, input, output);
			transport.Start();
			PumpUntil(dispatcher, [&transport] { return transport.IsFinished(); });
		}

		std::istringstream lines(output.str());
		std::vector<Json::Value> responses;
		for (std::string line; std::getline(lines, line);)
		{
			auto response = Json::Parse(line);
			REQUIRE(response.has_value());
			responses.push_back(std::move(*response));
		}
		// The notification and the blank line get nothing
		REQUIRE(responses.size() == 3);
		CHECK(responses[0]["id"] == 1);
		CHECK(responses[0]["result"]["protocolVersion"] == "2025-06-18");
		CHECK(responses[1]["id"] == 2);
		CHECK(responses[1]["result"]["structuredContent"]["sum"] == 5);
		CHECK(responses[2]["error"]["code"] == -32700);
		// Every message is a single line
		CHECK(output.str().ends_with("\n"));
		CHECK_FALSE(output.str().contains("\r"));
	}

	TEST_CASE("The stdio transport stops when the dispatcher shuts down")
	{
		McpServer server = MakeServer();
		MainThreadDispatcher dispatcher;
		std::istringstream input(R"({"jsonrpc": "2.0", "id": 1, "method": "ping"})"
								 "\n"
								 R"({"jsonrpc": "2.0", "id": 2, "method": "ping"})"
								 "\n");
		std::ostringstream output;
		dispatcher.Shutdown();
		{
			const Testing::LogLevelScope quiet(LogLevel::Warn);
			StdioTransport transport(server, dispatcher, input, output);
			transport.Start();
		}

		CHECK(output.str().empty());
	}

	TEST_CASE("The HTTP transport only accepts a localhost Host with its own port")
	{
		CHECK(HttpTransport::IsAllowedHost("localhost:7850", 7850));
		CHECK(HttpTransport::IsAllowedHost("127.0.0.1:7850", 7850));
		CHECK(HttpTransport::IsAllowedHost("localhost", 80));

		CHECK_FALSE(HttpTransport::IsAllowedHost("localhost:7851", 7850));
		CHECK_FALSE(HttpTransport::IsAllowedHost("localhost", 7850));
		CHECK_FALSE(HttpTransport::IsAllowedHost("evil.com:7850", 7850));
		CHECK_FALSE(HttpTransport::IsAllowedHost("localhost.evil.com:7850", 7850));
		CHECK_FALSE(HttpTransport::IsAllowedHost("127.0.0.2:7850", 7850));
		CHECK_FALSE(HttpTransport::IsAllowedHost("0.0.0.0:7850", 7850));
		CHECK_FALSE(HttpTransport::IsAllowedHost("[::1]:7850", 7850));
		CHECK_FALSE(HttpTransport::IsAllowedHost("", 7850));
		CHECK_FALSE(HttpTransport::IsAllowedHost("localhost:", 7850));
		CHECK_FALSE(HttpTransport::IsAllowedHost("localhost:78500", 7850));
		CHECK_FALSE(HttpTransport::IsAllowedHost("localhost:7850x", 7850));
	}

	TEST_CASE("The HTTP transport only accepts localhost origins")
	{
		CHECK(HttpTransport::IsAllowedOrigin("http://localhost"));
		CHECK(HttpTransport::IsAllowedOrigin("http://localhost:3000"));
		CHECK(HttpTransport::IsAllowedOrigin("https://127.0.0.1:8443"));

		CHECK_FALSE(HttpTransport::IsAllowedOrigin("http://evil.com"));
		CHECK_FALSE(HttpTransport::IsAllowedOrigin("http://localhost.evil.com"));
		CHECK_FALSE(HttpTransport::IsAllowedOrigin("http://evil.com:80@localhost"));
		CHECK_FALSE(HttpTransport::IsAllowedOrigin("null"));
		CHECK_FALSE(HttpTransport::IsAllowedOrigin("file://"));
		CHECK_FALSE(HttpTransport::IsAllowedOrigin("localhost"));
		CHECK_FALSE(HttpTransport::IsAllowedOrigin("http://localhost:abc"));
		CHECK_FALSE(HttpTransport::IsAllowedOrigin(""));
	}

}
