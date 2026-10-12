#pragma once

#include "Lodestone/Core/BoundingBox.h"
#include "Lodestone/Renderer/RenderWorld.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace Lodestone {

	// One of a directional light's cascaded shadow maps: an orthographic view from the light that covers a slice of
	// the camera's view, nearer slices with finer texels
	struct ShadowCascade
	{
		// From world space to the cascade's shadow map: x and y from -1 to 1 across it, depth from 0 (nearest the
		// light) to 1
		glm::mat4 ViewProjection{1.0f};
		// The view depth (distance along the camera's forward direction) where the cascade's slice ends
		float SplitDepth = 0.0f;
		// The width of a shadow map texel, in meters
		float TexelSize = 0.0f;
		// The meters the cascade's depth range spans, from 0 to 1
		float DepthRange = 0.0f;
	};

	struct ShadowCascadeSettings
	{
		uint32_t CascadeCount = 4;
		// The width and height of each cascade's shadow map, in texels
		uint32_t MapSize = 2048;
		// Shadows end this far from the camera, in meters
		float ShadowDistance = 60.0f;
		// How the slices are split: 0 evenly, 1 logarithmically (each slice a fixed multiple deeper than the last)
		float SplitBlend = 0.75f;
	};

	// Fits a directional light's cascades to a view. Each cascade covers its slice of the view's frustum with a
	// bounding sphere, so its size doesn't change as the camera turns, and moves only in whole texels, so shadow edges
	// don't shimmer as the camera moves. Its depth range reaches back toward the light to take in every caster in
	// casterBounds, wherever the camera is. No cascades when the view's near plane is beyond the shadow distance
	std::vector<ShadowCascade> ComputeShadowCascades(const glm::vec3& lightDirection, const RenderView& view,
		float aspectRatio, const ShadowCascadeSettings& settings, const BoundingBox& casterBounds);

}
