// Screen-space ambient occlusion (see Lodestone/Renderer/SceneRenderer.cpp): how much of the light arriving from the
// hemisphere above each pixel's surface nearby geometry blocks, from the prepass's depth and view-space normals.
//
// PSOcclusion is ground-truth ambient occlusion (Jimenez et al. 2016): it divides the hemisphere into slices through
// the view direction, finds the horizon on each side of each slice by marching across the depth buffer, and integrates
// the cosine-weighted visibility between the horizons exactly. Each pixel takes two slices, turned from pixel to pixel
// in a 4x4 pattern; PSBlur averages each 4x4 block of pixels, which combines the 32 slices and removes the pattern,
// but only across pixels at a similar depth, so occlusion doesn't bleed across edges

#include "Fullscreen.hlsli"

static const float Pi = 3.14159265358979323846;
static const uint SliceCount = 2;
// Samples on each side of a slice
static const uint StepCount = 6;
// Occluders fade out over this last part of the radius, so the occlusion has no hard edge
static const float FalloffFraction = 0.6;
// The widest the radius gets on screen, which bounds the cost of nearby surfaces
static const float MaxRadiusPixels = 128.0;

// Matches AmbientOcclusionConstants in Lodestone/Renderer/ShaderTypes.h
struct AmbientOcclusionConstants
{
	// x and y: the projection's scale of view-space x and y
	float4 ProjectionScale;
	// Turns reversed depth into the distance from the camera: x / (depth * y + z)
	float4 DepthToDistance;
	// xy: the image's size in pixels, zw: one over it
	float4 ViewportSize;
	// How far occluders reach, in meters
	float Radius;
	// An exponent: above 1 darkens occlusion, below 1 lightens it
	float Intensity;
	float2 Padding;
};

[[vk::push_constant]] AmbientOcclusionConstants g_Constants;

Texture2D<float> g_Depth : register(t0);
// PSOcclusion: the view-space normals. PSBlur: the occlusion to blur
Texture2D<float4> g_Input : register(t1);
SamplerState g_PointClampSampler : register(s0);

float GetDistance(float depth)
{
	return g_Constants.DepthToDistance.x / (depth * g_Constants.DepthToDistance.y + g_Constants.DepthToDistance.z);
}

// The view-space position at a point of the image (0 to 1 across it)
float3 GetViewPosition(float2 uv, float depth)
{
	const float distance = GetDistance(depth);
	const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	return float3(ndc * distance / g_Constants.ProjectionScale.xy, -distance);
}

// The 4x4 pattern the blur removes, from 0 to 1. Offsetting the pixel gives another value from the same tile
float TileNoise(uint2 pixel)
{
	static const float Pattern[16] = {0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0,
		13.0, 5.0};
	return (Pattern[(pixel.y & 3) * 4 + (pixel.x & 3)] + 0.5) / 16.0;
}

// The cosine-weighted visibility of the arc of a slice from the view direction to a horizon at angle h, for a normal
// at angle n in the slice (Jimenez et al. 2016)
float IntegrateArc(float h, float n)
{
	return 0.25 * (-cos(2.0 * h - n) + cos(n) + 2.0 * h * sin(n));
}

