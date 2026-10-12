#include "Lodestone/Renderer/MipChain.h"

#include "Lodestone/Core/Assert.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <span>

namespace Lodestone {

	namespace {

		float SrgbToLinear(float value)
		{
			return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
		}

		float LinearToSrgb(float value)
		{
			return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
		}

		const std::array<float, 256>& GetSrgbToLinearTable()
		{
			static const std::array<float, 256> Table = []
			{
				std::array<float, 256> values{};
				for (size_t i = 0; i < values.size(); ++i)
					values[i] = SrgbToLinear(static_cast<float>(i) / 255.0f);
				return values;
			}();
			return Table;
		}

		uint8_t ToByte(float value)
		{
			return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
		}

		Image Downsample(const Image& source, bool srgb)
		{
			const std::array<float, 256>& toLinear = GetSrgbToLinearTable();
			const uint32_t sourceWidth = source.GetWidth();
			const uint32_t sourceHeight = source.GetHeight();
			const uint32_t width = std::max(sourceWidth / 2, 1u);
			const uint32_t height = std::max(sourceHeight / 2, 1u);
			Image result(width, height);
			// Pixels are read and written directly: this runs over every texel of every model's textures
			const std::span<const uint8_t> input = source.GetPixels();
			const std::span<uint8_t> output = result.GetPixels();
			for (uint32_t y = 0; y < height; ++y)
			{
				const std::array<uint32_t, 2> rows = {
					std::min(y * 2, sourceHeight - 1), std::min(y * 2 + 1, sourceHeight - 1)};
				for (uint32_t x = 0; x < width; ++x)
				{
					const std::array<uint32_t, 2> columns = {
						std::min(x * 2, sourceWidth - 1), std::min(x * 2 + 1, sourceWidth - 1)};
					std::array<float, 4> sum{};
					for (const uint32_t row : rows)
					{
						for (const uint32_t column : columns)
						{
							const size_t sourceOffset = (static_cast<size_t>(row) * sourceWidth + column) * 4;
							for (size_t c = 0; c < 3; ++c)
							{
								const uint8_t value = input[sourceOffset + c];
								sum[c] += srgb ? toLinear[value] : static_cast<float>(value) / 255.0f;
							}
							sum[3] += static_cast<float>(input[sourceOffset + 3]) / 255.0f;
						}
					}
					const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
					for (size_t c = 0; c < 4; ++c)
					{
						const float average = sum[c] * 0.25f;
						output[offset + c] = ToByte(srgb && c < 3 ? LinearToSrgb(average) : average);
					}
				}
			}
			return result;
		}

	}

	uint32_t GetMipCount(uint32_t width, uint32_t height)
	{
		return static_cast<uint32_t>(std::bit_width(std::max({width, height, 1u})));
	}

	std::vector<Image> GenerateMipChain(const Image& image, bool srgb)
	{
		LS_CORE_ASSERT(!image.IsEmpty(), "An empty image has no mips");
		const uint32_t count = GetMipCount(image.GetWidth(), image.GetHeight());
		std::vector<Image> levels;
		levels.reserve(count - 1);
		for (uint32_t level = 1; level < count; ++level)
			levels.push_back(Downsample(level == 1 ? image : levels.back(), srgb));
		return levels;
	}

}
