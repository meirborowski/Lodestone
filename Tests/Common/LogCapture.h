#pragma once

#include <spdlog/sinks/ringbuffer_sink.h>

#include <memory>
#include <string>
#include <vector>

namespace Lodestone::Testing {

	// Captures the messages of both engine loggers for its lifetime, formatted as "<logger> <level> <message>"
	class LogCapture
	{
	public:
		LogCapture();
		~LogCapture();

		LogCapture(const LogCapture&) = delete;
		LogCapture& operator=(const LogCapture&) = delete;
		LogCapture(LogCapture&&) = delete;
		LogCapture& operator=(LogCapture&&) = delete;

		std::vector<std::string> GetMessages() const;

	private:
		std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> m_Sink;
	};

}
