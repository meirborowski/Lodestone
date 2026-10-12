#include "Lodestone/Renderer/ShadowCascades.h"

#include "Lodestone/Core/Assert.h"

#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Lodestone {

	namespace {

		// Depth room around what a cascade covers, in meters, so nothing sits exactly on its near or far plane
		constexpr float DepthMargin = 0.5f;

		// A rotation into the light's space, looking along its direction
		glm::mat4 MakeLightRotation(const glm::vec3& direction)
		{
			const glm::vec3 up =
				std::abs(direction.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			return glm::lookAtRH(glm::vec3(0.0f), direction, up);
		}

	}

	std::vector<ShadowCascade> ComputeShadowCascades(const glm::vec3& lightDirection, const RenderView& view,
		float aspectRatio, const ShadowCascadeSettings& settings, const BoundingBox& casterBounds)
	{
		LS_CORE_ASSERT(settings.CascadeCount > 0 && settings.MapSize > 0, "Cascades need a count and a size");
		const float nearDepth = view.NearPlane;
		const float farDepth = std::min(view.FarPlane, settings.ShadowDistance);
		if (!(farDepth > nearDepth) || !(glm::length(lightDirection) > 0.0f))
			return {};

		const glm::mat4 cameraToWorld = glm::inverse(view.View);
		const glm::vec3 cameraPosition(cameraToWorld[3]);
		const glm::vec3 cameraForward = -glm::normalize(glm::vec3(cameraToWorld[2]));
		// How far a corner of the frustum is from its axis, squared, per unit of depth squared
		const float tangent = std::tan(view.FieldOfView * 0.5f);
		const float cornerSpread = tangent * tangent * (1.0f + aspectRatio * aspectRatio);

		const glm::mat4 lightRotation = MakeLightRotation(glm::normalize(lightDirection));
		// The casters' depth range along the light, in light space (looking down -Z, so nearer the light is greater)
		float casterNearest = std::numeric_limits<float>::lowest();
		if (!casterBounds.IsEmpty())
		{
			const BoundingBox lightBounds = casterBounds.Transform(lightRotation);
			casterNearest = lightBounds.Max.z;
		}

		std::vector<ShadowCascade> cascades;
		cascades.reserve(settings.CascadeCount);
		float sliceStart = nearDepth;
		for (uint32_t i = 1; i <= settings.CascadeCount; ++i)
		{
			const float fraction = static_cast<float>(i) / static_cast<float>(settings.CascadeCount);
			const float uniform = nearDepth + (farDepth - nearDepth) * fraction;
			const float logarithmic = nearDepth * std::pow(farDepth / nearDepth, fraction);
			const float sliceEnd =
				i == settings.CascadeCount ? farDepth : glm::mix(uniform, logarithmic, settings.SplitBlend);

			// The smallest sphere around the slice: centered on the view axis, where it's as far from the near
			// corners as from the far ones - or at the far end, for slices whose far corners alone decide it
			float centerDepth = (sliceStart + sliceEnd) * (1.0f + cornerSpread) * 0.5f;
			float radius = 0.0f;
			if (centerDepth >= sliceEnd)
			{
				centerDepth = sliceEnd;
				radius = sliceEnd * std::sqrt(cornerSpread);
			}
			else
			{
				const float offset = centerDepth - sliceStart;
				radius = std::sqrt(offset * offset + sliceStart * sliceStart * cornerSpread);
			}
			// Rounded up, so small changes in the view don't change the cascade's size
			radius = std::ceil(radius * 16.0f) / 16.0f;

			// The map covers the sphere and a texel more on each side, so snapping its center to the texel grid
			// below never uncovers any of the sphere
			const float texelSize = 2.0f * radius / static_cast<float>(std::max(settings.MapSize, 3u) - 2);
			const float halfSize = radius + texelSize;
			glm::vec3 center = glm::vec3(lightRotation * glm::vec4(cameraPosition + cameraForward * centerDepth, 1.0f));
			center.x = std::floor(center.x / texelSize) * texelSize;
			center.y = std::floor(center.y / texelSize) * texelSize;

			// From the light's side of every caster to the far side of the slice's sphere
			const float nearest = std::max(center.z + radius, casterNearest) + DepthMargin;
			const float farthest = center.z - radius - DepthMargin;
			const glm::mat4 projection = glm::orthoRH_ZO(center.x - halfSize, center.x + halfSize, center.y - halfSize,
				center.y + halfSize, -nearest, -farthest);

			cascades.push_back({
				.ViewProjection = projection * lightRotation,
				.SplitDepth = sliceEnd,
				.TexelSize = texelSize,
				.DepthRange = nearest - farthest,
			});
			sliceStart = sliceEnd;
		}
		return cascades;
	}

}
