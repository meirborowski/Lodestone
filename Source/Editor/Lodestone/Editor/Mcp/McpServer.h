#pragma once

#include "Lodestone/Serialization/Json.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Lodestone {

	// What a tool call returns: content for the agent - text, images - and optionally the same data as JSON
	struct McpToolResult
	{
		Json::Value Content = Json::Value::array();
		std::optional<Json::Value> StructuredContent;
		// The tool ran but failed; the content says why. Agents see these and can correct themselves
		bool IsError = false;

		static McpToolResult Text(std::string text);
		// Structured data, also given as its JSON text for agents that only read text
		static McpToolResult Structured(Json::Value data);
		static McpToolResult Failure(std::string message);
		// A base64-encoded image, with a caption
		static McpToolResult Image(std::string base64, std::string mimeType, std::string caption);
	};

	struct McpTool
	{
		// snake_case, unique
		std::string Name;
		std::string Title;
		std::string Description;
		// A JSON Schema object describing the arguments
		Json::Value InputSchema;
		// Hints for agents: the tool only reads, or it may destroy data
		bool ReadOnly = false;
		bool Destructive = false;
		std::function<McpToolResult(const Json::Value& arguments)> Handler;
	};

	// What the server knows about one client connection
	struct McpSession
	{
		bool Initialized = false;
		std::string ProtocolVersion;
		std::string ClientName;
	};

	// A Model Context Protocol server: JSON-RPC 2.0 messages in, responses out, with tools as its only capability
	// (see docs/AIControl.md and docs/Decisions/0016-mcp-server.md). Transports - stdio, HTTP - move the messages; the
	// server handles them on the thread that calls it, which must be the editor's main thread
	class McpServer
	{
	public:
		// The protocol versions the server speaks, newest first
		static const std::vector<std::string>& GetSupportedProtocolVersions();

		McpServer(std::string name, std::string version, std::string instructions);

		void AddTool(McpTool tool);
		const std::vector<McpTool>& GetTools() const { return m_Tools; }
		const McpTool* FindTool(std::string_view name) const;

		// Handles one message. Returns the response to a request; notifications and responses get none
		std::optional<Json::Value> HandleMessage(const Json::Value& message, McpSession& session);
		// Parses and handles one message's text. Text that isn't JSON gets a parse error response
		std::optional<std::string> HandleText(std::string_view text, McpSession& session);

		// Calls a tool as tools/call would, for in-process use and tests
		McpToolResult CallTool(std::string_view name, const Json::Value& arguments) const;

	private:
		Json::Value Initialize(const Json::Value& params, McpSession& session) const;
		Json::Value ListTools() const;

	private:
		std::string m_Name;
		std::string m_Version;
		std::string m_Instructions;
		std::vector<McpTool> m_Tools;
	};

}
