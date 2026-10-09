#include "Lodestone/Editor/Mcp/McpServer.h"

#include "Lodestone/Core/Assert.h"
#include "Lodestone/Core/Log.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <exception>
#include <utility>

namespace Lodestone {

	namespace {

		// JSON-RPC 2.0 error codes
		constexpr int ParseError = -32700;
		constexpr int InvalidRequest = -32600;
		constexpr int MethodNotFound = -32601;
		constexpr int InvalidParams = -32602;
		constexpr int InternalError = -32603;
		// MCP: a request other than initialize before the session is initialized
		constexpr int NotInitialized = -32002;

		Json::Value MakeError(const Json::Value& id, int code, std::string_view message)
		{
			Json::Value response = Json::Value::object();
			response["jsonrpc"] = "2.0";
			response["id"] = id;
			response["error"] = {{"code", code}, {"message", message}};
			return response;
		}

		Json::Value MakeResult(const Json::Value& id, Json::Value result)
		{
			Json::Value response = Json::Value::object();
			response["jsonrpc"] = "2.0";
			response["id"] = id;
			response["result"] = std::move(result);
			return response;
		}

		Json::Value ToJson(const McpToolResult& result)
		{
			Json::Value json = Json::Value::object();
			json["content"] = result.Content;
			if (result.StructuredContent)
				json["structuredContent"] = *result.StructuredContent;
			json["isError"] = result.IsError;
			return json;
		}

	}

	McpToolResult McpToolResult::Text(std::string text)
	{
		McpToolResult result;
		result.Content.push_back({{"type", "text"}, {"text", std::move(text)}});
		return result;
	}

	McpToolResult McpToolResult::Structured(Json::Value data)
	{
		McpToolResult result = Text(data.dump(-1, ' ', false, Json::Value::error_handler_t::replace));
		result.StructuredContent = std::move(data);
		return result;
	}

	McpToolResult McpToolResult::Failure(std::string message)
	{
		McpToolResult result = Text(std::move(message));
		result.IsError = true;
		return result;
	}

	McpToolResult McpToolResult::Image(std::string base64, std::string mimeType, std::string caption)
	{
		McpToolResult result = Text(std::move(caption));
		result.Content.push_back({{"type", "image"}, {"data", std::move(base64)}, {"mimeType", std::move(mimeType)}});
		return result;
	}

	const std::vector<std::string>& McpServer::GetSupportedProtocolVersions()
	{
		static const std::vector<std::string> Versions = {"2025-11-25", "2025-06-18", "2025-03-26"};
		return Versions;
	}

	McpServer::McpServer(std::string name, std::string version, std::string instructions)
		: m_Name(std::move(name)), m_Version(std::move(version)), m_Instructions(std::move(instructions))
	{
	}

	void McpServer::AddTool(McpTool tool)
	{
		LS_CORE_ASSERT(FindTool(tool.Name) == nullptr, "A tool named '{}' already exists", tool.Name);
		LS_CORE_ASSERT(tool.Handler != nullptr && tool.InputSchema.is_object(), "Tool '{}' is incomplete", tool.Name);
		m_Tools.push_back(std::move(tool));
	}

	const McpTool* McpServer::FindTool(std::string_view name) const
	{
		const auto tool = std::ranges::find(m_Tools, name, &McpTool::Name);
		return tool != m_Tools.end() ? &*tool : nullptr;
	}

	std::optional<Json::Value> McpServer::HandleMessage(const Json::Value& message, McpSession& session)
	{
		const Json::Value nullId;
		if (!message.is_object())
			return MakeError(nullId, InvalidRequest, "A message must be a JSON-RPC object (batches aren't supported)");
		const Json::Value id = message.contains("id") ? message["id"] : nullId;
		if (!message.contains("jsonrpc") || message["jsonrpc"] != "2.0")
			return MakeError(id, InvalidRequest, "The message isn't JSON-RPC 2.0");
		if (!message.contains("method"))
			return std::nullopt; // A response or an error; the server sends no requests, so there's nothing to match
		if (!message["method"].is_string())
			return MakeError(id, InvalidRequest, "The method must be a string");

		const auto method = message["method"].get<std::string>();
		const bool isRequest = message.contains("id");
		if (isRequest && !id.is_string() && !id.is_number_integer())
			return MakeError(nullId, InvalidRequest, "The id must be a string or an integer");
		const Json::Value params = message.contains("params") ? message["params"] : Json::Value::object();
		if (!params.is_object())
			return isRequest ? std::optional(MakeError(id, InvalidParams, "The params must be an object"))
							 : std::nullopt;

		if (!isRequest)
		{
			// notifications/initialized completes the handshake; other notifications need nothing
			return std::nullopt;
		}

		if (method == "initialize")
			return MakeResult(id, Initialize(params, session));
		if (method == "ping")
			return MakeResult(id, Json::Value::object());
		if (!session.Initialized)
			return MakeError(id, NotInitialized, "The session isn't initialized: send initialize first");
		if (method == "tools/list")
			return MakeResult(id, ListTools());
		if (method == "tools/call")
		{
			if (!params.contains("name") || !params["name"].is_string())
				return MakeError(id, InvalidParams, "tools/call needs the tool's name");
			const auto name = params["name"].get<std::string>();
			if (FindTool(name) == nullptr)
				return MakeError(id, InvalidParams, fmt::format("There's no tool named '{}'", name));
			const Json::Value arguments = params.contains("arguments") ? params["arguments"] : Json::Value::object();
			if (!arguments.is_object())
				return MakeError(id, InvalidParams, "The arguments must be an object");
			return MakeResult(id, ToJson(CallTool(name, arguments)));
		}
		return MakeError(id, MethodNotFound, fmt::format("Unknown method '{}'", method));
	}

