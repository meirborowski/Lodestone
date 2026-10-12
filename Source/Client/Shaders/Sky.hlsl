// The environment map as the sky, behind everything else: a triangle covering the image at the far plane, so the depth
// test lets it through only where nothing was drawn

#include "Common.hlsli"
#include "Fullscreen.hlsli"

float4 PSMain(FullscreenVertex input) : SV_Target
{
	const float2 ndc = float2(input.TexCoord.x * 2.0 - 1.0, 1.0 - input.TexCoord.y * 2.0);
	const float4 world = mul(g_Frame.InverseViewProjection, float4(ndc, 0.5, 1.0));
	const float3 direction = normalize(world.xyz / world.w - g_Frame.CameraPosition.xyz);
	const float3 environmentDirection = ToEnvironmentDirection(direction);
	const float3 radiance = g_EnvironmentMap.SampleLevel(g_LinearClampSampler, environmentDirection, 0.0).rgb;
	return float4(radiance * g_Frame.Environment.x, 1.0);
}