float PSOcclusion(FullscreenVertex input) : SV_Target
{
	const uint2 pixel = uint2(input.Position.xy);
	const float depth = g_Depth.Load(int3(pixel, 0));
	// The far plane: nothing's there to occlude
	if (depth <= 0.0)
		return 1.0;
	const float3 position = GetViewPosition(input.TexCoord, depth);
	const float3 normal = normalize(g_Input.Load(int3(pixel, 0)).xyz);
	const float3 view = normalize(-position);

	// How many pixels the radius covers at the point's distance
	const float pixelsPerMeter = g_Constants.ProjectionScale.y * 0.5 * g_Constants.ViewportSize.y / -position.z;
	const float radiusPixels = min(g_Constants.Radius * pixelsPerMeter, MaxRadiusPixels);
	// Too far to resolve any occlusion
	if (radiusPixels < 1.0)
		return 1.0;
	const float falloffRange = FalloffFraction * g_Constants.Radius;
	const float falloffStart = g_Constants.Radius - falloffRange;

	const float rotation = TileNoise(pixel);
	const float stepOffset = TileNoise(pixel + uint2(1, 2));
	float visibility = 0.0;
	for (uint slice = 0; slice < SliceCount; ++slice)
	{
		const float angle = (float(slice) + rotation) / float(SliceCount) * Pi;
		// Across the image, y down; in view space, y up
		const float2 screenDirection = float2(cos(angle), sin(angle));
		const float3 direction = float3(screenDirection.x, -screenDirection.y, 0.0);

		// The slice's plane holds the view direction and the direction across the image; n is the angle of the
		// normal projected into it, from the view direction toward the slice's direction
		const float3 orthogonal = normalize(direction - view * dot(direction, view));
		const float3 axis = cross(orthogonal, view);
		const float3 projectedNormal = normal - axis * dot(normal, axis);
		const float projectedLength = length(projectedNormal);
		if (projectedLength < 1e-4)
			continue;
		const float cosNormal = clamp(dot(projectedNormal, view) / projectedLength, -1.0, 1.0);
		const float n = (dot(orthogonal, projectedNormal) >= 0.0 ? 1.0 : -1.0) * acos(cosNormal);

		// The highest horizon on each side, as the cosine of its angle from the view direction, starting from the
		// surface's own: the edge of the hemisphere around the normal
		float2 horizonCos = float2(cos(n + 0.5 * Pi), cos(n - 0.5 * Pi));
		const float2 lowestCos = horizonCos;
		for (uint step = 0; step < StepCount; ++step)
		{
			// Denser near the pixel, where occluders matter most
			float s = (float(step) + stepOffset) / float(StepCount);
			s = s * s;
			const float2 offset = screenDirection * max(s * radiusPixels, 1.0 + float(step));
			for (uint side = 0; side < 2; ++side)
			{
				const float2 samplePixel = input.Position.xy + (side == 0 ? offset : -offset);
				const float2 uv = samplePixel * g_Constants.ViewportSize.zw;
				if (any(uv < 0.0) || any(uv > 1.0))
					continue;
				const float sampleDepth = g_Depth.SampleLevel(g_PointClampSampler, uv, 0.0);
				const float3 delta = GetViewPosition(uv, sampleDepth) - position;
				const float distance = length(delta);
				if (distance < 1e-4)
					continue;
				// Farther occluders count for less, and those beyond the radius not at all
				const float weight = saturate((g_Constants.Radius - distance) / max(falloffRange, 1e-4));
				const float sampleCos = lerp(lowestCos[side], dot(delta / distance, view), weight);
				horizonCos[side] = max(horizonCos[side], sampleCos);
			}
		}

		// The horizons as angles - on the slice's side positive, on the other negative - kept within the hemisphere
		const float h1 = min(acos(clamp(horizonCos.x, -1.0, 1.0)), n + 0.5 * Pi);
		const float h0 = max(-acos(clamp(horizonCos.y, -1.0, 1.0)), n - 0.5 * Pi);
		visibility += projectedLength * (IntegrateArc(h0, n) + IntegrateArc(h1, n));
	}
	visibility = saturate(visibility / float(SliceCount));
	return pow(visibility, g_Constants.Intensity);
}

float PSBlur(FullscreenVertex input) : SV_Target
{
	const int2 pixel = int2(input.Position.xy);
	const int2 lastPixel = int2(g_Constants.ViewportSize.xy) - 1;
	const float centerDistance = GetDistance(g_Depth.Load(int3(pixel, 0)));
	// Pixels further apart in depth than this don't mix
	const float tolerance = 0.05 * centerDistance + 0.01;

	float sum = 0.0;
	float weight = 0.0;
	for (int y = -2; y < 2; ++y)
	{
		for (int x = -2; x < 2; ++x)
		{
			const int3 neighbor = int3(clamp(pixel + int2(x, y), 0, lastPixel), 0);
			const float distance = GetDistance(g_Depth.Load(neighbor));
			const float neighborWeight = saturate(1.0 - abs(distance - centerDistance) / tolerance);
			sum += g_Input.Load(neighbor).r * neighborWeight;
			weight += neighborWeight;
		}
	}
	return weight > 0.0 ? sum / weight : g_Input.Load(int3(pixel, 0)).r;
}
