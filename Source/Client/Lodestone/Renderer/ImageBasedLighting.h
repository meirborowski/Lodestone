#pragma once

#include "Lodestone/Core/HdrImage.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <vector>

// The CPU side of image-based lighting: mapping directions to environment images, the environment's diffuse light as
// spherical harmonics, and the lookup table for its specular light. The GPU side - cube maps and prefiltering - is in
// EnvironmentMap

namespace Lodestone {

	// Order-2 spherical harmonics of an RGB function on the sphere: nine coefficients, in the order of
	// EvaluateSphericalHarmonics() and Common.hlsli
	using SphericalHarmonics = std::array<glm::vec3, 9>;

	// Where a unit direction falls in an equirectangular image, from (0, 0) at its top left to (1, 1). The image's
	// center looks toward -Z, its right quarter toward +X, its top row straight up - as EnvironmentMapping.hlsli maps
	// it
	glm::vec2 DirectionToEquirectangular(const glm::vec3& direction);
	// The unit direction through a point of an equirectangular image
	glm::vec3 EquirectangularToDirection(const glm::vec2& point);

	// The irradiance an equirectangular environment casts, as spherical harmonics, divided by pi: evaluated for a unit
	// normal, it's the light a white diffuse surface facing that way reflects (Ramamoorthi and Hanrahan 2001)
	SphericalHarmonics ComputeIrradiance(const HdrImage& environment);
	glm::vec3 EvaluateSphericalHarmonics(const SphericalHarmonics& harmonics, const glm::vec3& direction);

	// The image, halved in size until it's at most a width, each pixel the average of the ones it covers - so the
	// light it holds stays the same. Images already that small are returned as they are
	HdrImage DownsampleHdr(const HdrImage& image, uint32_t maxWidth);

	// The split-sum lookup table for specular image-based lighting (Karis 2013), size x size texels: for n.v across (x)
	// and perceptual roughness down (y), each from 0 to 1 at texel centers, the scale (x) and bias (y) that turn F0
	// into a GGX surface's reflectance under uniform light
	std::vector<glm::vec2> ComputeBrdfLut(uint32_t size, uint32_t sampleCount);

	// The environment of scenes that have none: a plain grey sky over a darker ground
	HdrImage MakeDefaultEnvironment();

}
