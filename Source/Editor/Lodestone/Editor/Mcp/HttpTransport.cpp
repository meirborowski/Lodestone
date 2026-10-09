#include "Lodestone/Editor/Mcp/HttpTransport.h"

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Core/UUID.h"

#include <httplib.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <utility>

namespace Lodestone {

	namespace {

		constexpr const char* Endpoint = "/mcp";
		constexpr const char* SessionHeader = "Mcp-Session-Id";
		constexpr const char* ProtocolVersionHeader = "MCP-Protocol-Version";
		constexpr const char* JsonContentType = "application/json";
		// Far more than any tool call needs, and small enough that a request can't exhaust memory
		constexpr size_t MaxRequestSize = 16ull * 1024 * 1024;
		constexpr size_t WorkerThreads = 4;

		void SendError(httplib::Response& response, int status, std::string_view message)
		{
			response.status = status;
			response.set_content(fmt::format(R"({{"error": "{}"}})", message), JsonContentType);
		}

		// The port in "host:port", if the text after the last colon is one
		std::optional<uint16_t> ParsePort(std::string_view text)
		{
			uint16_t port = 0;
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
			if (error != std::errc() || end != text.data() + text.size())
				return std::nullopt;
			return port;
		}

		bool IsLoopbackName(std::string_view name)
		{
			return name == "localhost" || name == "127.0.0.1";
		}

		bool AcceptsJson(const httplib::Request& request)
		{
			if (!request.has_header("Accept"))
				return true;
			const std::string accept = request.get_header_value("Accept");
			return accept.contains("application/json") || accept.contains("*/*");
		}

	}

	std::expected<Scope<HttpTransport>, Error> HttpTransport::Start(
		McpServer& server, MainThreadDispatcher& dispatcher, uint16_t port)
	{
		auto transport = CreateScope<HttpTransport>(Passkey(), server, dispatcher);
		transport->InstallRoutes();

		httplib::Server& http = *transport->m_Http;
		if (port == 0)
		{
			const int bound = http.bind_to_any_port("127.0.0.1");
			if (bound <= 0)
				return std::unexpected(Error(ErrorCode::IoError, "The MCP server can't listen on any port"));
			transport->m_Port = static_cast<uint16_t>(bound);
		}
		else
		{
			if (!http.bind_to_port("127.0.0.1", port))
				return std::unexpected(Error(ErrorCode::IoError,
					fmt::format("The MCP server can't listen on port {}: is another editor using it?", port)));
			transport->m_Port = port;
		}

		transport->m_Listener = std::thread([&http] { http.listen_after_bind(); });
		http.wait_until_ready();
		LS_CORE_INFO("MCP server listening on {}", transport->GetUrl());
		return transport;
	}

	HttpTransport::HttpTransport(Passkey /*passkey*/, McpServer& server, MainThreadDispatcher& dispatcher)
		: m_Server(&server), m_Dispatcher(&dispatcher), m_Http(CreateScope<httplib::Server>())
	{
	}

	HttpTransport::~HttpTransport()
	{
		m_Http->stop();
		if (m_Listener.joinable())
			m_Listener.join();
	}

	std::string HttpTransport::GetUrl() const
	{
		return fmt::format("http://127.0.0.1:{}{}", m_Port, Endpoint);
	}

	bool HttpTransport::IsAllowedHost(std::string_view host, uint16_t port)
	{
		const size_t colon = host.rfind(':');
		if (colon == std::string_view::npos)
			return IsLoopbackName(host) && port == 80;
		const std::optional<uint16_t> hostPort = ParsePort(host.substr(colon + 1));
		return IsLoopbackName(host.substr(0, colon)) && hostPort == port;
	}

	bool HttpTransport::IsAllowedOrigin(std::string_view origin)
	{
		for (const std::string_view scheme : {std::string_view("http://"), std::string_view("https://")})
		{
			if (!origin.starts_with(scheme))
				continue;
			std::string_view authority = origin.substr(scheme.size());
			if (const size_t colon = authority.rfind(':'); colon != std::string_view::npos)
			{
				if (!ParsePort(authority.substr(colon + 1)))
					return false;
				authority = authority.substr(0, colon);
			}
			return IsLoopbackName(authority);
		}
		return false;
	}

