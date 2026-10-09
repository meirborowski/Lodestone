// Dear ImGui's triangles: textured, vertex-colored and alpha-blended (see Lodestone/Graphics/ImGuiRenderer.h)

struct Constants
{
	// From ImGui's display coordinates (pixels, top-left origin) to clip space
	float4x4 Projection;
};

[[vk::push_constant]] Constants g_Constants;

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct VertexInput
{
	float2 Position : POSITION;
	float2 TexCoord : TEXCOORD0;
	float4 Color : COLOR0;
};

struct Vertex
{
	float4 Position : SV_Position;
	float2 TexCoord : TEXCOORD0;
	float4 Color : COLOR0;
};

Vertex VSMain(VertexInput input)
{
	Vertex output;
	output.Position = mul(g_Constants.Projection, float4(input.Position, 0.0, 1.0));
	output.TexCoord = input.TexCoord;
	output.Color = input.Color;
	return output;
}

float4 PSMain(Vertex input) : SV_Target
{
	return input.Color * g_Texture.Sample(g_Sampler, input.TexCoord);
}
