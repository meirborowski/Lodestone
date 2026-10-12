#include "Lodestone/Renderer/ImageBasedLighting.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>

namespace Lodestone {

	namespace {

		bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
		{
			return glm::length(a - b) <= tolerance;
		}

		// An environment with one radiance above the horizon and another below
		HdrImage MakeHalves(float above, float below)
		{
			HdrImage image(64, 32);
			for (uint32_t y = 0; y < image.GetHeight(); ++y)
			{
				for (uint32_t x = 0; x < image.GetWidth(); ++x)
					image.SetPixel(x, y, glm::vec4(glm::vec3(y < image.GetHeight() / 2 ? above : below), 1.0f));
			}
			return image;
		}

		float Luminance(const glm::vec3& color)
		{
			return (color.r + color.g + color.b) / 3.0f;
		}

	}

	TEST_CASE("Equirectangular images look toward -Z at their center, +X at three quarters and up at the top")
	{
		CHECK(Near(EquirectangularToDirection(glm::vec2(0.5f, 0.5f)), glm::vec3(0.0f, 0.0f, -1.0f)));
		CHECK(Near(EquirectangularToDirection(glm::vec2(0.75f, 0.5f)), glm::vec3(1.0f, 0.0f, 0.0f)));
		CHECK(Near(EquirectangularToDirection(glm::vec2(0.25f, 0.5f)), glm::vec3(-1.0f, 0.0f, 0.0f)));
		CHECK(Near(EquirectangularToDirection(glm::vec2(0.3f, 0.0f)), glm::vec3(0.0f, 1.0f, 0.0f)));
		CHECK(Near(EquirectangularToDirection(glm::vec2(0.3f, 1.0f)), glm::vec3(0.0f, -1.0f, 0.0f)));

		for (const glm::vec2 point : {glm::vec2(0.1f, 0.2f), glm::vec2(0.6f, 0.45f), glm::vec2(0.9f, 0.8f)})
		{
			const glm::vec3 direction = EquirectangularToDirection(point);
			CHECK(glm::length(direction) == doctest::Approx(1.0f));
			const glm::vec2 back = DirectionToEquirectangular(direction);
			CHECK(back.x == doctest::Approx(point.x));
			CHECK(back.y == doctest::Approx(point.y));
		}
	}

	TEST_CASE("A uniform environment lights every direction equally, as much as it shines")
	{
		const SphericalHarmonics irradiance = ComputeIrradiance(MakeHalves(2.0f, 2.0f));

		for (const glm::vec3 normal : {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
				 glm::vec3(1.0f, 0.0f, 0.0f), glm::normalize(glm::vec3(1.0f, -2.0f, 3.0f))})
		{
			const glm::vec3 light = EvaluateSphericalHarmonics(irradiance, normal);
			CHECK(light.r == doctest::Approx(2.0f).epsilon(0.01));
			CHECK(light.g == doctest::Approx(2.0f).epsilon(0.01));
		}
	}

	TEST_CASE(
		"A sky over a black ground lights surfaces facing up, half lights walls and leaves the floor's underside dark")
	{
		const SphericalHarmonics irradiance = ComputeIrradiance(MakeHalves(1.0f, 0.0f));

		const float up = Luminance(EvaluateSphericalHarmonics(irradiance, glm::vec3(0.0f, 1.0f, 0.0f)));
		const float side = Luminance(EvaluateSphericalHarmonics(irradiance, glm::vec3(0.0f, 0.0f, 1.0f)));
		const float down = Luminance(EvaluateSphericalHarmonics(irradiance, glm::vec3(0.0f, -1.0f, 0.0f)));
		// Order-2 harmonics approximate the cosine lobe to within a few percent
		CHECK(up == doctest::Approx(1.0f).epsilon(0.06));
		CHECK(side == doctest::Approx(0.5f).epsilon(0.03));
		CHECK(std::abs(down) < 0.06f);
	}

	TEST_CASE("Downsampling an HDR image halves it until it fits, keeping its average")
	{
		HdrImage image(16, 8);
		float sum = 0.0f;
		for (uint32_t y = 0; y < 8; ++y)
		{
			for (uint32_t x = 0; x < 16; ++x)
			{
				const auto value = static_cast<float>(x * 3 + y * 7 % 5);
				image.SetPixel(x, y, glm::vec4(value, 1.0f, 0.0f, 1.0f));
				sum += value;
			}
		}

		const HdrImage small = DownsampleHdr(image, 5);

		CHECK(small.GetWidth() == 4);
		CHECK(small.GetHeight() == 2);
		float smallSum = 0.0f;
		for (uint32_t y = 0; y < 2; ++y)
		{
			for (uint32_t x = 0; x < 4; ++x)
				smallSum += small.GetPixel(x, y).r;
		}
		CHECK(smallSum / 8.0f == doctest::Approx(sum / 128.0f));
		CHECK(DownsampleHdr(image, 16) == image);
	}

	TEST_CASE("The BRDF lookup table conserves energy and reflects everything from smooth surfaces head on")
	{
		constexpr uint32_t size = 16;
		const std::vector<glm::vec2> table = ComputeBrdfLut(size, 256);
		REQUIRE(table.size() == size * size);
		const auto at = [&](uint32_t column, uint32_t row) { return table[row * size + column]; };

		for (const glm::vec2 value : table)
		{
			CHECK(value.x >= 0.0f);
			CHECK(value.y >= 0.0f);
			// With F0 = 1 the reflectance is scale + bias, which can't exceed 1
			CHECK(value.x + value.y <= 1.001f);
		}
		// Smooth, looking straight on: F0 is reflected as is
		const glm::vec2 smooth = at(size - 1, 0);
		CHECK(smooth.x == doctest::Approx(1.0f).epsilon(0.03));
		CHECK(smooth.y < 0.02f);
		// Smooth, at a grazing angle: Fresnel brightens reflections, adding bias
		CHECK(at(0, 0).y > at(size - 1, 0).y);
		// Rough surfaces lose energy to light that scatters more than once, which this single-scattering model drops
		CHECK(at(size / 2, size - 1).x + at(size / 2, size - 1).y < smooth.x + smooth.y);
	}

	TEST_CASE("The default environment is a grey sky, brighter above than below")
	{
		const HdrImage environment = MakeDefaultEnvironment();

		REQUIRE_FALSE(environment.IsEmpty());
		const glm::vec4 top = environment.GetPixel(0, 0);
		const glm::vec4 bottom = environment.GetPixel(0, environment.GetHeight() - 1);
		CHECK(top.r > bottom.r);
		CHECK(bottom.r > 0.0f);
		const SphericalHarmonics irradiance = ComputeIrradiance(environment);
		CHECK(Luminance(EvaluateSphericalHarmonics(irradiance, glm::vec3(0.0f, 1.0f, 0.0f))) >
			Luminance(EvaluateSphericalHarmonics(irradiance, glm::vec3(0.0f, -1.0f, 0.0f))));
	}

}
