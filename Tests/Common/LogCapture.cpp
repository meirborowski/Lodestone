#include "Common/LogCapture.h"

#include "Lodestone/Core/Log.h"

#include <spdlog/pattern_formatter.h>

namespace Lodestone::Testing {

	namespace {

		constexpr size_t MaxCapturedMessages = 64;

	}

	LogCapture::LogCapture()
		: m_Sink(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(MaxCapturedMessages))
	{
		// No end-of-line, so captured messages compare equal to plain strings
		m_Sink->set_formatter(
			std::make_unique<spdlog::pattern_formatter>("%n %l %v", spdlog::pattern_time_type::local, ""));
		Log::AddSink(m_Sink);
	}

	LogCapture::~LogCapture()
	{
		Log::RemoveSink(m_Sink);
	}

	std::vector<std::string> LogCapture::GetMessages() const
	{
		return m_Sink->last_formatted();
	}

}
