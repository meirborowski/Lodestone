// What the scene renderer's shaders share: the view's constants and resources (descriptor set 0), the environment's
// (set 1), and math. Every struct here has a twin in Lodestone/Renderer/ShaderTypes.h - change both together.
//
// Depth is reversed in the scene's depth buffer: 1 at the near plane, 0 at the far plane

#ifndef LODESTONE_COMMON_HLSLI
#define LODESTONE_COMMON_HLSLI

static const float Pi = 3.14159265358979323846;
static const uint MaxDirectionalLights = 4;
static const uint MaxShadowCascades = 4;
static const uint NoShadowedLight = 0xFFFFFFFF;

struct DirectionalLight
{
	// xyz: the direction the light travels, in world space
	float4 Direction;
	// rgb: the color times the intensity
	float4 Color;
};

struct FrameConstants
{
	float4x4 View;
	float4x4 Projection;
	float4x4 ViewProjection;
	float4x4 InverseProjection;
	float4x4 InverseViewProjection;
	// xyz: the camera's position in world space
	float4 CameraPosition;
	// xy: the size of the image in pixels, zw: one over it
	float4 ViewportSize;
	// x: directional lights, y: shadow cascades, z: the shadowed directional light (or NoShadowedLight), w: unused
	uint4 Counts;
	DirectionalLight DirectionalLights[MaxDirectionalLights];
	// From world space to each cascade's shadow map: x and y from -1 to 1, depth from 0 (nearest the light) to 1
	float4x4 CascadeViewProjections[MaxShadowCascades];
	// The view distance at which each cascade ends
	float4 CascadeSplits;
	// The size of a shadow map texel of each cascade, in meters
	float4 CascadeTexelSizes;
	// How many meters each cascade's depth range spans
	float4 CascadeDepthRanges;
	// x: the tangent of the light's angular radius (how soft shadows are), y: one over the shadow map's size,
	// z: the view distance at which shadows start to fade out, w: the distance at which they're gone
	float4 Shadow;
	// The environment's irradiance as order-2 spherical harmonics, divided by pi and scaled by its intensity: the
	// diffuse light a white surface reflects (rgb)
	float4 IrradianceSh[9];
	// x: intensity, y and z: the sine and cosine of its rotation, w: the prefiltered map's last mip level
	float4 Environment;
	// x: whether there's ambient occlusion (1) or not (0), y: radius in meters, z: intensity, w: unused
	float4 AmbientOcclusion;
};

// A point or spot light. A point light has an angle scale of 0 and an offset of 1, so its cone covers everything
struct PunctualLight
{
	// xyz: position, w: range
	float4 PositionRange;
	// rgb: the color times the intensity, w: the cone's angle scale
	float4 ColorAngleScale;
	// xyz: the direction the light points, w: the cone's angle offset
	float4 DirectionAngleOffset;
};

// Descriptor set 0: the view
ConstantBuffer<FrameConstants> g_Frame : register(b0, space0);
StructuredBuffer<PunctualLight> g_PunctualLights : register(t0, space0);
// The punctual lights that reach each draw: DrawConstants.LightOffset and LightCount select a range
StructuredBuffer<uint> g_LightIndices : register(t1, space0);
Texture2D<float> g_AmbientOcclusionTexture : register(t2, space0);
Texture2DArray<float> g_ShadowMap : register(t3, space0);
SamplerState g_PointClampSampler : register(s0, space0);
// Returns 1 where the reference depth is less than the shadow map's
SamplerComparisonState g_ShadowSampler : register(s1, space0);

// Descriptor set 1: the environment
TextureCube<float4> g_PrefilteredEnvironment : register(t0, space1);
// The split-sum lookup table for specular image-based lighting: x scales F0, y is added
Texture2D<float2> g_BrdfLut : register(t1, space1);
TextureCube<float4> g_EnvironmentMap : register(t2, space1);
SamplerState g_LinearClampSampler : register(s0, space1);

// A direction in the world turned into the environment map's space, against the environment's rotation
float3 ToEnvironmentDirection(float3 direction)
{
	const float s = g_Frame.Environment.y;
	const float c = g_Frame.Environment.z;
	return float3(direction.x * c - direction.z * s, direction.y, direction.x * s + direction.z * c);
}

// The environment's irradiance from the spherical harmonics, for a unit normal in world space
float3 EvaluateIrradiance(float3 normal)
{
	const float3 n = ToEnvironmentDirection(normal);
	const float4 sh[9] = g_Frame.IrradianceSh;
	float3 result = sh[0].rgb * 0.282095;
	result += sh[1].rgb * (0.488603 * n.y);
	result += sh[2].rgb * (0.488603 * n.z);
	result += sh[3].rgb * (0.488603 * n.x);
	result += sh[4].rgb * (1.092548 * n.x * n.y);
	result += sh[5].rgb * (1.092548 * n.y * n.z);
	result += sh[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0));
	result += sh[7].rgb * (1.092548 * n.x * n.z);
	result += sh[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
	return max(result, 0.0);
}

// Noise from 0 to 1 that changes from pixel to pixel but repeats in a 4x4 tile, so a 4x4 blur removes it
float TileNoise(uint2 pixel)
{
	static const float Pattern[16] = {0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0,
		13.0, 5.0};
	return (Pattern[(pixel.y & 3) * 4 + (pixel.x & 3)] + 0.5) / 16.0;
}

// Interleaved gradient noise (Jimenez 2014): from 0 to 1, without visible patterns
float InterleavedGradientNoise(float2 pixel)
{
	return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

#endif
