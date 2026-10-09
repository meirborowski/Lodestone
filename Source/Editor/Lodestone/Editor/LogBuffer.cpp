#include "Lodestone/Editor/LogBuffer.h"

#include <spdlog/fmt/chrono.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <chrono>

namespace Lodestone {

	namespace {

		LogLevel ToLogLevel(spdlog::level::level_enum level)
		{
			switch (level)
			{
				case spdlog::level::trace:
					return LogLevel::Trace;
				case spdlog::level::debug:
					return LogLevel::Debug;
				case spdlog::level::info:
					return LogLevel::Info;
				case spdlog::level::warn:
					return LogLevel::Warn;
				case spdlog::level::err:
					return LogLevel::Error;
				case spdlog::level::critical:
					return LogLevel::Critical;
				default:
					return LogLevel::Off;
			}
		}

	}

	class LogBuffer::Sink final : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		explicit Sink(size_t capacity)
			: m_Capacity(capacity)
		{
		}

		std::vector<LogEntry> GetEntries(uint64_t after, LogLevel minimumLevel, size_t maxCount)
		{
			const std::scoped_lock lock(mutex_);
			std::vector<LogEntry> entries;
			for (auto entry = m_Entries.rbegin(); entry != m_Entries.rend() && entries.size() < maxCount; ++entry)
			{
				if (entry->Sequence <= after)
					break;
				if (entry->Level >= minimumLevel)
					entries.push_back(*entry);
			}
			std::ranges::reverse(entries);
			return entries;
		}

		uint64_t GetLatestSequence()
		{
			const std::scoped_lock lock(mutex_);
			return m_NextSequence - 1;
		}

		void Clear()
		{
			const std::scoped_lock lock(mutex_);
			m_Entries.clear();
		}

	protected:
		void sink_it_(const spdlog::details::log_msg& message) override
		{
			const auto milliseconds = std::chrono::time_point_cast<std::chrono::milliseconds>(message.time);
			m_Entries.push_back({
				.Sequence = m_NextSequence++,
				.Level = ToLogLevel(message.level),
				.Logger = std::string(message.logger_name.data(), message.logger_name.size()),
				.Message = std::string(message.payload.data(), message.payload.size()),
				.Time = fmt::format("{:%FT%TZ}", milliseconds),
			});
			if (m_Entries.size() > m_Capacity)
				m_Entries.pop_front();
		}

		void flush_() override {}

	private:
		size_t m_Capacity;
		std::deque<LogEntry> m_Entries;
		uint64_t m_NextSequence = 1;
	};

	LogBuffer::LogBuffer(size_t capacity)
		: m_Sink(std::make_shared<Sink>(capacity))
	{
	}

	LogBuffer::~LogBuffer()
	{
		if (m_Attached)
			Log::RemoveSink(m_Sink);
	}

	void LogBuffer::Attach()
	{
		if (!m_Attached)
		{
			Log::AddSink(m_Sink);
			m_Attached = true;
		}
	}

	std::vector<LogEntry> LogBuffer::GetEntries(uint64_t after, LogLevel minimumLevel, size_t maxCount) const
	{
		return m_Sink->GetEntries(after, minimumLevel, maxCount);
	}

	uint64_t LogBuffer::GetLatestSequence() const
	{
		return m_Sink->GetLatestSequence();
	}

	void LogBuffer::Clear()
	{
		m_Sink->Clear();
	}

}
