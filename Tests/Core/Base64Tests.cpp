#include "Lodestone/Core/Base64.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Lodestone {

	namespace {

		std::string EncodeText(std::string_view text)
		{
			const std::vector<uint8_t> bytes(text.begin(), text.end());
			return EncodeBase64(bytes);
		}

	}

	TEST_CASE("Base64 encoding matches the RFC 4648 test vectors")
	{
		CHECK(EncodeText("") == "");
		CHECK(EncodeText("f") == "Zg==");
		CHECK(EncodeText("fo") == "Zm8=");
		CHECK(EncodeText("foo") == "Zm9v");
		CHECK(EncodeText("foob") == "Zm9vYg==");
		CHECK(EncodeText("fooba") == "Zm9vYmE=");
		CHECK(EncodeText("foobar") == "Zm9vYmFy");
	}

	TEST_CASE("Base64 encoding uses every character of the standard alphabet, in order")
	{
		// Each 6-bit group counts up from 0 to 63
		constexpr std::array<uint8_t, 48> bytes = {0x00, 0x10, 0x83, 0x10, 0x51, 0x87, 0x20, 0x92, 0x8B, 0x30, 0xD3,
			0x8F, 0x41, 0x14, 0x93, 0x51, 0x55, 0x97, 0x61, 0x96, 0x9B, 0x71, 0xD7, 0x9F, 0x82, 0x18, 0xA3, 0x92, 0x59,
			0xA7, 0xA2, 0x9A, 0xAB, 0xB2, 0xDB, 0xAF, 0xC3, 0x1C, 0xB3, 0xD3, 0x5D, 0xB7, 0xE3, 0x9E, 0xBB, 0xF3, 0xDF,
			0xBF};
		CHECK(EncodeBase64(bytes) == "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
	}

}
