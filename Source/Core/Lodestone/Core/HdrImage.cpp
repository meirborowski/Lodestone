#include "Lodestone/Core/HdrImage.h"

#include "Lodestone/Core/Assert.h"
#include "Lodestone/Core/FileSystem.h"

#include <spdlog/fmt/std.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace Lodestone {

	namespace {

		constexpr uint32_t ChannelCount = 4;
		// Radiance HDR files of an HDRI's size are tens of megabytes at most
		constexpr uint64_t MaxFileSize = 256ull * 1024 * 1024;

	}

	HdrImage::HdrImage(uint32_t width, uint32_t height)
		: m_Width(width), m_Height(height), m_Pixels(static_cast<size_t>(width) * height * ChannelCount)
	{
	}

	std::expected<HdrImage, Error> HdrImage::Load(const std::filesystem::path& path)
	{
		const auto bytes = ReadBinaryFile(path, MaxFileSize);
		if (!bytes)
			return std::unexpected(bytes.error().WithContext(fmt::format("Loading HDR image {}", path)));
		return Decode(*bytes).transform_error(
			[&path](const Error& error) { return error.WithContext(fmt::format("Loading HDR image {}", path)); });
	}

	std::expected<HdrImage, Error> HdrImage::Decode(std::span<const uint8_t> encoded)
	{
		if (encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
			return std::unexpected(Error(ErrorCode::Unsupported, "The encoded image is larger than 2 GB"));
		const auto size = static_cast<int>(encoded.size());
		if (stbi_is_hdr_from_memory(encoded.data(), size) == 0)
			return std::unexpected(Error(ErrorCode::ParseError, "The data isn't a Radiance HDR image"));

		// The size is checked before decoding, so a header that claims a huge image allocates nothing
		int width = 0;
		int height = 0;
		int channelsInFile = 0;
		if (stbi_info_from_memory(encoded.data(), size, &width, &height, &channelsInFile) == 0)
			return std::unexpected(Error(
				ErrorCode::ParseError, fmt::format("Can't read the HDR image's header: {}", stbi_failure_reason())));
		if (width <= 0 || height <= 0 || static_cast<uint64_t>(width) * static_cast<uint64_t>(height) > MaxPixelCount)
			return std::unexpected(Error(ErrorCode::Unsupported,
				fmt::format("A {}x{} HDR image is larger than the {} pixels supported", width, height, MaxPixelCount)));

		float* pixels = stbi_loadf_from_memory(
			encoded.data(), size, &width, &height, &channelsInFile, static_cast<int>(ChannelCount));
		if (pixels == nullptr)
			return std::unexpected(
				Error(ErrorCode::ParseError, fmt::format("Can't decode the HDR image: {}", stbi_failure_reason())));

		HdrImage image(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
		std::memcpy(image.m_Pixels.data(), pixels, image.m_Pixels.size() * sizeof(float));
		stbi_image_free(pixels);
		return image;
	}

	std::expected<std::vector<uint8_t>, Error> HdrImage::EncodeHdr() const
	{
		if (IsEmpty())
			return std::unexpected(Error(ErrorCode::InvalidArgument, "An empty image can't be encoded"));

		// Uncompressed RGBE: a shared exponent and three 8-bit mantissas per pixel. Every Radiance reader accepts it
		const std::string header =
			fmt::format("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {} +X {}\n", m_Height, m_Width);
		std::vector<uint8_t> encoded(header.begin(), header.end());
		encoded.reserve(encoded.size() + static_cast<size_t>(m_Width) * m_Height * 4);
		for (uint32_t y = 0; y < m_Height; ++y)
		{
			for (uint32_t x = 0; x < m_Width; ++x)
			{
				const glm::vec4 pixel = GetPixel(x, y);
				const float brightest = std::max({pixel.r, pixel.g, pixel.b});
				if (!(brightest > 1e-32f) || !std::isfinite(brightest))
				{
					encoded.insert(encoded.end(), {0, 0, 0, 0});
					continue;
				}
				int exponent = 0;
				const float mantissa = std::frexp(brightest, &exponent);
				const float scale = mantissa * 256.0f / brightest;
				const auto channel = [scale](float value)
				{ return static_cast<uint8_t>(std::clamp(value * scale, 0.0f, 255.0f)); };
				encoded.push_back(channel(pixel.r));
				encoded.push_back(channel(pixel.g));
				encoded.push_back(channel(pixel.b));
				encoded.push_back(static_cast<uint8_t>(std::clamp(exponent + 128, 0, 255)));
			}
		}
		return encoded;
	}

	glm::vec4 HdrImage::GetPixel(uint32_t x, uint32_t y) const
	{
		const size_t offset = GetPixelOffset(x, y);
		return {m_Pixels[offset], m_Pixels[offset + 1], m_Pixels[offset + 2], m_Pixels[offset + 3]};
	}

	void HdrImage::SetPixel(uint32_t x, uint32_t y, const glm::vec4& color)
	{
		const size_t offset = GetPixelOffset(x, y);
		m_Pixels[offset] = color.r;
		m_Pixels[offset + 1] = color.g;
		m_Pixels[offset + 2] = color.b;
		m_Pixels[offset + 3] = color.a;
	}

	size_t HdrImage::GetPixelOffset(uint32_t x, uint32_t y) const
	{
		LS_CORE_ASSERT(
			x < m_Width && y < m_Height, "Pixel ({}, {}) is outside the {}x{} image", x, y, m_Width, m_Height);
		return (static_cast<size_t>(y) * m_Width + x) * ChannelCount;
	}

}
