#pragma once

#include "Lodestone/Editor/LogBuffer.h"

#include <cstdint>
#include <deque>
#include <string>

namespace Lodestone {

	// The log, newest at the bottom, filtered by level and text
	class ConsolePanel
	{
	public:
		static constexpr const char* Title = "Console";
		static constexpr size_t Capacity = 4096;

		void Draw(const LogBuffer& log, bool* open);

	private:
		// Takes messages logged since the last frame
		void Update(const LogBuffer& log);

	private:
		std::deque<LogEntry> m_Entries;
		uint64_t m_LastSequence = 0;
		int m_MinimumLevel = static_cast<int>(LogLevel::Info);
		std::string m_Filter;
		bool m_AutoScroll = true;
	};

}
