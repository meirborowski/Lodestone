#include "Lodestone/Core/RunMain.h"

#include <cstdio>
#include <cstdlib>
#include <exception>

namespace Lodestone {

	namespace {

		void ReportUnhandledException(std::string_view applicationName, std::string_view description) noexcept
		{
			try
			{
				Log::ReportFatalError(Log::GetCoreLogger(),
					fmt::format("{} stopped because of an unhandled exception: {}", applicationName, description));
			}
			catch (...)
			{
				// Formatting the report failed - most likely out of memory - so report without allocating
				std::fputs("Stopped because of an unhandled exception, which couldn't be reported\n", stderr);
				std::fflush(stderr);
			}
		}

	}

	int RunMain(std::string_view applicationName, const LogConfig& logConfig, const std::function<int()>& body) noexcept
	{
		try
		{
			if (const auto result = Log::Init(logConfig); !result)
			{
				Log::ReportFatalError(
					Log::GetCoreLogger(), fmt::format("{} can't start: {}", applicationName, result.error()));
				return EXIT_FAILURE;
			}

			const int exitCode = body();
			Log::Shutdown();
			return exitCode;
		}
		catch (const std::exception& exception)
		{
			ReportUnhandledException(applicationName, exception.what());
		}
		catch (...)
		{
			ReportUnhandledException(applicationName, "an exception not derived from std::exception");
		}

		Log::Shutdown();
		return EXIT_FAILURE;
	}

}
