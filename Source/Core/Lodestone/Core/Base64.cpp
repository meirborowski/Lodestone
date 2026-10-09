#include "Lodestone/Core/Base64.h"

#include <string_view>

namespace Lodestone {

	std::string EncodeBase64(std::span<const uint8_t> data)
	{
		constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string encoded;
		encoded.reserve((data.size() + 2) / 3 * 4);
		size_t index = 0;
		for (; index + 2 < data.size(); index += 3)
		{
			const uint32_t group = (uint32_t{data[index]} << 16) | (uint32_t{data[index + 1]} << 8) | data[index + 2];
			encoded.push_back(alphabet[(group >> 18) & 0x3F]);
			encoded.push_back(alphabet[(group >> 12) & 0x3F]);
			encoded.push_back(alphabet[(group >> 6) & 0x3F]);
			encoded.push_back(alphabet[group & 0x3F]);
		}

		const size_t remaining = data.size() - index;
		if (remaining == 1)
		{
			const uint32_t group = uint32_t{data[index]} << 16;
			encoded.push_back(alphabet[(group >> 18) & 0x3F]);
			encoded.push_back(alphabet[(group >> 12) & 0x3F]);
			encoded.append("==");
		}
		else if (remaining == 2)
		{
			const uint32_t group = (uint32_t{data[index]} << 16) | (uint32_t{data[index + 1]} << 8);
			encoded.push_back(alphabet[(group >> 18) & 0x3F]);
			encoded.push_back(alphabet[(group >> 12) & 0x3F]);
			encoded.push_back(alphabet[(group >> 6) & 0x3F]);
			encoded.push_back('=');
		}
		return encoded;
	}

}
