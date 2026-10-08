#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"

#include <spdlog/logger.h>

#include <expected>
#include <filesystem>
#include <string_view>

namespace Lodestone {

	enum class LogLevel
	{
		Trace,
		Debug,
		Info,
		Warn,
		Error,
		Critical,
		Off
	};

	// Where console log output goes. Processes that use the standard output for something else - such as the MCP
	// stdio transport - log to the standard error instead
	enum class LogConsole
	{
		None,
		StandardOutput,
		StandardError
	};

	struct LogConfig
	{
		// Write messages to the console, colored where the terminal supports it
		LogConsole Console = LogConsole::StandardOutput;
		// Also write messages to this file, replacing any existing file. Empty for no log file
		std::filesystem::path FilePath;
		// Messages below this level are discarded
		LogLevel Level = LogLevel::Trace;
	};

	// The engine's log. Engine code logs to the core logger with the LS_CORE_* macros; applications and games log to
	// the app logger with the LS_* macros.
	//
	// Both loggers always exist, so logging is safe at any time - even before Init() and after Shutdown() - but
	// messages only go somewhere once sinks have been added. Init(), Shutdown(), AddSink() and RemoveSink() are
	// thread-safe and may be called while other threads log.
	class Log
	{
	public:
		// Adds the console and file sinks described by the config. Fails if Log is already initialized, or if the log
		// file can't be opened
		[[nodiscard]] static std::expected<void, Error> Init(const LogConfig& config = {});
		// Flushes and removes the sinks Init() added. Sinks added with AddSink() stay
		static void Shutdown();
		static bool IsInitialized();

		static void SetLevel(LogLevel level);
		static LogLevel GetLevel();

		// Adds a sink that receives the messages of both loggers - the editor console, or a test capturing output.
		// The sink keeps its own formatter
		static void AddSink(const spdlog::sink_ptr& sink);
		static void RemoveSink(const spdlog::sink_ptr& sink);

		static void Flush();

		// Reports a fatal error - a failed assert, or an exception that ends the program - at critical level, and
		// flushes the log. When the log doesn't write to the console, the report also goes straight to the standard
		// error, so it's never lost
		static void ReportFatalError(spdlog::logger& logger, std::string_view report) noexcept;

		static spdlog::logger& GetCoreLogger();
		static spdlog::logger& GetAppLogger();
	};

	namespace Detail {

		// Mentions the arguments of a compiled-out log macro in an unevaluated context, so variables used only in
		// log messages don't trigger unused-variable warnings in Dist builds. Never defined, never called
		template <typename... Args>
		int MarkLogArgumentsUsed(const Args&... args);

	}

}

// Engine log macros. Arguments are a fmt format string and its arguments: LS_CORE_INFO("Loaded {} assets", count)
#if LS_ENABLE_DEVELOPER_LOGGING
	#define LS_CORE_TRACE(...) ::Lodestone::Log::GetCoreLogger().trace(__VA_ARGS__)
	#define LS_CORE_DEBUG(...) ::Lodestone::Log::GetCoreLogger().debug(__VA_ARGS__)
	#define LS_CORE_INFO(...) ::Lodestone::Log::GetCoreLogger().info(__VA_ARGS__)
#else
	#define LS_CORE_TRACE(...) static_cast<void>(sizeof(::Lodestone::Detail::MarkLogArgumentsUsed(__VA_ARGS__)))
	#define LS_CORE_DEBUG(...) static_cast<void>(sizeof(::Lodestone::Detail::MarkLogArgumentsUsed(__VA_ARGS__)))
	#define LS_CORE_INFO(...) static_cast<void>(sizeof(::Lodestone::Detail::MarkLogArgumentsUsed(__VA_ARGS__)))
#endif
#define LS_CORE_WARN(...) ::Lodestone::Log::GetCoreLogger().warn(__VA_ARGS__)
#define LS_CORE_ERROR(...) ::Lodestone::Log::GetCoreLogger().error(__VA_ARGS__)
#define LS_CORE_CRITICAL(...) ::Lodestone::Log::GetCoreLogger().critical(__VA_ARGS__)

// Application and game log macros
#if LS_ENABLE_DEVELOPER_LOGGING
	#define LS_TRACE(...) ::Lodestone::Log::GetAppLogger().trace(__VA_ARGS__)
	#define LS_DEBUG(...) ::Lodestone::Log::GetAppLogger().debug(__VA_ARGS__)
	#define LS_INFO(...) ::Lodestone::Log::GetAppLogger().info(__VA_ARGS__)
#else
	#define LS_TRACE(...) static_cast<void>(sizeof(::Lodestone::Detail::MarkLogArgumentsUsed(__VA_ARGS__)))
	#define LS_DEBUG(...) static_cast<void>(sizeof(::Lodestone::Detail::MarkLogArgumentsUsed(__VA_ARGS__)))
	#define LS_INFO(...) static_cast<void>(sizeof(::Lodestone::Detail::MarkLogArgumentsUsed(__VA_ARGS__)))
#endif
#define LS_WARN(...) ::Lodestone::Log::GetAppLogger().warn(__VA_ARGS__)
#define LS_ERROR(...) ::Lodestone::Log::GetAppLogger().error(__VA_ARGS__)
#define LS_CRITICAL(...) ::Lodestone::Log::GetAppLogger().critical(__VA_ARGS__)
