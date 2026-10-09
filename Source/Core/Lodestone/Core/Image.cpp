#include "Lodestone/Core/Image.h"

#include "Lodestone/Core/Assert.h"

#include <spdlog/fmt/std.h>
#include <stb_image.h>
#include <stb_image_write.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

namespace Lodestone {

	namespace {

		constexpr uint32_t ChannelCount = 4;

		void AppendToVector(void* context, void* data, int size)
		{
			auto& bytes = *static_cast<std::vector<uint8_t>*>(context);
			const auto* begin = static_cast<const uint8_t*>(data);
			bytes.insert(bytes.end(), begin, begin + size);
		}

	}

	Image::Image(uint32_t width, uint32_t height)
		: m_Width(width), m_Height(height), m_Pixels(static_cast<size_t>(width) * height * ChannelCount)
	{
	}

	std::expected<Image, Error> Image::Load(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return std::unexpected(Error(ErrorCode::FileNotFound, fmt::format("Can't open image {}", path)));

		const std::vector<uint8_t> encoded{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		if (file.bad())
			return std::unexpected(Error(ErrorCode::IoError, fmt::format("Can't read image {}", path)));

		return Decode(encoded).transform_error(
			[&path](const Error& error) { return error.WithContext(fmt::format("Loading image {}", path)); });
	}

	std::expected<Image, Error> Image::Decode(std::span<const uint8_t> encoded)
	{
		if (encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
			return std::unexpected(Error(ErrorCode::Unsupported, "The encoded image is larger than 2 GB"));

		int width = 0;
		int height = 0;
		int channelsInFile = 0;
		stbi_uc* pixels = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height,
			&channelsInFile, static_cast<int>(ChannelCount));
		if (pixels == nullptr)
			return std::unexpected(
				Error(ErrorCode::ParseError, fmt::format("Can't decode the image: {}", stbi_failure_reason())));

		Image image(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
		std::memcpy(image.m_Pixels.data(), pixels, image.m_Pixels.size());
		stbi_image_free(pixels);
		return image;
	}

	std::expected<std::vector<uint8_t>, Error> Image::EncodePng() const
	{
		if (IsEmpty())
			return std::unexpected(Error(ErrorCode::InvalidArgument, "An empty image can't be encoded"));

		std::vector<uint8_t> encoded;
		const int strideInBytes = static_cast<int>(m_Width * ChannelCount);
		if (stbi_write_png_to_func(&AppendToVector, &encoded, static_cast<int>(m_Width), static_cast<int>(m_Height),
				static_cast<int>(ChannelCount), m_Pixels.data(), strideInBytes) == 0)
			return std::unexpected(Error(ErrorCode::IoError, "Encoding the image as PNG failed"));
		return encoded;
	}

	std::expected<void, Error> Image::SavePng(const std::filesystem::path& path) const
	{
		if (IsEmpty())
			return std::unexpected(
				Error(ErrorCode::InvalidArgument, fmt::format("Can't save the empty image {}", path)));

		const auto encoded = EncodePng();
		if (!encoded)
			return std::unexpected(encoded.error().WithContext(fmt::format("Saving image {}", path)));

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
			return std::unexpected(Error(ErrorCode::IoError, fmt::format("Can't create image file {}", path)));
		file.write(reinterpret_cast<const char*>(encoded->data()), static_cast<std::streamsize>(encoded->size()));
		file.close();
		if (!file)
			return std::unexpected(Error(ErrorCode::IoError, fmt::format("Can't write image file {}", path)));
		return {};
	}

	Rgba8 Image::GetPixel(uint32_t x, uint32_t y) const
	{
		const size_t offset = GetPixelOffset(x, y);
		return {.R = m_Pixels[offset], .G = m_Pixels[offset + 1], .B = m_Pixels[offset + 2], .A = m_Pixels[offset + 3]};
	}

	void Image::SetPixel(uint32_t x, uint32_t y, Rgba8 color)
	{
		const size_t offset = GetPixelOffset(x, y);
		m_Pixels[offset] = color.R;
		m_Pixels[offset + 1] = color.G;
		m_Pixels[offset + 2] = color.B;
		m_Pixels[offset + 3] = color.A;
	}

	size_t Image::GetPixelOffset(uint32_t x, uint32_t y) const
	{
		LS_CORE_ASSERT(
			x < m_Width && y < m_Height, "Pixel ({}, {}) is outside the {}x{} image", x, y, m_Width, m_Height);
		return (static_cast<size_t>(y) * m_Width + x) * ChannelCount;
	}

}
