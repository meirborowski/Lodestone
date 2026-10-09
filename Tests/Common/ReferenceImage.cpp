#include "Common/ReferenceImage.h"

#include "Lodestone/Core/Environment.h"

#include <doctest/doctest.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

// Set by Tests/CMakeLists.txt
#if !defined(LS_REFERENCE_IMAGE_DIR) || !defined(LS_RENDER_OUTPUT_DIR)
	#error "LS_REFERENCE_IMAGE_DIR and LS_RENDER_OUTPUT_DIR must be defined"
#endif

namespace Lodestone::Testing {

	namespace {

		uint8_t ChannelDifference(uint8_t a, uint8_t b)
		{
			return static_cast<uint8_t>(a > b ? a - b : b - a);
		}

		bool IsUpdateRequested()
		{
			const auto value = ReadEnvironmentVariable("LS_UPDATE_REFERENCE_IMAGES");
			return value.has_value() && *value == "1";
		}

		std::filesystem::path GetOutputPath(std::string_view name, std::string_view suffix)
		{
			return std::filesystem::path(LS_RENDER_OUTPUT_DIR) / fmt::format("{}.{}.png", name, suffix);
		}

		void SaveForInspection(const Image& image, const std::filesystem::path& path)
		{
			std::error_code error;
			std::filesystem::create_directories(path.parent_path(), error);
			if (const auto saved = image.SavePng(path); saved)
				MESSAGE(fmt::format("Wrote {}", path));
			else
				MESSAGE(fmt::format("Couldn't write {}: {}", path, saved.error()));
		}

	}

	ImageDifference CompareImages(const Image& actual, const Image& expected, uint8_t channelTolerance)
	{
		ImageDifference difference;
		difference.Visualization = Image(expected.GetWidth(), expected.GetHeight());
		for (uint32_t y = 0; y < expected.GetHeight(); ++y)
		{
			for (uint32_t x = 0; x < expected.GetWidth(); ++x)
			{
				const Rgba8 a = actual.GetPixel(x, y);
				const Rgba8 b = expected.GetPixel(x, y);
				const uint8_t pixelDifference = std::max({ChannelDifference(a.R, b.R), ChannelDifference(a.G, b.G),
					ChannelDifference(a.B, b.B), ChannelDifference(a.A, b.A)});
				difference.MaxChannelDifference = std::max(difference.MaxChannelDifference, pixelDifference);

				if (pixelDifference > channelTolerance)
				{
					++difference.MismatchedPixels;
					difference.Visualization.SetPixel(x, y, {.R = 255, .G = 0, .B = 0, .A = 255});
				}
				else
				{
					const auto grey = static_cast<uint8_t>((b.R + b.G + b.B) / 12);
					difference.Visualization.SetPixel(x, y, {.R = grey, .G = grey, .B = grey, .A = 255});
				}
			}
		}
		return difference;
	}

	void CheckReferenceImage(std::string_view name, const Image& actual, const ImageTolerance& tolerance)
	{
		const std::filesystem::path referencePath =
			std::filesystem::path(LS_REFERENCE_IMAGE_DIR) / fmt::format("{}.png", name);

		if (IsUpdateRequested())
		{
			const auto saved = actual.SavePng(referencePath);
			REQUIRE_MESSAGE(saved.has_value(), fmt::format("Couldn't update {}", referencePath));
			MESSAGE(fmt::format("Updated reference image {}", referencePath));
			return;
		}

		const auto expected = Image::Load(referencePath);
		if (!expected)
		{
			SaveForInspection(actual, GetOutputPath(name, "actual"));
			FAIL(fmt::format(
				"Can't load reference image {}: {}. Create it by running the test with LS_UPDATE_REFERENCE_IMAGES=1",
				referencePath, expected.error()));
		}

		if (actual.GetWidth() != expected->GetWidth() || actual.GetHeight() != expected->GetHeight())
		{
			SaveForInspection(actual, GetOutputPath(name, "actual"));
			FAIL(fmt::format("Rendered {}x{}, but the reference image {} is {}x{}", actual.GetWidth(),
				actual.GetHeight(), referencePath, expected->GetWidth(), expected->GetHeight()));
		}

		const ImageDifference difference = CompareImages(actual, *expected, tolerance.ChannelTolerance);
		const auto pixelCount = static_cast<double>(actual.GetWidth()) * actual.GetHeight();
		const double mismatchedFraction = static_cast<double>(difference.MismatchedPixels) / pixelCount;
		INFO(fmt::format("{} pixels ({:.4f}%) differ by more than {}; the largest channel difference is {}",
			difference.MismatchedPixels, mismatchedFraction * 100.0, tolerance.ChannelTolerance,
			difference.MaxChannelDifference));
		if (mismatchedFraction > tolerance.MaxMismatchedFraction)
		{
			SaveForInspection(actual, GetOutputPath(name, "actual"));
			SaveForInspection(difference.Visualization, GetOutputPath(name, "difference"));
			FAIL(fmt::format("The rendered image doesn't match {}", referencePath));
		}
	}

}
