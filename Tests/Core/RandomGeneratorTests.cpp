#include "Lodestone/Core/RandomGenerator.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <set>
#include <vector>

namespace Lodestone {

	namespace {

		std::vector<uint64_t> Take(RandomGenerator& generator, size_t count)
		{
			std::vector<uint64_t> values;
			values.reserve(count);
			for (size_t i = 0; i < count; ++i)
				values.push_back(generator.Next());
			return values;
		}

	}

	TEST_CASE("A seed always gives the same sequence")
	{
		RandomGenerator first(1234);
		RandomGenerator second(1234);
		CHECK(Take(first, 100) == Take(second, 100));
	}

	TEST_CASE("A copied generator continues the same sequence")
	{
		RandomGenerator generator(99);
		Take(generator, 10);
		RandomGenerator copy = generator;
		CHECK(copy == generator);
		CHECK(Take(copy, 50) == Take(generator, 50));
	}

	TEST_CASE("Different seeds give different sequences, and values don't repeat")
	{
		RandomGenerator zero(0);
		RandomGenerator one(1);
		const std::vector<uint64_t> values = Take(zero, 1000);
		CHECK(values != Take(one, 1000));
		CHECK(std::set(values.begin(), values.end()).size() == values.size());
	}

	TEST_CASE("Generators seeded by the system differ")
	{
		RandomGenerator first;
		RandomGenerator second;
		CHECK(Take(first, 4) != Take(second, 4));
	}

}
