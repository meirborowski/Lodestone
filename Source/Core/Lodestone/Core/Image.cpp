#include "Lodestone/Core/Image.h"

#include "Lodestone/Core/Assert.h"
#include "Lodestone/Core/FileSystem.h"

#include <spdlog/fmt/std.h>
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <vector>

namespace Lodestone {

	namespace {

		constexpr uint32_t ChannelCount = 4;

		constexpr std::array<uint8_t, 8> PngSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
		// Every PNG chunk has a 4-byte length, a 4-byte type, its data and a 4-byte CRC
		constexpr size_t PngChunkOverhead = 12;

		uint32_t ReadBigEndian32(const uint8_t* bytes)
		{
			return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
				(static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
		}

		// The PNG without its empty IDAT chunks, if it has any. They hold no image data, but stb_image copies each IDAT
		// chunk into a buffer it only allocates for a non-empty one, so an empty one before that is copied from a null
		// pointer - undefined behavior. Data that isn't a well-formed run of chunks is left for stb_image to refuse
		std::optional<std::vector<uint8_t>> RemoveEmptyPngDataChunks(std::span<const uint8_t> encoded)
		{
			if (encoded.size() < PngSignature.size() ||
				!std::ranges::equal(encoded.first(PngSignature.size()), PngSignature))
				return std::nullopt;

			std::vector<size_t> emptyChunks;
			size_t offset = PngSignature.size();
			while (encoded.size() - offset >= PngChunkOverhead)
			{
				const uint32_t length = ReadBigEndian32(&encoded[offset]);
				if (length > encoded.size() - offset - PngChunkOverhead)
					break;
				if (length == 0 && std::memcmp(&encoded[offset + 4], "IDAT", 4) == 0)
					emptyChunks.push_back(offset);
				offset += PngChunkOverhead + length;
			}
			if (emptyChunks.empty())
				return std::nullopt;

			std::vector<uint8_t> cleaned;
			cleaned.reserve(encoded.size() - emptyChunks.size() * PngChunkOverhead);
			size_t copied = 0;
			for (const size_t chunk : emptyChunks)
			{
				cleaned.insert(cleaned.end(), encoded.begin() + static_cast<ptrdiff_t>(copied),
					encoded.begin() + static_cast<ptrdiff_t>(chunk));
				copied = chunk + PngChunkOverhead;
			}
			cleaned.insert(cleaned.end(), encoded.begin() + static_cast<ptrdiff_t>(copied), encoded.end());
			return cleaned;
		}

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
		const auto encoded = ReadBinaryFile(path);
		if (!encoded)
			return std::unexpected(encoded.error().WithContext(fmt::format("Loading image {}", path)));
		return Decode(*encoded).transform_error(
			[&path](const Error& error) { return error.WithContext(fmt::format("Loading image {}", path)); });
	}

	std::expected<Image, Error> Image::Decode(std::span<const uint8_t> encoded)
	{
		if (encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
			return std::unexpected(Error(ErrorCode::Unsupported, "The encoded image is larger than 2 GB"));
		const std::optional<std::vector<uint8_t>> cleaned = RemoveEmptyPngDataChunks(encoded);
		if (cleaned)
			encoded = *cleaned;

		// The size is checked before decoding, so a header that claims a huge image allocates nothing
		int width = 0;
		int height = 0;
		int channelsInFile = 0;
		const auto size = static_cast<int>(encoded.size());
		if (stbi_info_from_memory(encoded.data(), size, &width, &height, &channelsInFile) == 0)
			return std::unexpected(
				Error(ErrorCode::ParseError, fmt::format("Can't decode the image: {}", stbi_failure_reason())));
		if (width <= 0 || height <= 0 || static_cast<uint64_t>(width) * static_cast<uint64_t>(height) > MaxPixelCount)
			return std::unexpected(Error(ErrorCode::Unsupported,
				fmt::format("A {}x{} image is larger than the {} pixels supported", width, height, MaxPixelCount)));
		stbi_uc* pixels = stbi_load_from_memory(
			encoded.data(), size, &width, &height, &channelsInFile, static_cast<int>(ChannelCount));
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