	void HttpTransport::InstallRoutes()
	{
		httplib::Server& http = *m_Http;
		// The port must be this server's alone. httplib's default lets other sockets bind it too: SO_REUSEPORT
		// shares it, splitting requests between two editors, and Windows' SO_REUSEADDR lets another process take it
		http.set_socket_options(
			[](socket_t socket)
			{
#if LS_PLATFORM_WINDOWS
				httplib::set_socket_opt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
				// Only so a restarted editor can listen again while the old connections wind down
				httplib::set_socket_opt(socket, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
			});
		http.set_payload_max_length(MaxRequestSize);
		http.new_task_queue = [] { return new httplib::ThreadPool(WorkerThreads); };

		// Every request: the DNS rebinding checks come first
		http.set_pre_routing_handler(
			[this](const httplib::Request& request, httplib::Response& response)
			{
				if (!IsAllowedHost(request.get_header_value("Host"), m_Port))
				{
					SendError(
						response, 403, "Forbidden: the Host must be localhost or 127.0.0.1 with this server's port");
					return httplib::Server::HandlerResponse::Handled;
				}
				if (request.has_header("Origin") && !IsAllowedOrigin(request.get_header_value("Origin")))
				{
					SendError(response, 403, "Forbidden: requests from web pages aren't allowed");
					return httplib::Server::HandlerResponse::Handled;
				}
				return httplib::Server::HandlerResponse::Unhandled;
			});

		http.Post(Endpoint,
			[this](const httplib::Request& request, httplib::Response& response)
			{
				if (!AcceptsJson(request))
				{
					SendError(response, 406, "The client must accept application/json");
					return;
				}
				if (request.has_header(ProtocolVersionHeader))
				{
					const std::vector<std::string>& versions = McpServer::GetSupportedProtocolVersions();
					if (std::ranges::find(versions, request.get_header_value(ProtocolVersionHeader)) == versions.end())
					{
						SendError(response, 400, "Unsupported MCP protocol version");
						return;
					}
				}

				// A session starts with initialize, and every later request names it
				std::shared_ptr<McpSession> session;
				std::string sessionId;
				if (request.has_header(SessionHeader))
				{
					sessionId = request.get_header_value(SessionHeader);
					const std::scoped_lock lock(m_SessionsMutex);
					const auto found = m_Sessions.find(sessionId);
					if (found == m_Sessions.end())
					{
						SendError(response, 404, "Unknown session: initialize a new one");
						return;
					}
					session = found->second;
				}
				else
				{
					const auto message = Json::Parse(request.body);
					const bool initialize = message && message->is_object() && message->contains("method") &&
						(*message)["method"] == "initialize";
					if (!initialize)
					{
						SendError(response, 400, "Missing Mcp-Session-Id: initialize a session first");
						return;
					}
					session = std::make_shared<McpSession>();
					sessionId = UUID::Generate().ToString();
				}

				const auto answer = m_Dispatcher->Invoke(
					[this, &request, session] { return m_Server->HandleText(request.body, *session); });
				if (!answer)
				{
					SendError(response, 503, "The editor is shutting down");
					return;
				}
				// A new session exists once initialize succeeds; the response tells the client its ID
				if (!request.has_header(SessionHeader) && session->Initialized)
				{
					const std::scoped_lock lock(m_SessionsMutex);
					m_Sessions.emplace(sessionId, session);
					response.set_header(SessionHeader, sessionId);
				}
				if (*answer)
				{
					response.status = 200;
					response.set_content(**answer, JsonContentType);
				}
				else
				{
					// A notification or a response: accepted, with nothing to say
					response.status = 202;
				}
			});

		// The server never starts conversations, so there's no event stream to open
		http.Get(Endpoint, [](const httplib::Request& /*request*/, httplib::Response& response)
			{ SendError(response, 405, "This server sends no events: POST messages instead"); });

		http.Delete(Endpoint,
			[this](const httplib::Request& request, httplib::Response& response)
			{
				const std::scoped_lock lock(m_SessionsMutex);
				if (!request.has_header(SessionHeader) ||
					m_Sessions.erase(request.get_header_value(SessionHeader)) == 0)
				{
					SendError(response, 404, "Unknown session");
					return;
				}
				response.status = 204;
			});
	}

}
