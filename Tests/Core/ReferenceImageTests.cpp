#include "Common/ReferenceImage.h"

#include <doctest/doctest.h>

namespace Lodestone {

	namespace {

		Image MakeSolidImage(uint32_t width, uint32_t height, Rgba8 color)
		{
			Image image(width, height);
			for (uint32_t y = 0; y < height; ++y)
			{
				for (uint32_t x = 0; x < width; ++x)
					image.SetPixel(x, y, color);
			}
			return image;
		}

	}

	TEST_CASE("Identical images have no difference")
	{
		const Image image = MakeSolidImage(4, 4, {.R = 10, .G = 20, .B = 30, .A = 255});

		const Testing::ImageDifference difference = Testing::CompareImages(image, image, 0);

		CHECK(difference.MismatchedPixels == 0);
		CHECK(difference.MaxChannelDifference == 0);
		CHECK(difference.Visualization.GetWidth() == 4);
	}

	TEST_CASE("Differences within the channel tolerance match")
	{
		const Image expected = MakeSolidImage(4, 4, {.R = 100, .G = 100, .B = 100, .A = 255});
		Image actual = expected;
		actual.SetPixel(1, 1, {.R = 102, .G = 99, .B = 100, .A = 255});

		const Testing::ImageDifference difference = Testing::CompareImages(actual, expected, 2);

		CHECK(difference.MismatchedPixels == 0);
		CHECK(difference.MaxChannelDifference == 2);
	}

	TEST_CASE("Pixels beyond the channel tolerance are counted and marked in red")
	{
		const Image expected = MakeSolidImage(4, 4, {.R = 100, .G = 100, .B = 100, .A = 255});
		Image actual = expected;
		actual.SetPixel(0, 0, {.R = 100, .G = 100, .B = 140, .A = 255});
		actual.SetPixel(3, 2, {.R = 0, .G = 100, .B = 100, .A = 255});

		const Testing::ImageDifference difference = Testing::CompareImages(actual, expected, 2);

		CHECK(difference.MismatchedPixels == 2);
		CHECK(difference.MaxChannelDifference == 100);
		CHECK(difference.Visualization.GetPixel(0, 0) == Rgba8{.R = 255, .G = 0, .B = 0, .A = 255});
		CHECK(difference.Visualization.GetPixel(3, 2) == Rgba8{.R = 255, .G = 0, .B = 0, .A = 255});
		CHECK(difference.Visualization.GetPixel(1, 1).R == 25);
	}

}
