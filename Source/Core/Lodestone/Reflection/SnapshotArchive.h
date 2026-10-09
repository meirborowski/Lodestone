#pragma once

#include "Lodestone/Core/Assert.h"

#include <any>
#include <cstddef>
#include <vector>

namespace Lodestone {

	// An in-memory archive for EnTT's snapshots: values are copied in as they're written, and copied out in the same
	// order when they're read. It holds simulation state for rollback, so it's never written to disk or sent over the
	// network - there's no encoding, and nothing to validate
	class SnapshotArchive
	{
	public:
		class Writer
		{
		public:
			explicit Writer(SnapshotArchive& archive)
				: m_Archive(&archive)
			{
			}

			template <typename T>
			void operator()(const T& value)
			{
				m_Archive->m_Values.emplace_back(value);
			}

		private:
			SnapshotArchive* m_Archive;
		};

		class Reader
		{
		public:
			explicit Reader(const SnapshotArchive& archive)
				: m_Archive(&archive)
			{
			}

			template <typename T>
			void operator()(T& value)
			{
				LS_CORE_ASSERT(m_Next < m_Archive->m_Values.size(), "Read past the end of a snapshot");
				const T* stored = std::any_cast<T>(&m_Archive->m_Values[m_Next]);
				LS_CORE_ASSERT(stored != nullptr, "A snapshot was read in a different order than it was written");
				value = *stored;
				++m_Next;
			}

			// Whether every value has been read
			bool IsAtEnd() const { return m_Next == m_Archive->m_Values.size(); }

		private:
			const SnapshotArchive* m_Archive;
			size_t m_Next = 0;
		};

		size_t GetValueCount() const { return m_Values.size(); }

	private:
		std::vector<std::any> m_Values;
	};

}
