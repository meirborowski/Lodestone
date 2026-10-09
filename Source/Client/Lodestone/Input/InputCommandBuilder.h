#pragma once

#include "Lodestone/Input/DeviceInput.h"
#include "Lodestone/Input/InputCommand.h"

#include <cstdint>
#include <optional>

namespace Lodestone {

	// Turns one player's device input into per-tick input commands. Frames and simulation ticks run at different
	// rates: call AccumulateFrame() after each frame's events are polled, and BuildCommand() for each tick the
	// simulation runs. Transitions and movement from every frame since the last command go into the next one, so a key
	// tapped between two ticks is never lost; when one frame runs several ticks, the first one gets them
	class InputCommandBuilder
	{
	public:
		// The player's gamepad is the one with this index, if any
		explicit InputCommandBuilder(uint32_t player = 0, std::optional<uint32_t> gamepad = 0);

		void AccumulateFrame(const DeviceInput& input);
		// The command for a tick, with everything accumulated since the previous command
		InputCommand BuildCommand(uint64_t tick);

	private:
		uint32_t m_Player;
		std::optional<uint32_t> m_Gamepad;
		InputCommand m_Pending;
	};

}
