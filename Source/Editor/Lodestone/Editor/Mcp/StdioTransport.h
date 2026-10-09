#pragma once

#include "Lodestone/Editor/Mcp/MainThreadDispatcher.h"
#include "Lodestone/Editor/Mcp/McpServer.h"

#include <atomic>
#include <istream>
#include <mutex>
#include <ostream>
#include <thread>

namespace Lodestone {

	// MCP over standard input and output: one JSON-RPC message per line in each direction, for clients that start
	// the editor themselves (--headless --mcp-stdio). Messages are read on a thread of their own and handled on the
	// main thread. The editor's log must not write to the output stream, which carries only messages
	class StdioTransport
	{
	public:
		StdioTransport(McpServer& server, MainThreadDispatcher& dispatcher, std::istream& input, std::ostream& output);
		// Waits for the reader, which stops when the input ends or the dispatcher shuts down
		~StdioTransport();

		StdioTransport(const StdioTransport&) = delete;
		StdioTransport& operator=(const StdioTransport&) = delete;
		StdioTransport(StdioTransport&&) = delete;
		StdioTransport& operator=(StdioTransport&&) = delete;

		void Start();
		// Whether the input has ended - the client is gone - so the editor can exit
		bool IsFinished() const { return m_Finished.load(); }

	private:
		void Read();

	private:
		McpServer* m_Server;
		MainThreadDispatcher* m_Dispatcher;
		std::istream* m_Input;
		std::ostream* m_Output;
		McpSession m_Session;
		std::thread m_Reader;
		std::atomic<bool> m_Finished = false;
	};

}
