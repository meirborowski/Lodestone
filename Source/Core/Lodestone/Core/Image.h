#pragma once

#include "Lodestone/Core/Error.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

namespace Lodestone {

	// One pixel of an Image
	struct Rgba8
	{
		uint8_t R = 0;
		uint8_t G = 0;
		uint8_t B = 0;
		uint8_t A = 0;

		bool operator==(const Rgba8& other) const = default;
	};

	// An image in CPU memory: 8-bit RGBA pixels, row by row from the top-left corner
	class Image
	{
	public:
		Image() = default;
		// An image of the given size with every pixel zero (transparent black)
		Image(uint32_t width, uint32_t height);

		// Decodes a PNG or JPEG file
		[[nodiscard]] static std::expected<Image, Error> Load(const std::filesystem::path& path);
		// Decodes a PNG or JPEG file already in memory
		[[nodiscard]] static std::expected<Image, Error> Decode(std::span<const uint8_t> encoded);
		// Writes the image as a PNG file, replacing any existing file
		[[nodiscard]] std::expected<void, Error> SavePng(const std::filesystem::path& path) const;
		// The image as the bytes of a PNG file
		[[nodiscard]] std::expected<std::vector<uint8_t>, Error> EncodePng() const;

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		bool IsEmpty() const { return m_Width == 0 || m_Height == 0; }

		// Width * height * 4 bytes
		std::span<const uint8_t> GetPixels() const { return m_Pixels; }
		std::span<uint8_t> GetPixels() { return m_Pixels; }

		Rgba8 GetPixel(uint32_t x, uint32_t y) const;
		void SetPixel(uint32_t x, uint32_t y, Rgba8 color);

		bool operator==(const Image& other) const = default;

	private:
		size_t GetPixelOffset(uint32_t x, uint32_t y) const;

	private:
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		std::vector<uint8_t> m_Pixels;
	};

}
