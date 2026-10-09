#pragma once

#include "Lodestone/Core/Log.h"

#include <spdlog/sinks/base_sink.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Lodestone {

	struct LogEntry
	{
		// Increases by one with every message, so readers can ask for what came after the last one they saw
		uint64_t Sequence = 0;
		LogLevel Level = LogLevel::Info;
		// "Engine" or "App"
		std::string Logger;
		std::string Message;
		// UTC, ISO 8601, to the millisecond
		std::string Time;
	};

	// Keeps the most recent log messages for the console panel and MCP's log tool. Thread-safe: any thread may log
	// while another reads
	class LogBuffer
	{
	public:
		explicit LogBuffer(size_t capacity = 4096);
		~LogBuffer();

		LogBuffer(const LogBuffer&) = delete;
		LogBuffer& operator=(const LogBuffer&) = delete;
		LogBuffer(LogBuffer&&) = delete;
		LogBuffer& operator=(LogBuffer&&) = delete;

		// Starts receiving the engine's log messages, until destroyed
		void Attach();

		// Messages after a sequence number, at or above a level, oldest first, at most maxCount of them (the latest)
		std::vector<LogEntry> GetEntries(
			uint64_t after = 0, LogLevel minimumLevel = LogLevel::Trace, size_t maxCount = SIZE_MAX) const;
		// The sequence number of the latest message, or 0 before the first
		uint64_t GetLatestSequence() const;
		void Clear();

	private:
		class Sink;

	private:
		std::shared_ptr<Sink> m_Sink;
		bool m_Attached = false;
	};

}
