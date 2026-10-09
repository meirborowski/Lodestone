#include "Lodestone/Core/Image.h"

#include "Common/TemporaryDirectory.h"

#include <doctest/doctest.h>

#include <array>
#include <fstream>

namespace Lodestone {

	TEST_CASE("A new image has the requested size and transparent black pixels")
	{
		const Image image(3, 2);

		CHECK(image.GetWidth() == 3);
		CHECK(image.GetHeight() == 2);
		CHECK_FALSE(image.IsEmpty());
		CHECK(image.GetPixels().size() == 3 * 2 * 4);
		CHECK(image.GetPixel(2, 1) == Rgba8{});
		CHECK(Image().IsEmpty());
	}

	TEST_CASE("Image pixels are stored row by row from the top-left corner")
	{
		Image image(2, 2);
		image.SetPixel(1, 0, {.R = 10, .G = 20, .B = 30, .A = 40});
		image.SetPixel(0, 1, {.R = 50, .G = 60, .B = 70, .A = 80});

		CHECK(image.GetPixel(1, 0) == Rgba8{.R = 10, .G = 20, .B = 30, .A = 40});
		CHECK(image.GetPixel(0, 1) == Rgba8{.R = 50, .G = 60, .B = 70, .A = 80});
		const auto pixels = image.GetPixels();
		CHECK(pixels[4] == 10);
		CHECK(pixels[8] == 50);
	}

	TEST_CASE("An image saved as PNG loads back unchanged")
	{
		const Testing::TemporaryDirectory directory("ImagePng");
		Image image(4, 3);
		for (uint32_t y = 0; y < 3; ++y)
		{
			for (uint32_t x = 0; x < 4; ++x)
				image.SetPixel(
					x, y, {.R = static_cast<uint8_t>(x * 60), .G = static_cast<uint8_t>(y * 80), .B = 7, .A = 255});
		}
		const std::filesystem::path path = directory.GetPath() / "Gradient.png";

		REQUIRE(image.SavePng(path).has_value());
		const auto loaded = Image::Load(path);

		REQUIRE(loaded.has_value());
		CHECK(*loaded == image);
	}

	TEST_CASE("Loading a missing image fails with FileNotFound")
	{
		const Testing::TemporaryDirectory directory("ImageMissing");

		const auto loaded = Image::Load(directory.GetPath() / "Missing.png");

		REQUIRE_FALSE(loaded.has_value());
		CHECK(loaded.error().GetCode() == ErrorCode::FileNotFound);
	}

	TEST_CASE("Decoding data that isn't an image fails with ParseError")
	{
		const std::array<uint8_t, 8> garbage = {1, 2, 3, 4, 5, 6, 7, 8};

		const auto decoded = Image::Decode(garbage);

		REQUIRE_FALSE(decoded.has_value());
		CHECK(decoded.error().GetCode() == ErrorCode::ParseError);
	}

	TEST_CASE("Saving an empty image fails")
	{
		const Testing::TemporaryDirectory directory("ImageEmpty");

		const auto saved = Image().SavePng(directory.GetPath() / "Empty.png");

		REQUIRE_FALSE(saved.has_value());
		CHECK(saved.error().GetCode() == ErrorCode::InvalidArgument);
	}

}
