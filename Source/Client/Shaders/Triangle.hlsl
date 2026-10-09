// A triangle with a red, a green and a blue corner, drawn without vertex buffers: the bring-up test for the rendering
// backend and the first reference image (see Lodestone/Graphics/TriangleRenderer.h)

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 Color : COLOR;
};

// NVRHI flips Vulkan's viewport, so clip space has +Y up, as in Direct3D: the red corner is at the top
static const float2 Positions[3] = {
	float2(0.0, 0.75),
	float2(0.75, -0.75),
	float2(-0.75, -0.75),
};

static const float3 Colors[3] = {
	float3(1.0, 0.0, 0.0),
	float3(0.0, 1.0, 0.0),
	float3(0.0, 0.0, 1.0),
};

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
	VertexOutput output;
	output.Position = float4(Positions[vertexId], 0.0, 1.0);
	output.Color = Colors[vertexId];
	return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
	return float4(input.Color, 1.0);
}
