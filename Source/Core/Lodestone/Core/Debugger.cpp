#include "Lodestone/Core/Debugger.h"

#include "Lodestone/Core/Base.h"

#if defined(LS_PLATFORM_WINDOWS)
	#include <Windows.h>
#elif defined(LS_PLATFORM_MACOS)
	#include <array>

	#include <sys/sysctl.h>
	#include <sys/types.h>
	#include <unistd.h>
#elif defined(LS_PLATFORM_LINUX)
	#include <charconv>
	#include <fstream>
	#include <string>
	#include <string_view>
#endif

namespace Lodestone {

	bool IsDebuggerAttached()
	{
#if defined(LS_PLATFORM_WINDOWS)
		return IsDebuggerPresent() != FALSE;
#elif defined(LS_PLATFORM_MACOS)
		// A traced process has the P_TRACED flag set (Apple Technical Q&A QA1361)
		std::array<int, 4> name = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
		kinfo_proc info = {};
		size_t size = sizeof(info);
		if (sysctl(name.data(), static_cast<u_int>(name.size()), &info, &size, nullptr, 0) != 0)
			return false;
		return (info.kp_proc.p_flag & P_TRACED) != 0;
#elif defined(LS_PLATFORM_LINUX)
		// A traced process has a non-zero TracerPid in /proc/self/status
		constexpr std::string_view tracerPidField = "TracerPid:";
		std::ifstream status("/proc/self/status");
		std::string line;
		while (std::getline(status, line))
		{
			if (!line.starts_with(tracerPidField))
				continue;

			std::string_view value(line);
			value.remove_prefix(tracerPidField.size());
			while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
				value.remove_prefix(1);

			int tracerPid = 0;
			const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), tracerPid);
			return error == std::errc() && tracerPid != 0;
		}
		return false;
#endif
	}

}
