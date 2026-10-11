// Turns an equirectangular HDR environment map into the cube maps image-based lighting samples (see
// Lodestone/Renderer/EnvironmentMap.cpp): the environment itself, with mips, for the sky and as the source of the
// prefiltered map, which holds the environment's specular reflection for each roughness, one per mip level

#include "EnvironmentMapping.hlsli"

static const float Pi = 3.14159265358979323846;

// Matches EnvironmentConstants in Lodestone/Renderer/ShaderTypes.h
struct EnvironmentConstants
{
	// The size of the face being written, in texels
	uint FaceSize;
	// For prefiltering: how many directions each texel samples
	uint SampleCount;
	// For prefiltering: the roughness of the mip level being written
	float Roughness;
	// For prefiltering: the size of the source cube map's faces, and its mip count
	float SourceFaceSize;
	float SourceMipCount;
	uint Padding0;
	uint Padding1;
	uint Padding2;
};

[[vk::push_constant]] EnvironmentConstants g_Constants;

Texture2D<float4> g_Equirectangular : register(t0);
// A mip level of a cube map, as an array of its six faces
Texture2DArray<float4> g_SourceFaces : register(t1);
TextureCube<float4> g_SourceCube : register(t2);
SamplerState g_Sampler : register(s0);
RWTexture2DArray<float4> g_Destination : register(u0);

float2 FacePoint(uint2 texel)
{
	return (float2(texel) + 0.5) / float(g_Constants.FaceSize) * 2.0 - 1.0;
}

// One thread per texel of the cube map's first mip level; z is the face
[numthreads(8, 8, 1)]
void CSEquirectangularToCube(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= g_Constants.FaceSize || id.y >= g_Constants.FaceSize)
		return;
	const float3 direction = CubeFaceDirection(id.z, FacePoint(id.xy));
	const float2 uv = DirectionToEquirectangular(direction);
	g_Destination[id] = float4(g_Equirectangular.SampleLevel(g_Sampler, uv, 0.0).rgb, 1.0);
}

// One thread per texel of the mip level being written: the average of the four texels it covers in the level above
[numthreads(8, 8, 1)]
void CSDownsampleCube(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= g_Constants.FaceSize || id.y >= g_Constants.FaceSize)
		return;
	const int2 source = int2(id.xy * 2);
	const int face = int(id.z);
	float4 sum = 0.0;
	for (int y = 0; y < 2; ++y)
	{
		for (int x = 0; x < 2; ++x)
			sum += g_SourceFaces.Load(int4(source + int2(x, y), face, 0));
	}
	g_Destination[id] = sum * 0.25;
}

float RadicalInverse(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint index, uint count)
{
	return float2((float(index) + 0.5) / float(count), RadicalInverse(index));
}

// A half vector around +Z, distributed like the GGX normal distribution
float3 ImportanceSampleGgx(float2 xi, float alpha)
{
	const float phi = 2.0 * Pi * xi.y;
	const float cosTheta = sqrt((1.0 - xi.x) / (1.0 + (alpha * alpha - 1.0) * xi.x));
	const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float DistributionGgx(float nDotH, float alpha)
{
	const float a2 = alpha * alpha;
	const float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
	return a2 / (Pi * d * d);
}

// One thread per texel of a mip level of the prefiltered map: the environment's reflection off a surface of the mip
// level's roughness, by GGX importance sampling with the normal and view along the texel's direction. Each sample
// reads the source's mip level whose texels cover the sample's solid angle (filtered importance sampling, Krivanek and
// Colbert 2008), which keeps bright spots such as the sun from turning into speckles
[numthreads(8, 8, 1)]
void CSPrefilter(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= g_Constants.FaceSize || id.y >= g_Constants.FaceSize)
		return;
	const float3 n = CubeFaceDirection(id.z, FacePoint(id.xy));
	const float sourceTexelSolidAngle = 4.0 * Pi / (6.0 * g_Constants.SourceFaceSize * g_Constants.SourceFaceSize);

	if (g_Constants.Roughness <= 0.0)
	{
		// A mirror: the environment itself, from the source level whose texels match this level's
		const float lod = max(log2(g_Constants.SourceFaceSize / float(g_Constants.FaceSize)), 0.0);
		g_Destination[id] = float4(g_SourceCube.SampleLevel(g_Sampler, n, lod).rgb, 1.0);
		return;
	}

	const float3 up = abs(n.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
	const float3 tangentX = normalize(cross(up, n));
	const float3 tangentY = cross(n, tangentX);
	const float alpha = g_Constants.Roughness * g_Constants.Roughness;

	float3 sum = 0.0;
	float weight = 0.0;
	for (uint i = 0; i < g_Constants.SampleCount; ++i)
	{
		const float3 h = ImportanceSampleGgx(Hammersley(i, g_Constants.SampleCount), alpha);
		const float3 halfway = tangentX * h.x + tangentY * h.y + n * h.z;
		const float3 l = 2.0 * dot(n, halfway) * halfway - n;
		const float nDotL = dot(n, l);
		if (nDotL <= 0.0)
			continue;
		// With the view along the normal, the half vector's pdf over reflected directions is D / 4
		const float pdf = DistributionGgx(h.z, alpha) * 0.25;
		const float sampleSolidAngle = 1.0 / (float(g_Constants.SampleCount) * pdf + 1e-6);
		const float lod = clamp(0.5 * log2(sampleSolidAngle / sourceTexelSolidAngle) + 1.0, 0.0,
			g_Constants.SourceMipCount - 1.0);
		sum += g_SourceCube.SampleLevel(g_Sampler, l, lod).rgb * nDotL;
		weight += nDotL;
	}
	g_Destination[id] = float4(sum / max(weight, 1e-6), 1.0);
}
