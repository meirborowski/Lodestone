#pragma once

#include "Lodestone/Core/Error.h"

#include <glm/vec4.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

namespace Lodestone {

	// A high dynamic range image in CPU memory: linear RGBA pixels as 32-bit floats, row by row from the top-left
	// corner. Environment maps (HDRIs) are loaded into these
	class HdrImage
	{
	public:
		// The most pixels an image may have, so a hostile file can't exhaust memory: 8192x4096, as an HDRI
		static constexpr uint64_t MaxPixelCount = 8192ull * 4096ull;

		HdrImage() = default;
		// An image of the given size with every pixel zero
		HdrImage(uint32_t width, uint32_t height);

		// Decodes a Radiance HDR (.hdr) file
		[[nodiscard]] static std::expected<HdrImage, Error> Load(const std::filesystem::path& path);
		[[nodiscard]] static std::expected<HdrImage, Error> Decode(std::span<const uint8_t> encoded);
		// The image as the bytes of a Radiance HDR file, uncompressed
		[[nodiscard]] std::expected<std::vector<uint8_t>, Error> EncodeHdr() const;

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		bool IsEmpty() const { return m_Width == 0 || m_Height == 0; }

		// Four floats per pixel
		std::span<const float> GetPixels() const { return m_Pixels; }
		std::span<float> GetPixels() { return m_Pixels; }

		glm::vec4 GetPixel(uint32_t x, uint32_t y) const;
		void SetPixel(uint32_t x, uint32_t y, const glm::vec4& color);

		bool operator==(const HdrImage& other) const = default;

	private:
		size_t GetPixelOffset(uint32_t x, uint32_t y) const;

	private:
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		std::vector<float> m_Pixels;
	};

}
