#include "Lodestone/Physics/StateBuffer.h"

#include "Lodestone/Core/Assert.h"

#include <cstring>

namespace Lodestone {

	void StateBuffer::WriteBytes(const void* data, size_t size)
	{
		LS_CORE_ASSERT(m_Output != nullptr, "The state buffer is for reading");
		if (size == 0)
			return;
		const auto* bytes = static_cast<const std::byte*>(data);
		m_Output->insert(m_Output->end(), bytes, bytes + size);
	}

	void StateBuffer::ReadBytes(void* data, size_t size)
	{
		LS_CORE_ASSERT(m_Output == nullptr, "The state buffer is for writing");
		// memcpy() with an empty span's null pointer is undefined, even for no bytes
		if (size == 0)
			return;
		if (m_Failed || size > m_Input.size() - m_Position)
		{
			m_Failed = true;
			std::memset(data, 0, size);
			return;
		}
		std::memcpy(data, m_Input.data() + m_Position, size);
		m_Position += size;
	}

}
