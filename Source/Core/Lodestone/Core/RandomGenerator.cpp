#include "Lodestone/Core/RandomGenerator.h"

#include "Lodestone/Core/UUID.h"

#include <bit>

namespace Lodestone {

	namespace {

		// SplitMix64, which spreads a seed over xoshiro's state as its authors recommend, so similar seeds give
		// unrelated sequences and the state is never all zero
		uint64_t SplitMix64(uint64_t& state)
		{
			uint64_t value = (state += 0x9E37'79B9'7F4A'7C15ull);
			value = (value ^ (value >> 30)) * 0xBF58'476D'1CE4'E5B9ull;
			value = (value ^ (value >> 27)) * 0x94D0'49BB'1331'11EBull;
			return value ^ (value >> 31);
		}

		uint64_t MakeRandomSeed()
		{
			const UUID random = UUID::Generate();
			return random.GetHigh() ^ std::rotl(random.GetLow(), 29);
		}

	}

	RandomGenerator::RandomGenerator()
		: RandomGenerator(MakeRandomSeed())
	{
	}

	RandomGenerator::RandomGenerator(uint64_t seed)
	{
		for (uint64_t& word : m_State)
			word = SplitMix64(seed);
	}

	uint64_t RandomGenerator::Next()
	{
		const uint64_t result = std::rotl(m_State[1] * 5, 7) * 9;
		const uint64_t shifted = m_State[1] << 17;
		m_State[2] ^= m_State[0];
		m_State[3] ^= m_State[1];
		m_State[1] ^= m_State[2];
		m_State[0] ^= m_State[3];
		m_State[2] ^= shifted;
		m_State[3] = std::rotl(m_State[3], 45);
		return result;
	}

}
