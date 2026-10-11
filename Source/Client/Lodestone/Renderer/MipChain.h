#pragma once

#include "Lodestone/Core/Image.h"

#include <cstdint>
#include <vector>

namespace Lodestone {

	// How many mip levels a texture of a size has, down to 1x1
	uint32_t GetMipCount(uint32_t width, uint32_t height);

	// The mip levels below an image, largest first: each half the size of the one above (rounded down, at least one
	// pixel), each pixel the average of the 2x2 block it covers. sRGB images are averaged as linear light, so they
	// don't darken as they shrink; alpha is always linear
	std::vector<Image> GenerateMipChain(const Image& image, bool srgb);

}
