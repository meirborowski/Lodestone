#pragma once

#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/Image.h"

#include <nvrhi/nvrhi.h>

#include <expected>

namespace Lodestone {

	// Copies the first mip level of an 8-bit RGBA or BGRA texture back to the CPU. Waits for the GPU to finish all
	// submitted work first, so it's for screenshots and tests, not for every frame
	[[nodiscard]] std::expected<Image, Error> ReadTexture(nvrhi::IDevice* device, nvrhi::ITexture* texture);

}
