#include "Lodestone/Core/Log.h"

#include <spdlog/fmt/std.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <vector>

namespace Lodestone {

	namespace {

		constexpr const char* ConsolePattern = "%^[%T.%e] [%l] %n: %v%$";
		constexpr const char* FilePattern = "[%Y-%m-%d %T.%e] [%l] %n: %v";

		spdlog::level::level_enum ToSpdlogLevel(LogLevel level)
		{
			switch (level)
			{
				case LogLevel::Trace:
					return spdlog::level::trace;
				case LogLevel::Debug:
					return spdlog::level::debug;
				case LogLevel::Info:
					return spdlog::level::info;
				case LogLevel::Warn:
					return spdlog::level::warn;
				case LogLevel::Error:
					return spdlog::level::err;
				case LogLevel::Critical:
					return spdlog::level::critical;
				case LogLevel::Off:
					return spdlog::level::off;
			}
			return spdlog::level::trace;
		}

		LogLevel FromSpdlogLevel(spdlog::level::level_enum level)
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
				case spdlog::level::off:
				case spdlog::level::n_levels:
					return LogLevel::Off;
			}
			return LogLevel::Off;
		}

		// The two loggers write to one distributing sink, which forwards every message to the sinks added to it
		struct LogState
		{
			LogState()
			{
				// Messages at warning level and above are flushed immediately, so they survive a crash
				CoreLogger.flush_on(spdlog::level::warn);
				AppLogger.flush_on(spdlog::level::warn);
			}

			std::shared_ptr<spdlog::sinks::dist_sink_mt> Sink = std::make_shared<spdlog::sinks::dist_sink_mt>();
			spdlog::logger CoreLogger{"Engine", Sink};
			spdlog::logger AppLogger{"App", Sink};

			std::mutex InitMutex;
			std::atomic<bool> Initialized = false;
			// Whether Init() added a console sink
			std::atomic<bool> WritesToConsole = false;
			// The sinks Init() added, which Shutdown() removes
			std::vector<spdlog::sink_ptr> InitSinks;
		};

		LogState& GetState()
		{
			// Created on first use and intentionally never destroyed, so logging stays valid during static
			// initialization and destruction
			static auto* s_State = new LogState();
			return *s_State;
		}

		[[nodiscard]] std::expected<std::vector<spdlog::sink_ptr>, Error> CreateSinks(const LogConfig& config)
		{
			std::vector<spdlog::sink_ptr> sinks;

			// spdlog reports failures with exceptions, which must not escape into engine code
			try
			{
				if (config.Console == LogConsole::StandardOutput)
					sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
				else if (config.Console == LogConsole::StandardError)
					sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
				if (!sinks.empty())
					sinks.back()->set_pattern(ConsolePattern);
			}
			catch (const std::exception& exception)
			{
				return std::unexpected(
					Error(ErrorCode::IoError, fmt::format("Can't open the console log: {}", exception.what())));
			}

			if (!config.FilePath.empty())
			{
				try
				{
					auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.FilePath.string(), true);
					file->set_pattern(FilePattern);
					sinks.push_back(std::move(file));
				}
				catch (const std::exception& exception)
				{
					return std::unexpected(Error(ErrorCode::IoError,
						fmt::format("Can't open the log file {}: {}", config.FilePath, exception.what())));
				}
			}

			return sinks;
		}

	}

	std::expected<void, Error> Log::Init(const LogConfig& config)
	{
		LogState& state = GetState();
		const std::scoped_lock lock(state.InitMutex);
		if (state.Initialized)
			return std::unexpected(Error(ErrorCode::InvalidState, "Log is already initialized"));

		auto sinks = CreateSinks(config);
		if (!sinks)
			return std::unexpected(std::move(sinks).error());

		for (const spdlog::sink_ptr& sink : *sinks)
			state.Sink->add_sink(sink);
		state.InitSinks = std::move(*sinks);
		SetLevel(config.Level);
		state.WritesToConsole = config.Console != LogConsole::None;
		state.Initialized = true;
		return {};
	}

	void Log::Shutdown()
	{
		LogState& state = GetState();
		const std::scoped_lock lock(state.InitMutex);
		if (!state.Initialized)
			return;

		Flush();
		for (const spdlog::sink_ptr& sink : state.InitSinks)
			state.Sink->remove_sink(sink);
		state.InitSinks.clear();
		state.WritesToConsole = false;
		state.Initialized = false;
	}

	bool Log::IsInitialized()
	{
		return GetState().Initialized;
	}

	void Log::SetLevel(LogLevel level)
	{
		LogState& state = GetState();
		state.CoreLogger.set_level(ToSpdlogLevel(level));
		state.AppLogger.set_level(ToSpdlogLevel(level));
	}

	LogLevel Log::GetLevel()
	{
		return FromSpdlogLevel(GetState().CoreLogger.level());
	}

	void Log::AddSink(const spdlog::sink_ptr& sink)
	{
		GetState().Sink->add_sink(sink);
	}

	void Log::RemoveSink(const spdlog::sink_ptr& sink)
	{
		GetState().Sink->remove_sink(sink);
	}

	void Log::Flush()
	{
		LogState& state = GetState();
		state.CoreLogger.flush();
		state.AppLogger.flush();
	}

	void Log::ReportFatalError(spdlog::logger& logger, std::string_view report) noexcept
	{
		bool reportedOnConsole = false;
		try
		{
			logger.critical("{}", report);
			logger.flush();
			reportedOnConsole = GetState().WritesToConsole;
		}
		catch (...)
		{
			// The log failed, so the standard error is the only place left for the report
			reportedOnConsole = false;
		}

		if (!reportedOnConsole)
		{
			std::fwrite(report.data(), 1, report.size(), stderr);
			std::fputc('\n', stderr);
			std::fflush(stderr);
		}
	}

	spdlog::logger& Log::GetCoreLogger()
	{
		return GetState().CoreLogger;
	}

	spdlog::logger& Log::GetAppLogger()
	{
		return GetState().AppLogger;
	}

}
