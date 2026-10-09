#pragma once

#include "Lodestone/Graphics/GraphicsDevice.h"

namespace Lodestone::Testing {

	// The device configuration rendering tests use: lavapipe at the pinned Mesa version wherever it's available (see
	// cmake/Lavapipe.cmake), so results don't depend on the machine's GPU; otherwise the installed drivers - MoltenVK
	// on macOS
	GraphicsDeviceConfig MakeRenderTestDeviceConfig();

}
