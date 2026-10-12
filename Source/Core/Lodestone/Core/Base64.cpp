#include "Lodestone/Core/Base64.h"

#include <spdlog/fmt/fmt.h>

#include <string_view>

namespace Lodestone {

	namespace {

		constexpr std::string_view Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		// The 6-bit value of a Base64 character, or -1 for one outside the alphabet
		int DecodeCharacter(char character)
		{
			if (character >= 'A' && character <= 'Z')
				return character - 'A';
			if (character >= 'a' && character <= 'z')
				return character - 'a' + 26;
			if (character >= '0' && character <= '9')
				return character - '0' + 52;
			if (character == '+')
				return 62;
			if (character == '/')
				return 63;
			return -1;
		}

	}

	std::string EncodeBase64(std::span<const uint8_t> data)
	{
		std::string encoded;
		encoded.reserve((data.size() + 2) / 3 * 4);
		size_t index = 0;
		for (; index + 2 < data.size(); index += 3)
		{
			const uint32_t group = (uint32_t{data[index]} << 16) | (uint32_t{data[index + 1]} << 8) | data[index + 2];
			encoded.push_back(Alphabet[(group >> 18) & 0x3F]);
			encoded.push_back(Alphabet[(group >> 12) & 0x3F]);
			encoded.push_back(Alphabet[(group >> 6) & 0x3F]);
			encoded.push_back(Alphabet[group & 0x3F]);
		}

		const size_t remaining = data.size() - index;
		if (remaining == 1)
		{
			const uint32_t group = uint32_t{data[index]} << 16;
			encoded.push_back(Alphabet[(group >> 18) & 0x3F]);
			encoded.push_back(Alphabet[(group >> 12) & 0x3F]);
			encoded.append("==");
		}
		else if (remaining == 2)
		{
			const uint32_t group = (uint32_t{data[index]} << 16) | (uint32_t{data[index + 1]} << 8);
			encoded.push_back(Alphabet[(group >> 18) & 0x3F]);
			encoded.push_back(Alphabet[(group >> 12) & 0x3F]);
			encoded.push_back(Alphabet[(group >> 6) & 0x3F]);
			encoded.push_back('=');
		}
		return encoded;
	}

	std::expected<std::vector<uint8_t>, Error> DecodeBase64(std::string_view text)
	{
		// Up to two padding characters end the text
		size_t length = text.size();
		for (int padding = 0; padding < 2 && length > 0 && text[length - 1] == '='; ++padding)
			--length;
		if (length % 4 == 1)
			return std::unexpected(Error(ErrorCode::ParseError, "The Base64 text has an impossible length"));

		std::vector<uint8_t> bytes;
		bytes.reserve(length / 4 * 3 + 2);
		uint32_t group = 0;
		int bits = 0;
		for (size_t index = 0; index < length; ++index)
		{
			const int value = DecodeCharacter(text[index]);
			if (value < 0)
				return std::unexpected(Error(ErrorCode::ParseError,
					fmt::format("The Base64 text has a character outside its alphabet at {}", index)));
			group = (group << 6) | static_cast<uint32_t>(value);
			bits += 6;
			if (bits >= 8)
			{
				bits -= 8;
				bytes.push_back(static_cast<uint8_t>((group >> bits) & 0xFF));
			}
		}
		return bytes;
	}

}
