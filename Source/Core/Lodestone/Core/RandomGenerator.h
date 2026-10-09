#pragma once

#include <array>
#include <cstdint>

namespace Lodestone {

	// A small, fast pseudo-random generator (xoshiro256**) whose whole state is 32 bytes, so it can be copied into
	// snapshots and replayed. It's for determinism - the same seed gives the same sequence on every platform - not for
	// security
	class RandomGenerator
	{
	public:
		// Seeded from the operating system's entropy source
		RandomGenerator();
		explicit RandomGenerator(uint64_t seed);

		uint64_t Next();

		bool operator==(const RandomGenerator& other) const = default;

	private:
		std::array<uint64_t, 4> m_State{};
	};

}
