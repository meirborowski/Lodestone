#include "Lodestone/Physics/BodyIdAllocator.h"

#include "Lodestone/Core/Assert.h"

namespace Lodestone {

	BodyIdAllocator::BodyIdAllocator(uint32_t capacity)
		: m_Capacity(capacity)
	{
		LS_CORE_ASSERT(capacity > 0 && capacity <= MaxCapacity, "A body ID allocator holds 1 to {} IDs", MaxCapacity);
	}

	std::optional<uint32_t> BodyIdAllocator::Allocate()
	{
		uint32_t index = 0;
		if (!m_Free.empty())
		{
			index = *m_Free.begin();
			m_Free.erase(m_Free.begin());
		}
		else
		{
			if (m_Sequences.size() >= m_Capacity)
				return std::nullopt;
			index = static_cast<uint32_t>(m_Sequences.size());
			m_Sequences.push_back(0);
			m_Used.push_back(0);
		}
		m_Used[index] = 1;
		return index | (static_cast<uint32_t>(m_Sequences[index]) << SequenceShift);
	}

	void BodyIdAllocator::Free(uint32_t id)
	{
		LS_CORE_ASSERT(IsAllocated(id), "Body ID {:#x} isn't allocated", id);
		const uint32_t index = GetIndex(id);
		m_Used[index] = 0;
		// Wraps around after 256 reuses, as Jolt's sequence numbers do
		m_Sequences[index] = static_cast<uint8_t>(m_Sequences[index] + 1);
		m_Free.insert(index);
	}

	bool BodyIdAllocator::IsAllocated(uint32_t id) const
	{
		const uint32_t index = GetIndex(id);
		return index < m_Sequences.size() && m_Used[index] != 0 && m_Sequences[index] == GetSequence(id);
	}

	bool BodyIdAllocator::Restore(std::span<const uint8_t> sequences, std::span<const uint8_t> used)
	{
		if (sequences.size() != used.size() || sequences.size() > m_Capacity)
			return false;
		m_Sequences.assign(sequences.begin(), sequences.end());
		m_Used.assign(used.begin(), used.end());
		m_Free.clear();
		for (uint32_t index = 0; index < m_Used.size(); ++index)
			if (m_Used[index] == 0)
				m_Free.insert(m_Free.end(), index);
		return true;
	}

}
