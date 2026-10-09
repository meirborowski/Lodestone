// The editor's debug view of a scene until the scene renderer arrives (Milestone 5): a shaded cube for each entity.
// The geometry comes from the vertex index, so no vertex buffers are needed (see Lodestone/Graphics/DebugSceneRenderer.h)

struct CubeConstants
{
	float4x4 WorldViewProjection;
	float4 Color;
	// The light's direction in the cube's own space, so the cube is lit without a normal matrix
	float4 LightDirection;
};

[[vk::push_constant]] CubeConstants g_Cube;

// A unit cube centered on the origin, two triangles per face. Faces aren't culled, so their winding doesn't matter
static const float3 Positions[36] = {
	// +X
	float3(0.5, -0.5, -0.5), float3(0.5, 0.5, -0.5), float3(0.5, 0.5, 0.5),
	float3(0.5, -0.5, -0.5), float3(0.5, 0.5, 0.5), float3(0.5, -0.5, 0.5),
	// -X
	float3(-0.5, -0.5, 0.5), float3(-0.5, 0.5, 0.5), float3(-0.5, 0.5, -0.5),
	float3(-0.5, -0.5, 0.5), float3(-0.5, 0.5, -0.5), float3(-0.5, -0.5, -0.5),
	// +Y
	float3(-0.5, 0.5, -0.5), float3(-0.5, 0.5, 0.5), float3(0.5, 0.5, 0.5),
	float3(-0.5, 0.5, -0.5), float3(0.5, 0.5, 0.5), float3(0.5, 0.5, -0.5),
	// -Y
	float3(-0.5, -0.5, 0.5), float3(-0.5, -0.5, -0.5), float3(0.5, -0.5, -0.5),
	float3(-0.5, -0.5, 0.5), float3(0.5, -0.5, -0.5), float3(0.5, -0.5, 0.5),
	// +Z
	float3(0.5, -0.5, 0.5), float3(0.5, 0.5, 0.5), float3(-0.5, 0.5, 0.5),
	float3(0.5, -0.5, 0.5), float3(-0.5, 0.5, 0.5), float3(-0.5, -0.5, 0.5),
	// -Z
	float3(-0.5, -0.5, -0.5), float3(-0.5, 0.5, -0.5), float3(0.5, 0.5, -0.5),
	float3(-0.5, -0.5, -0.5), float3(0.5, 0.5, -0.5), float3(0.5, -0.5, -0.5),
};

static const float3 Normals[6] = {
	float3(1.0, 0.0, 0.0), float3(-1.0, 0.0, 0.0),
	float3(0.0, 1.0, 0.0), float3(0.0, -1.0, 0.0),
	float3(0.0, 0.0, 1.0), float3(0.0, 0.0, -1.0),
};

struct Vertex
{
	float4 Position : SV_Position;
	float3 Normal : NORMAL;
};

Vertex VSMain(uint vertexId : SV_VertexID)
{
	Vertex output;
	output.Position = mul(g_Cube.WorldViewProjection, float4(Positions[vertexId], 1.0));
	output.Normal = Normals[vertexId / 6];
	return output;
}

float4 PSMain(Vertex input) : SV_Target
{
	// Ambient plus a directional light, so neighboring faces show different shades
	const float diffuse = saturate(dot(normalize(input.Normal), -normalize(g_Cube.LightDirection.xyz)));
	return float4(g_Cube.Color.rgb * (0.35 + 0.65 * diffuse), g_Cube.Color.a);
}
