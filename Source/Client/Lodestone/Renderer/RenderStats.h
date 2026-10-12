#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Lodestone {

	// The scene renderer's passes, in the order they run
	enum class RenderPass : uint8_t
	{
		// Depth from the directional light, for each shadow cascade
		Shadows,
		// Depth and normals from the camera
		Prepass,
		AmbientOcclusion,
		// Lit opaque and alpha-tested surfaces
		Opaque,
		Sky,
		// Lit transparent surfaces, blended back to front
		Transparent,
		// HDR to the displayed 8-bit image
		Tonemap,
	};

	inline constexpr size_t RenderPassCount = 7;

	std::string_view ToString(RenderPass pass);

	// What the scene renderer did for a frame
	struct RenderStats
	{
		// In every pass, and in each (indexed by RenderPass)
		uint32_t DrawCalls = 0;
		std::array<uint32_t, RenderPassCount> PassDrawCalls{};
		uint64_t Triangles = 0;
		// The world's meshes, and how many of them the camera sees
		uint32_t Meshes = 0;
		uint32_t VisibleMeshes = 0;
		uint32_t DirectionalLights = 0;
		uint32_t PunctualLights = 0;
		uint32_t ShadowCascades = 0;
		// Milliseconds the CPU spent recording the frame, loading and uploading assets used for the first time included
		double CpuTime = 0.0;
		// Milliseconds the GPU spent on each pass (indexed by RenderPass), from the latest frame it has finished - a
		// frame or more behind, as the GPU works after the CPU records. Empty until a frame finishes, and where the
		// device can't time work - it has no timer queries, or they measure no time at all
		std::optional<std::array<double, RenderPassCount>> GpuPassTimes;

		// The GPU's time for all the passes, if it's known
		std::optional<double> GetGpuTime() const;
	};

}
