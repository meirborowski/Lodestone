#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace Lodestone {

	// Hands out physics body IDs deterministically: the lowest free index, with a sequence number that changes each
	// time the index is reused, laid out as Jolt's BodyID (the index in the low 23 bits, the sequence number in the 8
	// above). The physics world saves its state with the simulation's, so re-simulating after a restore hands out the
	// IDs the first run did (see docs/Decisions/0021-physics.md)
	class BodyIdAllocator
	{
	public:
		static constexpr uint32_t MaxCapacity = 1u << 23;

		// Indices from 0 to capacity - 1, at most MaxCapacity
		explicit BodyIdAllocator(uint32_t capacity);

		// An unused ID, or nothing when every index is in use
		[[nodiscard]] std::optional<uint32_t> Allocate();
		// Returns an ID that Allocate() handed out
		void Free(uint32_t id);
		bool IsAllocated(uint32_t id) const;

		static uint32_t GetIndex(uint32_t id) { return id & IndexMask; }
		static uint32_t GetSequence(uint32_t id) { return (id >> SequenceShift) & SequenceMask; }

		// The next sequence number of every index handed out so far, and whether each is in use: all of the state
		std::span<const uint8_t> GetSequences() const { return m_Sequences; }
		std::span<const uint8_t> GetUsed() const { return m_Used; }
		// Replaces the state with one from GetSequences() and GetUsed(), which must be the same length, within the
		// capacity. Returns false, changing nothing, if they aren't
		[[nodiscard]] bool Restore(std::span<const uint8_t> sequences, std::span<const uint8_t> used);

	private:
		static constexpr uint32_t IndexMask = MaxCapacity - 1;
		static constexpr uint32_t SequenceShift = 23;
		static constexpr uint32_t SequenceMask = 0xff;

	private:
		uint32_t m_Capacity;
		std::vector<uint8_t> m_Sequences;
		std::vector<uint8_t> m_Used;
		// Indices below m_Sequences.size() that aren't in use, lowest first
		std::set<uint32_t> m_Free;
	};

}
