#include "Lodestone/Core/HdrImage.h"

#include "Common/DescribeError.h"
#include "Common/TemporaryDirectory.h"

#include <doctest/doctest.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vector_relational.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace Lodestone {

	TEST_CASE("An HDR environment map loads with values above 1")
	{
		const auto image = HdrImage::Load(std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Environments" / "Sky.hdr");

		REQUIRE_MESSAGE(image.has_value(), Testing::DescribeError(image));
		CHECK(image->GetWidth() == 128);
		CHECK(image->GetHeight() == 64);
		// The sun is far brighter than anything else; the ground is dim
		float brightest = 0.0f;
		for (uint32_t y = 0; y < image->GetHeight(); ++y)
		{
			for (uint32_t x = 0; x < image->GetWidth(); ++x)
				brightest = std::max(brightest, image->GetPixel(x, y).r);
		}
		CHECK(brightest > 40.0f);
		CHECK(image->GetPixel(64, 60).r < 0.3f);
		CHECK(image->GetPixel(64, 0).a == 1.0f);
	}

	TEST_CASE("An HDR image encodes and decodes within the format's precision")
	{
		HdrImage image(5, 3);
		for (uint32_t y = 0; y < 3; ++y)
		{
			for (uint32_t x = 0; x < 5; ++x)
				image.SetPixel(
					x, y, glm::vec4(0.25f * static_cast<float>(x), 10.0f * static_cast<float>(y), 0.5f, 1.0f));
		}

		const auto encoded = image.EncodeHdr();
		REQUIRE(encoded.has_value());
		const auto decoded = HdrImage::Decode(*encoded);

		REQUIRE_MESSAGE(decoded.has_value(), Testing::DescribeError(decoded));
		REQUIRE(decoded->GetWidth() == 5);
		REQUIRE(decoded->GetHeight() == 3);
		for (uint32_t y = 0; y < 3; ++y)
		{
			for (uint32_t x = 0; x < 5; ++x)
			{
				const glm::vec4 expected = image.GetPixel(x, y);
				const glm::vec4 actual = decoded->GetPixel(x, y);
				CAPTURE(x);
				CAPTURE(y);
				// RGBE shares an exponent, so each channel keeps 8 bits relative to the brightest
				const float tolerance = std::max({expected.r, expected.g, expected.b}) / 128.0f + 1e-6f;
				CHECK(glm::all(
					glm::lessThanEqual(glm::abs(glm::vec3(actual) - glm::vec3(expected)), glm::vec3(tolerance))));
			}
		}
	}

	TEST_CASE("Data that isn't an HDR image fails to decode")
	{
		const std::string png = "\x89PNG\r\n\x1a\nnot really";
		const std::vector<uint8_t> bytes(png.begin(), png.end());

		const auto decoded = HdrImage::Decode(bytes);

		REQUIRE_FALSE(decoded.has_value());
		CHECK(decoded.error().GetCode() == ErrorCode::ParseError);
		CHECK_FALSE(HdrImage::Decode({}).has_value());
	}

	TEST_CASE("An HDR image larger than supported is refused before it's decoded")
	{
		const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 9000 +X 9000\n";
		const std::vector<uint8_t> bytes(header.begin(), header.end());

		const auto decoded = HdrImage::Decode(bytes);

		REQUIRE_FALSE(decoded.has_value());
		CHECK(decoded.error().GetCode() == ErrorCode::Unsupported);
	}

	TEST_CASE("Loading a missing HDR image fails with FileNotFound")
	{
		const Testing::TemporaryDirectory directory("HdrMissing");
		const auto loaded = HdrImage::Load(directory.GetPath() / "Missing.hdr");
		REQUIRE_FALSE(loaded.has_value());
		CHECK(loaded.error().GetCode() == ErrorCode::FileNotFound);
	}

}
