#include "Lodestone/Core/UUID.h"

#include <algorithm>
#include <array>
#include <random>

namespace Lodestone {

	namespace {

		constexpr size_t CanonicalLength = 36;
		constexpr std::array<size_t, 4> HyphenPositions = {8, 13, 18, 23};

		std::mt19937_64& GetGenerator()
		{
			// One generator per thread, seeded from the operating system's entropy source
			thread_local std::mt19937_64 s_Generator = []
			{
				std::random_device device;
				std::seed_seq seed{device(), device(), device(), device(), device(), device(), device(), device()};
				return std::mt19937_64(seed);
			}();
			return s_Generator;
		}

		std::optional<uint8_t> ParseHexDigit(char character)
		{
			if (character >= '0' && character <= '9')
				return static_cast<uint8_t>(character - '0');
			if (character >= 'a' && character <= 'f')
				return static_cast<uint8_t>(character - 'a' + 10);
			if (character >= 'A' && character <= 'F')
				return static_cast<uint8_t>(character - 'A' + 10);
			return std::nullopt;
		}

	}

	UUID UUID::Generate()
	{
		std::mt19937_64& generator = GetGenerator();
		const uint64_t high = generator();
		const uint64_t low = generator();
		return FromRandomBits(high, low);
	}

	UUID UUID::FromRandomBits(uint64_t high, uint64_t low)
	{
		// Version 4 (random) in the version field, and the RFC 9562 variant
		high = (high & 0xFFFF'FFFF'FFFF'0FFFull) | 0x0000'0000'0000'4000ull;
		low = (low & 0x3FFF'FFFF'FFFF'FFFFull) | 0x8000'0000'0000'0000ull;
		return {high, low};
	}

	std::optional<UUID> UUID::Parse(std::string_view text)
	{
		if (text.size() != CanonicalLength)
			return std::nullopt;

		uint64_t high = 0;
		uint64_t low = 0;
		size_t digitIndex = 0;
		for (size_t position = 0; position < text.size(); ++position)
		{
			const bool hyphenExpected = std::ranges::find(HyphenPositions, position) != HyphenPositions.end();
			if (hyphenExpected)
			{
				if (text[position] != '-')
					return std::nullopt;
				continue;
			}

			const std::optional<uint8_t> digit = ParseHexDigit(text[position]);
			if (!digit)
				return std::nullopt;
			uint64_t& half = digitIndex < 16 ? high : low;
			half = (half << 4) | *digit;
			++digitIndex;
		}
		return UUID(high, low);
	}

	std::string UUID::ToString() const
	{
		constexpr std::string_view digits = "0123456789abcdef";
		std::string text;
		text.reserve(CanonicalLength);
		for (size_t digitIndex = 0; digitIndex < 32; ++digitIndex)
		{
			if (digitIndex == 8 || digitIndex == 12 || digitIndex == 16 || digitIndex == 20)
				text.push_back('-');
			const uint64_t half = digitIndex < 16 ? m_High : m_Low;
			const auto shift = static_cast<unsigned>((15 - (digitIndex % 16)) * 4);
			text.push_back(digits[(half >> shift) & 0xF]);
		}
		return text;
	}

}