	std::optional<std::string> McpServer::HandleText(std::string_view text, McpSession& session)
	{
		const auto message = Json::Parse(text);
		if (!message)
			return MakeError(Json::Value(), ParseError, message.error().GetMessageText())
				.dump(-1, ' ', false, Json::Value::error_handler_t::replace);
		try
		{
			const auto response = HandleMessage(*message, session);
			if (!response)
				return std::nullopt;
			return response->dump(-1, ' ', false, Json::Value::error_handler_t::replace);
		}
		catch (const std::exception& exception)
		{
			// Nothing above should throw; a bug mustn't take the server down
			LS_CORE_ERROR("Handling an MCP message failed: {}", exception.what());
			const Json::Value id = message->is_object() && message->contains("id") ? (*message)["id"] : Json::Value();
			return MakeError(id, InternalError, exception.what())
				.dump(-1, ' ', false, Json::Value::error_handler_t::replace);
		}
	}

	McpToolResult McpServer::CallTool(std::string_view name, const Json::Value& arguments) const
	{
		const McpTool* tool = FindTool(name);
		if (tool == nullptr)
			return McpToolResult::Failure(fmt::format("There's no tool named '{}'", name));
		try
		{
			return tool->Handler(arguments);
		}
		catch (const std::exception& exception)
		{
			// Tools return errors rather than throwing; anything thrown is a bug, reported to the agent and the log
			LS_CORE_ERROR("MCP tool '{}' failed: {}", name, exception.what());
			return McpToolResult::Failure(fmt::format("The tool failed unexpectedly: {}", exception.what()));
		}
	}

	Json::Value McpServer::Initialize(const Json::Value& params, McpSession& session) const
	{
		const std::vector<std::string>& versions = GetSupportedProtocolVersions();
		std::string version = versions.front();
		if (params.contains("protocolVersion") && params["protocolVersion"].is_string())
		{
			const auto requested = params["protocolVersion"].get<std::string>();
			if (std::ranges::find(versions, requested) != versions.end())
				version = requested;
		}
		session.Initialized = true;
		session.ProtocolVersion = version;
		if (params.contains("clientInfo") && params["clientInfo"].is_object() &&
			params["clientInfo"].contains("name") && params["clientInfo"]["name"].is_string())
			session.ClientName = params["clientInfo"]["name"].get<std::string>();
		LS_CORE_INFO("MCP client '{}' connected (protocol {})", session.ClientName, version);

		Json::Value result = Json::Value::object();
		result["protocolVersion"] = version;
		result["capabilities"] = {{"tools", {{"listChanged", false}}}};
		result["serverInfo"] = {{"name", m_Name}, {"version", m_Version}};
		result["instructions"] = m_Instructions;
		return result;
	}

	Json::Value McpServer::ListTools() const
	{
		Json::Value tools = Json::Value::array();
		for (const McpTool& tool : m_Tools)
		{
			Json::Value json = Json::Value::object();
			json["name"] = tool.Name;
			json["title"] = tool.Title;
			json["description"] = tool.Description;
			json["inputSchema"] = tool.InputSchema;
			json["annotations"] = {{"title", tool.Title}, {"readOnlyHint", tool.ReadOnly},
				{"destructiveHint", tool.Destructive}, {"openWorldHint", false}};
			tools.push_back(std::move(json));
		}
		return {{"tools", std::move(tools)}};
	}

}
