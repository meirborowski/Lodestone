#include "Lodestone/Core/UUID.h"

#include <doctest/doctest.h>
#include <spdlog/fmt/fmt.h>

#include <string>
#include <unordered_set>

namespace Lodestone {

	TEST_CASE("The default UUID is nil")
	{
		CHECK(UUID().IsNil());
		CHECK(UUID().ToString() == "00000000-0000-0000-0000-000000000000");
	}

	TEST_CASE("Generated UUIDs are random version 4 UUIDs")
	{
		std::unordered_set<UUID> seen;
		for (int i = 0; i < 1000; ++i)
		{
			const UUID uuid = UUID::Generate();
			CHECK_FALSE(uuid.IsNil());
			const std::string text = uuid.ToString();
			CAPTURE(text);
			// The version digit, and the variant bits 10xx
			CHECK(text[14] == '4');
			CHECK(std::string_view("89ab").contains(text[19]));
			CHECK(seen.insert(uuid).second);
		}
	}

	TEST_CASE("UUIDs round-trip through their canonical text")
	{
		const UUID uuid(0x0123'4567'89ab'cdefull, 0xfedc'ba98'7654'3210ull);
		CHECK(uuid.ToString() == "01234567-89ab-cdef-fedc-ba9876543210");
		CHECK(UUID::Parse(uuid.ToString()) == uuid);

		const UUID generated = UUID::Generate();
		CHECK(UUID::Parse(generated.ToString()) == generated);
	}

	TEST_CASE("UUID parsing accepts uppercase digits")
	{
		CHECK(UUID::Parse("01234567-89AB-CDEF-FEDC-BA9876543210") ==
			UUID(0x0123'4567'89ab'cdefull, 0xfedc'ba98'7654'3210ull));
	}

	TEST_CASE("UUID parsing rejects anything but the canonical form")
	{
		for (const char* text : {"", "01234567-89ab-cdef-fedc-ba987654321", "01234567-89ab-cdef-fedc-ba98765432100",
				 "0123456789abcdeffedcba9876543210", "01234567_89ab_cdef_fedc_ba9876543210",
				 "0123456g-89ab-cdef-fedc-ba9876543210", "{01234567-89ab-cdef-fedc-ba98765432}",
				 " 1234567-89ab-cdef-fedc-ba9876543210"})
		{
			CAPTURE(text);
			CHECK_FALSE(UUID::Parse(text).has_value());
		}
	}

	TEST_CASE("UUIDs format in log messages in their canonical form")
	{
		CHECK(fmt::format("{}", UUID(1, 2)) == "00000000-0000-0001-0000-000000000002");
	}

	TEST_CASE("UUIDs order by their value")
	{
		CHECK(UUID(0, 1) < UUID(0, 2));
		CHECK(UUID(0, 0xFFFF'FFFF'FFFF'FFFFull) < UUID(1, 0));
		CHECK(UUID(3, 4) == UUID(3, 4));
	}

}
