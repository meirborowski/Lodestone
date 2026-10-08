#include "Lodestone/Core/Log.h"

#include "Common/LogCapture.h"
#include "Common/LogLevelScope.h"
#include "Common/LogShutdownScope.h"
#include "Common/TemporaryDirectory.h"

#include <doctest/doctest.h>
#include <spdlog/sinks/ringbuffer_sink.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace Lodestone {

	namespace {

		std::string ReadFile(const std::filesystem::path& path)
		{
			std::ifstream file(path);
			return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		}

	}

	TEST_CASE("The core and app loggers write to added sinks under their own names")
	{
		const Testing::LogCapture capture;

		LS_CORE_WARN("Core warning {}", 1);
		LS_ERROR("App error {}", 2);

		const std::vector<std::string> messages = capture.GetMessages();
		REQUIRE(messages.size() == 2);
		CHECK(messages[0] == "Engine warning Core warning 1");
		CHECK(messages[1] == "App error App error 2");
	}

	TEST_CASE("Every log macro writes at its level")
	{
		const Testing::LogCapture capture;
		const Testing::LogLevelScope level(LogLevel::Trace);

		LS_CORE_TRACE("a");
		LS_CORE_DEBUG("b");
		LS_CORE_INFO("c");
		LS_CORE_WARN("d");
		LS_CORE_ERROR("e");
		LS_CORE_CRITICAL("f");
		LS_TRACE("g");
		LS_DEBUG("h");
		LS_INFO("i");
		LS_WARN("j");
		LS_ERROR("k");
		LS_CRITICAL("l");

		const std::vector<std::string> messages = capture.GetMessages();
#if LS_ENABLE_DEVELOPER_LOGGING
		const std::vector<std::string> expected = {
			"Engine trace a",
			"Engine debug b",
			"Engine info c",
			"Engine warning d",
			"Engine error e",
			"Engine critical f",
			"App trace g",
			"App debug h",
			"App info i",
			"App warning j",
			"App error k",
			"App critical l",
		};
#else
		// Trace, debug and info messages are compiled out of Dist builds
		const std::vector<std::string> expected = {
			"Engine warning d",
			"Engine error e",
			"Engine critical f",
			"App warning j",
			"App error k",
			"App critical l",
		};
#endif
		CHECK(messages == expected);
	}

	TEST_CASE("Messages below the log level are discarded")
	{
		const Testing::LogCapture capture;
		const Testing::LogLevelScope level(LogLevel::Error);

		CHECK(Log::GetLevel() == LogLevel::Error);
		LS_CORE_WARN("Discarded");
		LS_WARN("Discarded");
		LS_CORE_ERROR("Kept");

		const std::vector<std::string> messages = capture.GetMessages();
		REQUIRE(messages.size() == 1);
		CHECK(messages[0] == "Engine error Kept");
	}

	TEST_CASE("A removed sink receives no more messages")
	{
		const auto sink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(8);
		Log::AddSink(sink);
		LS_CORE_WARN("Before");
		Log::RemoveSink(sink);
		LS_CORE_WARN("After");

		CHECK(sink->last_raw().size() == 1);
	}

	TEST_CASE("Log::ReportFatalError logs the report at critical level")
	{
		const Testing::LogCapture capture;

		Log::ReportFatalError(Log::GetAppLogger(), "Out of cheese");

		CHECK(capture.GetMessages() == std::vector<std::string>{"App critical Out of cheese"});
	}

	TEST_CASE("Log::Init fails when the log is already initialized")
	{
		REQUIRE(Log::IsInitialized());

		const auto result = Log::Init();

		REQUIRE_FALSE(result.has_value());
		CHECK(result.error().GetCode() == ErrorCode::InvalidState);
		CHECK(Log::IsInitialized());
	}

	TEST_CASE("Logging is safe while the log isn't initialized")
	{
		const Testing::LogShutdownScope shutdown;
		const Testing::LogCapture capture;

		CHECK_FALSE(Log::IsInitialized());
		LS_CORE_ERROR("Still captured by added sinks");

		CHECK(capture.GetMessages().size() == 1);
	}

	TEST_CASE("The log file receives messages")
	{
		const Testing::TemporaryDirectory directory("LogFile");
		const std::filesystem::path logPath = directory.GetPath() / "Logs" / "Lodestone.log";
		{
			const Testing::LogShutdownScope shutdown;
			const auto result = Log::Init({.Console = LogConsole::None, .FilePath = logPath});
			REQUIRE(result.has_value());

			LS_CORE_WARN("Written to the file {}", 42);
		}

		const std::string contents = ReadFile(logPath);
		CHECK(contents.find("[warning] Engine: Written to the file 42") != std::string::npos);
	}

	TEST_CASE("Log::Init fails when the log file can't be created")
	{
		const Testing::TemporaryDirectory directory("BadLogFile");
		// A regular file where the log file's directory should be
		const std::filesystem::path blocker = directory.GetPath() / "NotADirectory";
		std::ofstream(blocker) << "blocker";

		const Testing::LogShutdownScope shutdown;
		const auto result = Log::Init({.Console = LogConsole::None, .FilePath = blocker / "Lodestone.log"});

		REQUIRE_FALSE(result.has_value());
		CHECK(result.error().GetCode() == ErrorCode::IoError);
		CHECK(result.error().GetMessageText().find("NotADirectory") != std::string::npos);
		CHECK_FALSE(Log::IsInitialized());
	}

}
