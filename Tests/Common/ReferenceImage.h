#pragma once

#include "Lodestone/Core/Image.h"

#include <cstdint>
#include <string_view>

namespace Lodestone::Testing {

	struct ImageTolerance
	{
		// The largest difference in any channel (0-255) at which two pixels still match
		uint8_t ChannelTolerance = 2;
		// The share of pixels that may mismatch, for differences in rasterization at triangle edges
		double MaxMismatchedFraction = 0.001;
	};

	struct ImageDifference
	{
		uint64_t MismatchedPixels = 0;
		uint8_t MaxChannelDifference = 0;
		// The expected image, dimmed to grey, with mismatched pixels in red
		Image Visualization;
	};

	// Compares two images of the same size, pixel by pixel
	ImageDifference CompareImages(const Image& actual, const Image& expected, uint8_t channelTolerance);

	// Checks a rendered image against its reference image, Tests/ReferenceImages/<name>.png, failing the current
	// test if they differ by more than the tolerance. On failure, the rendered image and a difference image are
	// written to the render output directory (<build>/RenderOutput), which CI uploads.
	//
	// With LS_UPDATE_REFERENCE_IMAGES=1 in the environment, the reference image is replaced with the rendered one
	// instead. Only do that deliberately, with the reason in the commit message (see docs/Testing.md#reference-images)
	void CheckReferenceImage(std::string_view name, const Image& actual, const ImageTolerance& tolerance = {});

}
