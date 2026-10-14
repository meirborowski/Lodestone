#pragma once

// Internal to the physics module (see JoltRuntime.h)

#include "Lodestone/Physics/JoltRuntime.h"

#include <Jolt/Physics/StateRecorder.h>

#include <cstddef>
#include <span>
#include <vector>

namespace Lodestone {

	// A Jolt state recorder over bytes in memory: one that writes appends to a buffer, and one that reads consumes a
	// span of bytes. Reading past the end fails the recorder and reads zeros
	class StateBuffer final : public JPH::StateRecorder
	{
	public:
		explicit StateBuffer(std::vector<std::byte>& output)
			: m_Output(&output)
		{
		}

		explicit StateBuffer(std::span<const std::byte> input)
			: m_Input(input)
		{
		}

		void WriteBytes(const void* data, size_t size) override;
		void ReadBytes(void* data, size_t size) override;
		bool IsEOF() const override { return m_Position >= m_Input.size(); }
		bool IsFailed() const override { return m_Failed; }

	private:
		std::vector<std::byte>* m_Output = nullptr;
		std::span<const std::byte> m_Input;
		size_t m_Position = 0;
		bool m_Failed = false;
	};

}
