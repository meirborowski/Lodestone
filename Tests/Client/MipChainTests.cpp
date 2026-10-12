#include "Lodestone/Renderer/MipChain.h"

#include <doctest/doctest.h>

namespace Lodestone {

	namespace {

		// A 2x2 checkerboard of black and white, with half-transparent white
		Image MakeChecker()
		{
			Image image(2, 2);
			image.SetPixel(0, 0, Rgba8{.R = 255, .G = 255, .B = 255, .A = 255});
			image.SetPixel(1, 0, Rgba8{.R = 0, .G = 0, .B = 0, .A = 255});
			image.SetPixel(0, 1, Rgba8{.R = 0, .G = 0, .B = 0, .A = 255});
			image.SetPixel(1, 1, Rgba8{.R = 255, .G = 255, .B = 255, .A = 0});
			return image;
		}

	}

	TEST_CASE("Mip counts go down to a single pixel along the longer side")
	{
		CHECK(GetMipCount(1, 1) == 1);
		CHECK(GetMipCount(2, 1) == 2);
		CHECK(GetMipCount(256, 256) == 9);
		CHECK(GetMipCount(256, 64) == 9);
		CHECK(GetMipCount(5, 3) == 3);
		CHECK(GetMipCount(0, 0) == 1);
	}

	TEST_CASE("Each mip level is half the size of the one above, down to one pixel")
	{
		const std::vector<Image> levels = GenerateMipChain(Image(8, 2), false);

		REQUIRE(levels.size() == 3);
		CHECK(levels[0].GetWidth() == 4);
		CHECK(levels[0].GetHeight() == 1);
		CHECK(levels[1].GetWidth() == 2);
		CHECK(levels[2].GetWidth() == 1);
		CHECK(levels[2].GetHeight() == 1);
		CHECK(GenerateMipChain(Image(1, 1), true).empty());
	}

	TEST_CASE("Linear images average their values; sRGB images average the light they encode")
	{
		const std::vector<Image> linear = GenerateMipChain(MakeChecker(), false);
		const std::vector<Image> srgb = GenerateMipChain(MakeChecker(), true);
		REQUIRE(linear.size() == 1);
		REQUIRE(srgb.size() == 1);

		// Half of full white
		CHECK(linear[0].GetPixel(0, 0).R == 128);
		// Half of white's light, encoded as sRGB: 0.5 linear is 0.735, or 188
		CHECK(srgb[0].GetPixel(0, 0).R == 188);
		CHECK(srgb[0].GetPixel(0, 0).G == 188);
		// Alpha is linear either way
		CHECK(linear[0].GetPixel(0, 0).A == 191);
		CHECK(srgb[0].GetPixel(0, 0).A == 191);
	}

	TEST_CASE("Images with an odd size shrink without reading past their edges")
	{
		Image image(3, 1);
		for (uint32_t x = 0; x < 3; ++x)
			image.SetPixel(x, 0, Rgba8{.R = 90, .G = 90, .B = 90, .A = 255});

		const std::vector<Image> levels = GenerateMipChain(image, true);

		REQUIRE(levels.size() == 1);
		CHECK(levels[0].GetWidth() == 1);
		CHECK(levels[0].GetPixel(0, 0) == Rgba8{.R = 90, .G = 90, .B = 90, .A = 255});
	}

}
