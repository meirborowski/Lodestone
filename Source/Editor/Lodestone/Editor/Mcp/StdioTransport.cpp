#include "Lodestone/Editor/Mcp/StdioTransport.h"

#include "Lodestone/Core/Log.h"

#include <string>

namespace Lodestone {

	StdioTransport::StdioTransport(
		McpServer& server, MainThreadDispatcher& dispatcher, std::istream& input, std::ostream& output)
		: m_Server(&server), m_Dispatcher(&dispatcher), m_Input(&input), m_Output(&output)
	{
	}

	StdioTransport::~StdioTransport()
	{
		if (m_Reader.joinable())
			m_Reader.join();
	}

	void StdioTransport::Start()
	{
		m_Reader = std::thread([this] { Read(); });
	}

	void StdioTransport::Read()
	{
		std::string line;
		while (std::getline(*m_Input, line))
		{
			// A message never spans lines; blank lines (and the \r of CRLF line ends) carry nothing
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line.find_first_not_of(" \t") == std::string::npos)
				continue;

			const auto response = m_Dispatcher->Invoke([this, &line] { return m_Server->HandleText(line, m_Session); });
			if (!response)
				break;
			if (*response)
			{
				*m_Output << **response << '\n';
				m_Output->flush();
			}
		}
		LS_CORE_INFO("The MCP client closed standard input");
		m_Finished = true;
	}

}
