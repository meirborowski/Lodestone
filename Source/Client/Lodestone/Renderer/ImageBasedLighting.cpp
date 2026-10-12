#include "Lodestone/Renderer/ImageBasedLighting.h"

#include "Lodestone/Core/Assert.h"

#include <glm/common.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace Lodestone {

	namespace {

		constexpr float Pi = glm::pi<float>();
		// Convolving with the cosine lobe scales each band of the harmonics (pi, 2pi/3, pi/4); dividing by pi gives
		// the light a white diffuse surface reflects
		constexpr std::array<double, 9> BandScales = {
			1.0, 2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0, 0.25, 0.25, 0.25, 0.25, 0.25};
		// The default environment's size and colors
		constexpr uint32_t DefaultEnvironmentWidth = 64;
		constexpr uint32_t DefaultEnvironmentHeight = 32;
		constexpr glm::vec3 DefaultZenith{0.42f, 0.45f, 0.5f};
		constexpr glm::vec3 DefaultHorizon{0.62f, 0.62f, 0.62f};
		constexpr glm::vec3 DefaultGround{0.2f, 0.19f, 0.18f};

		// The real spherical harmonics basis functions, in the order of SphericalHarmonics
		std::array<float, 9> EvaluateBasis(const glm::vec3& d)
		{
			return {
				0.282095f,
				0.488603f * d.y,
				0.488603f * d.z,
				0.488603f * d.x,
				1.092548f * d.x * d.y,
				1.092548f * d.y * d.z,
				0.315392f * (3.0f * d.z * d.z - 1.0f),
				1.092548f * d.x * d.z,
				0.546274f * (d.x * d.x - d.y * d.y),
			};
		}

		float RadicalInverse(uint32_t bits)
		{
			bits = (bits << 16u) | (bits >> 16u);
			bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
			bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
			bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
			bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
			return static_cast<float>(bits) * 2.3283064365386963e-10f;
		}

		// The height-correlated Smith visibility term for GGX, as in Lighting.hlsli
		float VisibilitySmithGgxCorrelated(float nDotV, float nDotL, float alpha)
		{
			const float a2 = alpha * alpha;
			const float lambdaV = nDotL * std::sqrt(nDotV * nDotV * (1.0f - a2) + a2);
			const float lambdaL = nDotV * std::sqrt(nDotL * nDotL * (1.0f - a2) + a2);
			return 0.5f / std::max(lambdaV + lambdaL, 1e-6f);
		}

	}

	glm::vec2 DirectionToEquirectangular(const glm::vec3& direction)
	{
		const float azimuth = std::atan2(direction.x, -direction.z);
		const float elevation = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
		return {azimuth / (2.0f * Pi) + 0.5f, 0.5f - elevation / Pi};
	}

	glm::vec3 EquirectangularToDirection(const glm::vec2& point)
	{
		const float azimuth = (point.x - 0.5f) * 2.0f * Pi;
		const float elevation = (0.5f - point.y) * Pi;
		const float horizontal = std::cos(elevation);
		return {horizontal * std::sin(azimuth), std::sin(elevation), -horizontal * std::cos(azimuth)};
	}

	SphericalHarmonics ComputeIrradiance(const HdrImage& environment)
	{
		LS_CORE_ASSERT(!environment.IsEmpty(), "An empty image has no irradiance");
		const uint32_t width = environment.GetWidth();
		const uint32_t height = environment.GetHeight();

		// Project the radiance onto the basis, each pixel weighted by the solid angle it covers, which shrinks toward
		// the poles. The weights are normalized to cover the sphere exactly
		std::array<glm::dvec3, 9> radiance{};
		double totalSolidAngle = 0.0;
		for (uint32_t y = 0; y < height; ++y)
		{
			const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
			const double solidAngle = std::cos((0.5 - static_cast<double>(v)) * Pi);
			for (uint32_t x = 0; x < width; ++x)
			{
				const glm::vec2 point((static_cast<float>(x) + 0.5f) / static_cast<float>(width), v);
				const std::array<float, 9> basis = EvaluateBasis(EquirectangularToDirection(point));
				const glm::dvec3 pixel(glm::vec3(environment.GetPixel(x, y)));
				for (size_t i = 0; i < basis.size(); ++i)
					radiance[i] += pixel * (static_cast<double>(basis[i]) * solidAngle);
				totalSolidAngle += solidAngle;
			}
		}

		const double normalization = 4.0 * glm::pi<double>() / totalSolidAngle;
		SphericalHarmonics irradiance{};
		for (size_t i = 0; i < irradiance.size(); ++i)
			irradiance[i] = glm::vec3(radiance[i] * (BandScales[i] * normalization));
		return irradiance;
	}

	glm::vec3 EvaluateSphericalHarmonics(const SphericalHarmonics& harmonics, const glm::vec3& direction)
	{
		const std::array<float, 9> basis = EvaluateBasis(direction);
		glm::vec3 result(0.0f);
		for (size_t i = 0; i < basis.size(); ++i)
			result += harmonics[i] * basis[i];
		return result;
	}

	HdrImage DownsampleHdr(const HdrImage& image, uint32_t maxWidth)
	{
		LS_CORE_ASSERT(maxWidth > 0, "Images can't be downsampled to nothing");
		HdrImage result = image;
		while (result.GetWidth() > maxWidth && result.GetWidth() > 1)
		{
			const uint32_t width = result.GetWidth() / 2;
			const uint32_t height = std::max(result.GetHeight() / 2, 1u);
			HdrImage half(width, height);
			for (uint32_t y = 0; y < height; ++y)
			{
				for (uint32_t x = 0; x < width; ++x)
				{
					// The 2x2 block, clamped at the edges of an image one pixel high
					const uint32_t top = std::min(y * 2, result.GetHeight() - 1);
					const uint32_t bottom = std::min(y * 2 + 1, result.GetHeight() - 1);
					const glm::vec4 sum = result.GetPixel(x * 2, top) + result.GetPixel(x * 2 + 1, top) +
						result.GetPixel(x * 2, bottom) + result.GetPixel(x * 2 + 1, bottom);
					half.SetPixel(x, y, sum * 0.25f);
				}
			}
			result = std::move(half);
		}
		return result;
	}

	std::vector<glm::vec2> ComputeBrdfLut(uint32_t size, uint32_t sampleCount)
	{
		LS_CORE_ASSERT(size > 0 && sampleCount > 0, "The lookup table needs texels and samples");
		std::vector<glm::vec2> table(static_cast<size_t>(size) * size);
		for (uint32_t row = 0; row < size; ++row)
		{
			const float roughness = (static_cast<float>(row) + 0.5f) / static_cast<float>(size);
			const float alpha = roughness * roughness;
			for (uint32_t column = 0; column < size; ++column)
			{
				const float nDotV = (static_cast<float>(column) + 0.5f) / static_cast<float>(size);
				// The view in the surface's tangent space, where the normal is +Z
				const glm::vec3 view(std::sqrt(1.0f - nDotV * nDotV), 0.0f, nDotV);
				float scale = 0.0f;
				float bias = 0.0f;
				for (uint32_t i = 0; i < sampleCount; ++i)
				{
					// A half vector distributed like GGX
					const float xi = (static_cast<float>(i) + 0.5f) / static_cast<float>(sampleCount);
					const float phi = 2.0f * Pi * RadicalInverse(i);
					const float cosTheta = std::sqrt((1.0f - xi) / (1.0f + (alpha * alpha - 1.0f) * xi));
					const float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
					const glm::vec3 half(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);
					const float vDotH = glm::dot(view, half);
					const glm::vec3 light = 2.0f * vDotH * half - view;
					const float nDotL = light.z;
					if (nDotL <= 0.0f || vDotH <= 0.0f)
						continue;
					// The BRDF times n.l over the sample's pdf, without the Fresnel term
					const float weight =
						4.0f * VisibilitySmithGgxCorrelated(nDotV, nDotL, alpha) * nDotL * vDotH / half.z;
					const float fresnel = std::pow(1.0f - vDotH, 5.0f);
					scale += (1.0f - fresnel) * weight;
					bias += fresnel * weight;
				}
				table[static_cast<size_t>(row) * size + column] =
					glm::vec2(scale, bias) / static_cast<float>(sampleCount);
			}
		}
		return table;
	}

	HdrImage MakeDefaultEnvironment()
	{
		HdrImage image(DefaultEnvironmentWidth, DefaultEnvironmentHeight);
		for (uint32_t y = 0; y < DefaultEnvironmentHeight; ++y)
		{
			const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(DefaultEnvironmentHeight);
			const float elevation = std::sin((0.5f - v) * Pi);
			// A soft step at the horizon, so the ground doesn't show a hard line
			const float sky = glm::smoothstep(-0.08f, 0.08f, elevation);
			const glm::vec3 above = glm::mix(DefaultHorizon, DefaultZenith, std::sqrt(std::max(elevation, 0.0f)));
			const glm::vec3 color = glm::mix(DefaultGround, above, sky);
			for (uint32_t x = 0; x < DefaultEnvironmentWidth; ++x)
				image.SetPixel(x, y, glm::vec4(color, 1.0f));
		}
		return image;
	}

}
