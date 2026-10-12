// Physically based shading: the metallic-roughness BRDF of glTF (GGX microfacets, height-correlated Smith visibility,
// Schlick Fresnel, Lambert diffuse), directional and punctual lights, soft shadows from cascaded shadow maps, and
// image-based lighting from the environment

#ifndef LODESTONE_LIGHTING_HLSLI
#define LODESTONE_LIGHTING_HLSLI

#include "Common.hlsli"

// Below this, highlights alias to single pixels
static const float MinRoughness = 0.045;
static const uint ShadowSampleCount = 16;
// The widest a shadow's penumbra gets, in shadow map texels, which bounds the blocker search too
static const float MaxShadowFilterTexels = 24.0;
// How far above a receiver the blocker search looks for casters, in meters
static const float MaxBlockerDistance = 8.0;

struct Surface
{
	float3 Position;
	// Unit vectors in world space: the shading normal and the direction toward the camera
	float3 Normal;
	float3 View;
	float NDotV;
	float3 DiffuseColor;
	// Reflectance at normal incidence
	float3 F0;
	// Perceptual roughness, and its square, which the BRDF uses
	float Roughness;
	float Alpha;
	// Ambient occlusion from the material
	float Occlusion;
	float3 Emissive;
	float Opacity;
};

Surface MakeSurface(float3 position, float3 normal, float3 baseColor, float metallic, float roughness)
{
	Surface surface;
	surface.Position = position;
	surface.Normal = normal;
	surface.View = normalize(g_Frame.CameraPosition.xyz - position);
	// Normals that face away from the camera, from interpolation or normal maps, would shade black
	surface.NDotV = max(dot(normal, surface.View), 1e-4);
	surface.DiffuseColor = baseColor * (1.0 - metallic);
	surface.F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
	surface.Roughness = clamp(roughness, MinRoughness, 1.0);
	surface.Alpha = surface.Roughness * surface.Roughness;
	surface.Occlusion = 1.0;
	surface.Emissive = 0.0;
	surface.Opacity = 1.0;
	return surface;
}

float DistributionGgx(float nDotH, float alpha)
{
	const float a2 = alpha * alpha;
	const float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
	return a2 / (Pi * d * d);
}

// The Smith visibility term for GGX, height-correlated, with the BRDF's 4 n.l n.v denominator folded in
float VisibilitySmithGgxCorrelated(float nDotV, float nDotL, float alpha)
{
	const float a2 = alpha * alpha;
	const float lambdaV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
	const float lambdaL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
	return 0.5 / max(lambdaV + lambdaL, 1e-6);
}

float3 FresnelSchlick(float3 f0, float cosine)
{
	const float f = pow(1.0 - saturate(cosine), 5.0);
	return f0 + (1.0 - f0) * f;
}

// The light a surface reflects toward the camera from light arriving from direction l with the given radiance (or,
// for directional lights, irradiance)
float3 EvaluateLight(Surface surface, float3 l, float3 radiance)
{
	const float nDotL = dot(surface.Normal, l);
	if (nDotL <= 0.0)
		return 0.0;
	const float3 h = normalize(l + surface.View);
	const float nDotH = saturate(dot(surface.Normal, h));
	const float vDotH = saturate(dot(surface.View, h));
	const float3 fresnel = FresnelSchlick(surface.F0, vDotH);
	const float distribution = DistributionGgx(nDotH, surface.Alpha);
	const float visibility = VisibilitySmithGgxCorrelated(surface.NDotV, nDotL, surface.Alpha);
	const float3 specular = fresnel * (distribution * visibility);
	const float3 diffuse = (1.0 - fresnel) * surface.DiffuseColor / Pi;
	return (diffuse + specular) * radiance * nDotL;
}

// A point or spot light: inverse-square falloff, windowed to reach zero at the range (Karis 2013), and the spot's
// cone as in glTF's KHR_lights_punctual
float3 EvaluatePunctualLight(Surface surface, PunctualLight light)
{
	const float3 toLight = light.PositionRange.xyz - surface.Position;
	const float distanceSquared = max(dot(toLight, toLight), 1e-4);
	const float range = light.PositionRange.w;
	const float ratio = distanceSquared / (range * range);
	const float window = saturate(1.0 - ratio * ratio);
	const float3 l = toLight * rsqrt(distanceSquared);
	const float cone = saturate(dot(light.DirectionAngleOffset.xyz, -l) * light.ColorAngleScale.w +
		light.DirectionAngleOffset.w);
	const float attenuation = window * window / distanceSquared * cone * cone;
	if (attenuation <= 0.0)
		return 0.0;
	return EvaluateLight(surface, l, light.ColorAngleScale.rgb * attenuation);
}

// Points spread evenly over the unit disk (a Vogel spiral), turned by a rotation
float2 VogelDisk(uint index, uint count, float rotation)
{
	const float GoldenAngle = 2.39996323;
	const float radius = sqrt((float(index) + 0.5) / float(count));
	const float angle = float(index) * GoldenAngle + rotation;
	return radius * float2(cos(angle), sin(angle));
}

