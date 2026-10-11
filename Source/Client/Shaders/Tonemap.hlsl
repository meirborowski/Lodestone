// Turns the scene's HDR color into the 8-bit image that's shown: exposure, a tonemapping curve that brings any
// brightness into the displayable range, and the sRGB transfer function

#include "Fullscreen.hlsli"

// Matches TonemapConstants in Lodestone/Renderer/ShaderTypes.h
struct TonemapConstants
{
	// The factor the color is multiplied by: 2 to the power of the exposure in stops
	float Exposure;
	// A Tonemapper (Lodestone/Scene/RenderComponents.h)
	uint Tonemapper;
	float2 Padding;
};

static const uint TonemapperAces = 0;
static const uint TonemapperNeutral = 1;

[[vk::push_constant]] TonemapConstants g_Constants;

Texture2D<float4> g_Color : register(t0);

// Stephen Hill's fit of the ACES reference rendering and output transforms, for sRGB displays
float3 TonemapAces(float3 color)
{
	static const float3x3 InputMatrix = {
		0.59719, 0.35458, 0.04823,
		0.07600, 0.90834, 0.01566,
		0.02840, 0.13383, 0.83777,
	};
	static const float3x3 OutputMatrix = {
		1.60475, -0.53108, -0.07367,
		-0.10208, 1.10813, -0.00605,
		-0.00327, -0.07276, 1.07602,
	};
	color = mul(InputMatrix, color);
	const float3 a = color * (color + 0.0245786) - 0.000090537;
	const float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
	return saturate(mul(OutputMatrix, a / b));
}

// Khronos PBR Neutral: leaves colors up to a brightness unchanged, so materials look as authored, and compresses
// highlights above it toward white
float3 TonemapNeutral(float3 color)
{
	const float StartCompression = 0.8 - 0.04;
	const float Desaturation = 0.15;

	const float lowest = min(color.r, min(color.g, color.b));
	const float offset = lowest < 0.08 ? lowest - 6.25 * lowest * lowest : 0.04;
	color -= offset;
	const float peak = max(color.r, max(color.g, color.b));
	if (peak < StartCompression)
		return saturate(color);
	const float d = 1.0 - StartCompression;
	const float newPeak = 1.0 - d * d / (peak + d - StartCompression);
	color *= newPeak / peak;
	const float g = 1.0 - 1.0 / (Desaturation * (peak - newPeak) + 1.0);
	return saturate(lerp(color, newPeak, g));
}

float3 LinearToSrgb(float3 color)
{
	return select(color <= 0.0031308, color * 12.92, 1.055 * pow(color, 1.0 / 2.4) - 0.055);
}

float4 PSMain(FullscreenVertex input) : SV_Target
{
	const float3 hdr = max(g_Color.Load(int3(input.Position.xy, 0)).rgb, 0.0) * g_Constants.Exposure;
	float3 color;
	if (g_Constants.Tonemapper == TonemapperAces)
		color = TonemapAces(hdr);
	else if (g_Constants.Tonemapper == TonemapperNeutral)
		color = TonemapNeutral(hdr);
	else
		color = saturate(hdr);
	return float4(LinearToSrgb(color), 1.0);
}
