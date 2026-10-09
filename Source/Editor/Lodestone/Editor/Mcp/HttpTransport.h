#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Editor/Mcp/MainThreadDispatcher.h"
#include "Lodestone/Editor/Mcp/McpServer.h"

#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace httplib { // NOLINT(readability-identifier-naming): cpp-httplib's own namespace
	class Server;
}

namespace Lodestone {

	// MCP's Streamable HTTP transport, for attaching to a running editor: POST /mcp with one JSON-RPC message, answered
	// with JSON. It listens on 127.0.0.1 only, and - so web pages can't reach it through DNS rebinding - it refuses
	// requests whose Host isn't localhost or 127.0.0.1 with its port, and requests from any other web origin (see
	// docs/AIControl.md#security). Requests are handled on the main thread
	class HttpTransport
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		// Starts listening. Port 0 picks a free port (see GetPort())
		[[nodiscard]] static std::expected<Scope<HttpTransport>, Error> Start(
			McpServer& server, MainThreadDispatcher& dispatcher, uint16_t port);

		HttpTransport(Passkey passkey, McpServer& server, MainThreadDispatcher& dispatcher);
		// Stops listening and waits for requests in progress
		~HttpTransport();

		HttpTransport(const HttpTransport&) = delete;
		HttpTransport& operator=(const HttpTransport&) = delete;
		HttpTransport(HttpTransport&&) = delete;
		HttpTransport& operator=(HttpTransport&&) = delete;

		uint16_t GetPort() const { return m_Port; }
		// http://127.0.0.1:<port>/mcp
		std::string GetUrl() const;

		// Whether a request with these headers may be served (exposed for tests): its Host names this server, and
		// its Origin, if any, is a localhost origin
		static bool IsAllowedHost(std::string_view host, uint16_t port);
		static bool IsAllowedOrigin(std::string_view origin);

	private:
		void InstallRoutes();

	private:
		McpServer* m_Server;
		MainThreadDispatcher* m_Dispatcher;
		Scope<httplib::Server> m_Http;
		uint16_t m_Port = 0;
		std::thread m_Listener;
		std::mutex m_SessionsMutex;
		std::map<std::string, std::shared_ptr<McpSession>> m_Sessions;
	};

}
