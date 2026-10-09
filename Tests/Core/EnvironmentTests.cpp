#include "Lodestone/Core/Environment.h"

#include <doctest/doctest.h>

#include <optional>
#include <string>

namespace Lodestone {

	TEST_CASE("ReadEnvironmentVariable reads a variable that's set")
	{
		// Every process on every supported platform has a PATH
		const auto path = ReadEnvironmentVariable("PATH");

		REQUIRE(path.has_value());
		CHECK_FALSE(path.value_or(std::string()).empty());
	}

	TEST_CASE("ReadEnvironmentVariable returns nothing for a variable that isn't set")
	{
		CHECK_FALSE(ReadEnvironmentVariable("LODESTONE_TEST_VARIABLE_THAT_IS_NEVER_SET").has_value());
	}

	TEST_CASE("WriteEnvironmentVariable sets a variable for this process")
	{
		REQUIRE(WriteEnvironmentVariable("LODESTONE_TEST_WRITTEN_VARIABLE", "written value").has_value());

		CHECK(ReadEnvironmentVariable("LODESTONE_TEST_WRITTEN_VARIABLE") == "written value");
	}

	TEST_CASE("WriteEnvironmentVariable rejects invalid names")
	{
		CHECK_FALSE(WriteEnvironmentVariable("", "value").has_value());
		CHECK_FALSE(WriteEnvironmentVariable("NAME=WITH=EQUALS", "value").has_value());
	}

}
