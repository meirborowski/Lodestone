#include "Lodestone/Physics/BodyIdAllocator.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

namespace Lodestone {

	TEST_CASE("Body IDs are the lowest free index, with a new sequence number each time an index is reused")
	{
		BodyIdAllocator allocator(8);
		const uint32_t first = allocator.Allocate().value_or(~0u);
		const uint32_t second = allocator.Allocate().value_or(~0u);
		const uint32_t third = allocator.Allocate().value_or(~0u);
		CHECK(first == 0);
		CHECK(second == 1);
		CHECK(third == 2);
		CHECK(allocator.IsAllocated(second));

		allocator.Free(second);
		CHECK_FALSE(allocator.IsAllocated(second));
		const uint32_t reused = allocator.Allocate().value_or(~0u);
		CHECK(BodyIdAllocator::GetIndex(reused) == 1);
		CHECK(BodyIdAllocator::GetSequence(reused) == 1);
		CHECK(reused != second);
		// The old ID no longer names the body that has its index now
		CHECK_FALSE(allocator.IsAllocated(second));
		CHECK(allocator.IsAllocated(reused));

		// The lowest free index comes first, whatever order indices were freed in
		allocator.Free(third);
		allocator.Free(first);
		CHECK(BodyIdAllocator::GetIndex(allocator.Allocate().value_or(~0u)) == 0);
		CHECK(BodyIdAllocator::GetIndex(allocator.Allocate().value_or(~0u)) == 2);
		CHECK(BodyIdAllocator::GetIndex(allocator.Allocate().value_or(~0u)) == 3);
	}

	TEST_CASE("Sequence numbers wrap around after 256 reuses, like Jolt's")
	{
		BodyIdAllocator allocator(1);
		uint32_t id = allocator.Allocate().value_or(~0u);
		for (int reuse = 0; reuse < 256; ++reuse)
		{
			allocator.Free(id);
			id = allocator.Allocate().value_or(~0u);
		}
		CHECK(id == 0);
		// Jolt reserves the top bit of a BodyID
		CHECK((id & 0x80000000u) == 0);
	}

	TEST_CASE("A body ID allocator runs out at its capacity")
	{
		BodyIdAllocator allocator(2);
		CHECK(allocator.Allocate().has_value());
		CHECK(allocator.Allocate().has_value());
		CHECK_FALSE(allocator.Allocate().has_value());
	}

	TEST_CASE("A restored body ID allocator hands out the IDs the original did")
	{
		BodyIdAllocator original(16);
		std::vector<uint32_t> ids;
		ids.reserve(5);
		for (int i = 0; i < 5; ++i)
			ids.push_back(original.Allocate().value_or(~0u));
		original.Free(ids[3]);
		original.Free(ids[1]);
		const std::vector<uint8_t> sequences(original.GetSequences().begin(), original.GetSequences().end());
		const std::vector<uint8_t> used(original.GetUsed().begin(), original.GetUsed().end());

		BodyIdAllocator restored(16);
		CHECK(restored.Allocate().has_value());
		REQUIRE(restored.Restore(sequences, used));
		for (int i = 0; i < 4; ++i)
			CHECK(restored.Allocate() == original.Allocate());
		CHECK(restored.IsAllocated(ids[0]));
		CHECK_FALSE(restored.IsAllocated(ids[1]));
	}

	TEST_CASE("Restoring a body ID allocator from mismatched state changes nothing")
	{
		BodyIdAllocator allocator(4);
		const uint32_t id = allocator.Allocate().value_or(~0u);
		const std::vector<uint8_t> twoSequences{0, 0};
		const std::vector<uint8_t> oneUsed{1};
		CHECK_FALSE(allocator.Restore(twoSequences, oneUsed));
		const std::vector<uint8_t> tooMany(5, 0);
		CHECK_FALSE(allocator.Restore(tooMany, tooMany));
		CHECK(allocator.IsAllocated(id));
		CHECK(allocator.Allocate() == 1u);
	}

}
