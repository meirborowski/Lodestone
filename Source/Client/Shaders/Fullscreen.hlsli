// A triangle that covers the whole image, for passes that run a pixel shader on every pixel (Fullscreen.hlsl). It needs
// no vertex buffer: draw three vertices

#ifndef LODESTONE_FULLSCREEN_HLSLI
#define LODESTONE_FULLSCREEN_HLSLI

struct FullscreenVertex
{
	float4 Position : SV_Position;
	// From (0, 0) at the image's top left to (1, 1) at its bottom right
	float2 TexCoord : TEXCOORD0;
};

FullscreenVertex MakeFullscreenVertex(uint vertexId, float depth)
{
	const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
	FullscreenVertex output;
	output.Position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0);
	output.TexCoord = uv;
	return output;
}

#endif
