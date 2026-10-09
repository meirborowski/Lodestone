// The ground grid of the editor's debug view of a scene (see Lodestone/Graphics/DebugSceneRenderer.h): lines on the XZ
// plane, with the X and Z axes in their own colors. The geometry comes from the vertex index

struct GridConstants
{
	float4x4 ViewProjection;
	float4 Color;
	float4 AxisColorX;
	float4 AxisColorZ;
	// Lines are this far apart, and there are this many on each side of the origin
	float Spacing;
	uint HalfLineCount;
};

[[vk::push_constant]] GridConstants g_Grid;

struct Vertex
{
	float4 Position : SV_Position;
	float4 Color : COLOR;
};

// Lines along X, then lines along Z, two vertices each
Vertex VSMain(uint vertexId : SV_VertexID)
{
	const uint lineCount = g_Grid.HalfLineCount * 2 + 1;
	const uint lineIndex = vertexId / 2;
	const bool alongX = lineIndex < lineCount;
	const int offsetIndex = int(lineIndex % lineCount) - int(g_Grid.HalfLineCount);
	const float offset = float(offsetIndex) * g_Grid.Spacing;
	const float end = (vertexId % 2 == 0 ? -1.0 : 1.0) * float(g_Grid.HalfLineCount) * g_Grid.Spacing;

	Vertex output;
	const float3 position = alongX ? float3(end, 0.0, offset) : float3(offset, 0.0, end);
	output.Position = mul(g_Grid.ViewProjection, float4(position, 1.0));
	output.Color = offsetIndex != 0 ? g_Grid.Color : (alongX ? g_Grid.AxisColorX : g_Grid.AxisColorZ);
	return output;
}

float4 PSMain(Vertex input) : SV_Target
{
	return input.Color;
}
