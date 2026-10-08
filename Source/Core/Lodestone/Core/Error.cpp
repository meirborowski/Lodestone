#include "Lodestone/Core/Error.h"

namespace Lodestone {

	std::string_view ToString(ErrorCode code)
	{
		switch (code)
		{
			case ErrorCode::Unknown:
				return "Unknown";
			case ErrorCode::InvalidArgument:
				return "InvalidArgument";
			case ErrorCode::InvalidState:
				return "InvalidState";
			case ErrorCode::NotFound:
				return "NotFound";
			case ErrorCode::AlreadyExists:
				return "AlreadyExists";
			case ErrorCode::FileNotFound:
				return "FileNotFound";
			case ErrorCode::IoError:
				return "IoError";
			case ErrorCode::ParseError:
				return "ParseError";
			case ErrorCode::UnsupportedVersion:
				return "UnsupportedVersion";
			case ErrorCode::Unsupported:
				return "Unsupported";
			case ErrorCode::OutOfMemory:
				return "OutOfMemory";
			case ErrorCode::ScriptError:
				return "ScriptError";
		}
		// Only reachable when a value outside the enumeration is cast to ErrorCode
		return "InvalidErrorCode";
	}

	Error::Error(ErrorCode code, std::string message)
		: m_Code(code), m_Message(std::move(message))
	{
	}

	std::string Error::ToString() const
	{
		std::string result(Lodestone::ToString(m_Code));
		if (!m_Message.empty())
		{
			result += ": ";
			result += m_Message;
		}
		return result;
	}

	void Error::PrependContext(std::string_view context)
	{
		if (context.empty())
			return;

		if (m_Message.empty())
		{
			m_Message = context;
			return;
		}

		std::string message;
		message.reserve(context.size() + 2 + m_Message.size());
		message += context;
		message += ": ";
		message += m_Message;
		m_Message = std::move(message);
	}

}
