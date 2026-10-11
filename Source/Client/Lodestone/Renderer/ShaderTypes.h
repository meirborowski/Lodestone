#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstdint>

// The C++ side of the structs the scene renderer's shaders read (Source/Client/Shaders). Each matches its HLSL twin
// member for member - change both together. Constant buffers use 16-byte rows, so every member here is a 16-byte vector
// or matrix, or a group of scalars that fills one

namespace Lodestone::ShaderTypes {

	inline constexpr uint32_t MaxDirectionalLights = 4;
	inline constexpr uint32_t MaxShadowCascades = 4;
	inline constexpr uint32_t NoShadowedLight = 0xFFFFFFFF;

	// Common.hlsli
	struct DirectionalLight
	{
		glm::vec4 Direction{0.0f};
		glm::vec4 Color{0.0f};
	};

	// Common.hlsli
	struct FrameConstants
	{
		glm::mat4 View{1.0f};
		glm::mat4 Projection{1.0f};
		glm::mat4 ViewProjection{1.0f};
		glm::mat4 InverseProjection{1.0f};
		glm::mat4 InverseViewProjection{1.0f};
		glm::vec4 CameraPosition{0.0f};
		glm::vec4 ViewportSize{0.0f};
		glm::uvec4 Counts{0, 0, NoShadowedLight, 0};
		std::array<DirectionalLight, MaxDirectionalLights> DirectionalLights{};
		std::array<glm::mat4, MaxShadowCascades> CascadeViewProjections{};
		glm::vec4 CascadeSplits{0.0f};
		glm::vec4 CascadeTexelSizes{0.0f};
		glm::vec4 CascadeDepthRanges{0.0f};
		glm::vec4 Shadow{0.0f};
		std::array<glm::vec4, 9> IrradianceSh{};
		glm::vec4 Environment{0.0f};
		glm::vec4 AmbientOcclusion{0.0f};
	};
	static_assert(sizeof(FrameConstants) == 992);

	// Common.hlsli
	struct PunctualLight
	{
		glm::vec4 PositionRange{0.0f};
		glm::vec4 ColorAngleScale{0.0f};
		glm::vec4 DirectionAngleOffset{0.0f};
	};
	static_assert(sizeof(PunctualLight) == 48);

	enum MaterialFlags : uint32_t
	{
		MaterialHasNormalTexture = 1,
	};

	// Mesh.hlsl
	struct MaterialConstants
	{
		glm::vec4 BaseColor{1.0f};
		glm::vec4 Emissive{0.0f};
		float Metallic = 1.0f;
		float Roughness = 1.0f;
		float NormalScale = 1.0f;
		float OcclusionStrength = 1.0f;
		float AlphaCutoff = 0.5f;
		uint32_t Flags = 0;
		uint32_t Padding[2] = {};
	};
	static_assert(sizeof(MaterialConstants) == 64);

	// Mesh.hlsl, in push constants
	struct DrawConstants
	{
		glm::mat4 World{1.0f};
		std::array<glm::vec4, 3> NormalMatrix{};
		uint32_t LightOffset = 0;
		uint32_t LightCount = 0;
	};
	static_assert(sizeof(DrawConstants) == 120);
	static_assert(sizeof(DrawConstants) <= 128, "Push constants beyond 128 bytes aren't guaranteed");

	// AmbientOcclusion.hlsl, in push constants
	struct AmbientOcclusionConstants
	{
		glm::vec4 ProjectionScale{0.0f};
		glm::vec4 DepthToDistance{0.0f};
		glm::vec4 ViewportSize{0.0f};
		float Radius = 0.5f;
		float Intensity = 1.0f;
		float Padding[2] = {};
	};
	static_assert(sizeof(AmbientOcclusionConstants) == 64);

	// Tonemap.hlsl, in push constants
	struct TonemapConstants
	{
		float Exposure = 1.0f;
		uint32_t Tonemapper = 0;
		float Padding[2] = {};
	};
	static_assert(sizeof(TonemapConstants) == 16);

	// Environment.hlsl, in push constants
	struct EnvironmentConstants
	{
		uint32_t FaceSize = 0;
		uint32_t SampleCount = 0;
		float Roughness = 0.0f;
		float SourceFaceSize = 0.0f;
		float SourceMipCount = 0.0f;
		uint32_t Padding[3] = {};
	};
	static_assert(sizeof(EnvironmentConstants) == 32);

}