// How much of the shadowed directional light reaches a point, from 0 to 1. Percentage-closer soft shadows (Fernando
// 2005): a blocker search estimates how far the casters are from the point, which sets how wide a region the
// percentage-closer filter averages - so shadows are sharp where casters touch receivers and soften with distance.
// The sample pattern turns from pixel to pixel, trading banding for fine noise
float SampleShadow(float3 position, float3 geometricNormal, float viewDepth, float2 pixel)
{
	const uint cascadeCount = g_Frame.Counts.y;
	if (cascadeCount == 0 || viewDepth >= g_Frame.Shadow.w)
		return 1.0;
	uint cascade = 0;
	while (cascade + 1 < cascadeCount && viewDepth > g_Frame.CascadeSplits[cascade])
		++cascade;

	const float texelSize = g_Frame.CascadeTexelSizes[cascade];
	const float depthRange = g_Frame.CascadeDepthRanges[cascade];
	// Moving the point off the surface along its normal keeps it from shadowing itself (normal offset shadows)
	const float3 offsetPosition = position + geometricNormal * (texelSize * 1.5);
	const float4 clip = mul(g_Frame.CascadeViewProjections[cascade], float4(offsetPosition, 1.0));
	const float2 uv = clip.xy * float2(0.5, -0.5) + 0.5;
	// A small constant bias in depth, against the rest of the acne: half a texel's size, in the cascade's depth units
	const float depth = clip.z - 0.5 * texelSize / depthRange;
	if (any(uv < 0.0) || any(uv > 1.0) || depth >= 1.0)
		return 1.0;

	const float lightSize = g_Frame.Shadow.x;
	const float uvPerTexel = g_Frame.Shadow.y;
	const float rotation = InterleavedGradientNoise(pixel) * 2.0 * Pi;

	// The average depth of the casters between the point and the light
	const float searchTexels = clamp(lightSize * MaxBlockerDistance / texelSize, 1.0, MaxShadowFilterTexels);
	float blockerDepthSum = 0.0;
	float blockerCount = 0.0;
	for (uint i = 0; i < ShadowSampleCount; ++i)
	{
		const float2 offset = VogelDisk(i, ShadowSampleCount, rotation) * (searchTexels * uvPerTexel);
		const float stored = g_ShadowMap.SampleLevel(g_PointClampSampler, float3(uv + offset, cascade), 0.0);
		if (stored < depth)
		{
			blockerDepthSum += stored;
			blockerCount += 1.0;
		}
	}
	if (blockerCount == 0.0)
		return 1.0;

	// The penumbra widens with the distance from the casters to the point
	const float blockerDistance = (depth - blockerDepthSum / blockerCount) * depthRange;
	const float filterTexels = clamp(blockerDistance * lightSize / texelSize, 1.0, MaxShadowFilterTexels);
	float lit = 0.0;
	for (uint j = 0; j < ShadowSampleCount; ++j)
	{
		const float2 offset = VogelDisk(j, ShadowSampleCount, rotation) * (filterTexels * uvPerTexel);
		lit += g_ShadowMap.SampleCmpLevelZero(g_ShadowSampler, float3(uv + offset, cascade), depth);
	}
	const float shadow = lit / float(ShadowSampleCount);

	// Shadows fade out toward the end of the shadow distance, rather than stopping at a line
	const float fade = saturate((viewDepth - g_Frame.Shadow.z) / max(g_Frame.Shadow.w - g_Frame.Shadow.z, 1e-4));
	return lerp(shadow, 1.0, fade);
}

// The environment's light reflected toward the camera: diffuse from the irradiance, specular from the prefiltered
// map and the split-sum lookup table (Karis 2013). Ambient occlusion darkens both, the specular part by an
// approximation of how much of the reflection it hides (Lagarde and de Rousiers 2014)
float3 EvaluateImageBasedLighting(Surface surface, float ambientOcclusion)
{
	const float3 reflected = reflect(-surface.View, surface.Normal);
	const float lod = surface.Roughness * g_Frame.Environment.w;
	const float3 prefiltered =
		g_PrefilteredEnvironment.SampleLevel(g_LinearClampSampler, ToEnvironmentDirection(reflected), lod).rgb *
		g_Frame.Environment.x;
	const float2 brdf = g_BrdfLut.SampleLevel(g_LinearClampSampler, float2(surface.NDotV, surface.Roughness), 0.0);
	const float3 specularReflectance = surface.F0 * brdf.x + brdf.y;

	const float3 diffuse = surface.DiffuseColor * EvaluateIrradiance(surface.Normal) * (1.0 - specularReflectance);
	const float3 specular = prefiltered * specularReflectance;
	const float specularOcclusion = saturate(
		pow(surface.NDotV + ambientOcclusion, exp2(-16.0 * surface.Roughness - 1.0)) - 1.0 + ambientOcclusion);
	return diffuse * ambientOcclusion + specular * specularOcclusion;
}

// All the light a surface sends toward the camera
float3 ShadeSurface(Surface surface, float3 geometricNormal, float viewDepth, float2 pixel, uint lightOffset,
	uint lightCount, float screenAmbientOcclusion)
{
	float3 color = surface.Emissive;
	for (uint i = 0; i < g_Frame.Counts.x; ++i)
	{
		const DirectionalLight light = g_Frame.DirectionalLights[i];
		float3 irradiance = light.Color.rgb;
		if (i == g_Frame.Counts.z)
			irradiance *= SampleShadow(surface.Position, geometricNormal, viewDepth, pixel);
		color += EvaluateLight(surface, -light.Direction.xyz, irradiance);
	}
	for (uint j = 0; j < lightCount; ++j)
		color += EvaluatePunctualLight(surface, g_PunctualLights[g_LightIndices[lightOffset + j]]);
	color += EvaluateImageBasedLighting(surface, min(surface.Occlusion, screenAmbientOcclusion));
	return color;
}

#endif
