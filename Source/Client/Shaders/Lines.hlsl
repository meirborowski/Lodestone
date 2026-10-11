// Colored line segments over a rendered scene - the editor's grid, markers and selection, debug drawing - with the
// scene's reversed depth (see Lodestone/Renderer/LineRenderer.h)

// Matches LineConstants in Lodestone/Renderer/LineRenderer.cpp
struct LineConstants
{
	float4x4 ViewProjection;
};

[[vk::push_constant]] LineConstants g_Constants;

struct VertexInput
{
	[[vk::location(0)]] float3 Position : POSITION;
	// sRGB-encoded, like the image the lines are drawn on
	[[vk::location(1)]] float4 Color : COLOR;
};

struct Vertex
{
	float4 Position : SV_Position;
	float4 Color : COLOR;
};

Vertex VSMain(VertexInput input)
{
	Vertex output;
	output.Position = mul(g_Constants.ViewProjection, float4(input.Position, 1.0));
	output.Color = input.Color;
	return output;
}

float4 PSMain(Vertex input) : SV_Target
{
	return input.Color;
}
