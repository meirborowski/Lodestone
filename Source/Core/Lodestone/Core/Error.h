#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace Lodestone {

	// The category of a recoverable error. Callers branch on the code; the message is for people.
	// When adding a code, add it to ToString() as well
	enum class ErrorCode : uint16_t
	{
		Unknown,
		InvalidArgument,
		InvalidState,
		NotFound,
		AlreadyExists,
		FileNotFound,
		IoError,
		ParseError,
		UnsupportedVersion,
		Unsupported,
		OutOfMemory,
		ScriptError,
	};

	[[nodiscard]] std::string_view ToString(ErrorCode code);

	// A recoverable error, returned as std::expected<T, Error> (see docs/CodeStyle.md#error-handling)
	class Error
	{
	public:
		explicit Error(ErrorCode code, std::string message = {});

		ErrorCode GetCode() const { return m_Code; }
		// Named GetMessageText rather than GetMessage, which <windows.h> defines as a macro
		const std::string& GetMessageText() const { return m_Message; }

		// Returns this error with context prepended to its message, for callers that pass an error on:
		// Error(ErrorCode::FileNotFound, "hero.png").WithContext("Loading material 'Hero'")
		// has the message "Loading material 'Hero': hero.png"
		template <typename Self>
		[[nodiscard]] Error WithContext(this Self&& self, std::string_view context)
		{
			Error result = std::forward<Self>(self);
			result.PrependContext(context);
			return result;
		}

		// The code and message, for logs: "FileNotFound: Loading material 'Hero': hero.png"
		[[nodiscard]] std::string ToString() const;

		bool operator==(const Error& other) const = default;

	private:
		void PrependContext(std::string_view context);

	private:
		ErrorCode m_Code;
		std::string m_Message;
	};

	// fmt customization points (named as fmt requires), so the log macros can format errors and error codes directly
	inline std::string_view format_as(ErrorCode code)
	{
		return ToString(code);
	}

	inline std::string format_as(const Error& error)
	{
		return error.ToString();
	}

}
