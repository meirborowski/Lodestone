// Meshes with glTF's metallic-roughness materials, in each of the scene renderer's passes (see
// Lodestone/Renderer/SceneRenderer.cpp):
//   - Shadows: depth only, from the light (VSShadow, with PSShadowMasked for alpha-tested materials)
//   - Prepass: depth and view-space normals, which ambient occlusion reads (PSPrepass, PSPrepassMasked)
//   - Forward: the lit color, over the prepass's depth (PSForward, PSForwardMasked), and transparent surfaces blended
//     on top (PSForwardBlended)
// Skinned meshes use VSMainSkinned and VSShadowSkinned, which move each vertex by its joints, with the same pixel
// shaders

#include "Common.hlsli"
#include "Lighting.hlsli"

// Matches MaterialConstants in Lodestone/Renderer/ShaderTypes.h
struct MaterialConstants
{
	float4 BaseColor;
	// rgb: emitted light, w: unused
	float4 Emissive;
	float Metallic;
	float Roughness;
	float NormalScale;
	float OcclusionStrength;
	float AlphaCutoff;
	// MaterialFlags
	uint Flags;
	uint2 Padding;
};

static const uint MaterialHasNormalTexture = 1;

// Matches DrawConstants in Lodestone/Renderer/ShaderTypes.h
struct DrawConstants
{
	// The mesh's world matrix. In the shadow pass, the light's view-projection times the world matrix
	float4x4 World;
	// The rows of the inverse transpose of the world matrix's upper 3x3, for normals. The first row's w is the sign of
	// the world matrix's determinant: -1 when it mirrors the mesh, which flips its tangent space
	float4 NormalMatrix[3];
	// The range of g_LightIndices with the punctual lights that reach the mesh
	uint LightOffset;
	uint LightCount;
	// Skinned meshes: where their joints' matrices start in g_JointMatrices
	uint JointOffset;
};

[[vk::push_constant]] DrawConstants g_Draw;

// Descriptor set 3: the frame's joint matrices, from each skinned mesh's bind pose to the world
StructuredBuffer<float4x4> g_JointMatrices : register(t0, space3);

// Descriptor set 2: the material
ConstantBuffer<MaterialConstants> g_Material : register(b0, space2);
Texture2D<float4> g_BaseColorTexture : register(t0, space2);
Texture2D<float4> g_MetallicRoughnessTexture : register(t1, space2);
Texture2D<float4> g_NormalTexture : register(t2, space2);
Texture2D<float4> g_OcclusionTexture : register(t3, space2);
Texture2D<float4> g_EmissiveTexture : register(t4, space2);
SamplerState g_BaseColorSampler : register(s0, space2);
SamplerState g_MetallicRoughnessSampler : register(s1, space2);
SamplerState g_NormalSampler : register(s2, space2);
SamplerState g_OcclusionSampler : register(s3, space2);
SamplerState g_EmissiveSampler : register(s4, space2);

struct VertexInput
{
	[[vk::location(0)]] float3 Position : POSITION;
	[[vk::location(1)]] float3 Normal : NORMAL;
	[[vk::location(2)]] float4 Tangent : TANGENT;
	[[vk::location(3)]] float2 TexCoord : TEXCOORD0;
};

struct Vertex
{
	float4 Position : SV_Position;
	float3 WorldPosition : POSITION;
	float3 Normal : NORMAL;
	float4 Tangent : TANGENT;
	float2 TexCoord : TEXCOORD0;
};

// The skin vertex buffer: up to four joints and their weights, which add up to 1
struct SkinnedVertexInput
{
	[[vk::location(0)]] float3 Position : POSITION;
	[[vk::location(1)]] float3 Normal : NORMAL;
	[[vk::location(2)]] float4 Tangent : TANGENT;
	[[vk::location(3)]] float2 TexCoord : TEXCOORD0;
	[[vk::location(4)]] uint4 Joints : BLENDINDICES;
	[[vk::location(5)]] float4 Weights : BLENDWEIGHT;
};

struct ShadowVertexInput
{
	[[vk::location(0)]] float3 Position : POSITION;
	[[vk::location(1)]] float2 TexCoord : TEXCOORD0;
};

struct SkinnedShadowVertexInput
{
	[[vk::location(0)]] float3 Position : POSITION;
	[[vk::location(1)]] float2 TexCoord : TEXCOORD0;
	[[vk::location(2)]] uint4 Joints : BLENDINDICES;
	[[vk::location(3)]] float4 Weights : BLENDWEIGHT;
};

struct ShadowVertex
{
	float4 Position : SV_Position;
	float2 TexCoord : TEXCOORD0;
};

float3 TransformNormal(float3 normal)
{
	return float3(dot(g_Draw.NormalMatrix[0].xyz, normal), dot(g_Draw.NormalMatrix[1].xyz, normal),
		dot(g_Draw.NormalMatrix[2].xyz, normal));
}

// The prepass and the forward pass share this, so both compute the same depth for every pixel
Vertex VSMain(VertexInput input)
{
	const float4 world = mul(g_Draw.World, float4(input.Position, 1.0));
	Vertex output;
	output.Position = mul(g_Frame.ViewProjection, world);
	output.WorldPosition = world.xyz;
	output.Normal = TransformNormal(input.Normal);
	output.Tangent = float4(mul((float3x3)g_Draw.World, input.Tangent.xyz), input.Tangent.w * g_Draw.NormalMatrix[0].w);
	output.TexCoord = input.TexCoord;
	return output;
}

ShadowVertex VSShadow(ShadowVertexInput input)
{
	ShadowVertex output;
	output.Position = mul(g_Draw.World, float4(input.Position, 1.0));
	output.TexCoord = input.TexCoord;
	return output;
}

// A skinned vertex's matrix: its joints' matrices, weighted
float4x4 GetSkinMatrix(uint4 joints, float4 weights)
{
	const uint offset = g_Draw.JointOffset;
	return weights.x * g_JointMatrices[offset + joints.x] + weights.y * g_JointMatrices[offset + joints.y] +
		weights.z * g_JointMatrices[offset + joints.z] + weights.w * g_JointMatrices[offset + joints.w];
}

// A normal moved by a matrix: by the cofactor of its upper 3x3, which is its inverse transpose up to scale and exists
// even when the matrix flattens the mesh; turned around when the matrix mirrors, so it still faces out
float3 TransformNormalBy(float3x3 transform, float3 normal)
{
	const float3x3 columns = transpose(transform);
	const float3 moved = cross(columns[1], columns[2]) * normal.x + cross(columns[2], columns[0]) * normal.y +
		cross(columns[0], columns[1]) * normal.z;
	return determinant(transform) < 0.0 ? -moved : moved;
}

// Skinned meshes are placed by their joints: the draw's world matrix is the identity, and mirroring is the skin's
Vertex VSMainSkinned(SkinnedVertexInput input)
{
	const float4x4 skin = GetSkinMatrix(input.Joints, input.Weights);
	const float4 world = mul(skin, float4(input.Position, 1.0));
	const float3x3 skin3 = (float3x3)skin;
	Vertex output;
	output.Position = mul(g_Frame.ViewProjection, world);
	output.WorldPosition = world.xyz;
	output.Normal = TransformNormalBy(skin3, input.Normal);
	output.Tangent =
		float4(mul(skin3, input.Tangent.xyz), input.Tangent.w * (determinant(skin3) < 0.0 ? -1.0 : 1.0));
	output.TexCoord = input.TexCoord;
	return output;
}

ShadowVertex VSShadowSkinned(SkinnedShadowVertexInput input)
{
	ShadowVertex output;
	const float4 world = mul(GetSkinMatrix(input.Joints, input.Weights), float4(input.Position, 1.0));
	output.Position = mul(g_Draw.World, world);
	output.TexCoord = input.TexCoord;
	return output;
}

float SampleOpacity(float2 texCoord)
{
	return g_Material.BaseColor.a * g_BaseColorTexture.Sample(g_BaseColorSampler, texCoord).a;
}

void PSShadowMasked(ShadowVertex input)
{
	clip(SampleOpacity(input.TexCoord) - g_Material.AlphaCutoff);
}

// The interpolated normal, unit length and facing the camera's side of a double-sided surface
float3 GetGeometricNormal(Vertex input, bool frontFace)
{
	const float3 normal = normalize(input.Normal);
	return frontFace ? normal : -normal;
}

// The geometric normal perturbed by the material's normal texture, in world space
float3 GetShadingNormal(Vertex input, float3 geometricNormal, bool frontFace)
{
	if ((g_Material.Flags & MaterialHasNormalTexture) == 0)
		return geometricNormal;
	float3 tangent = input.Tangent.xyz - geometricNormal * dot(geometricNormal, input.Tangent.xyz);
	if (dot(tangent, tangent) < 1e-12)
		return geometricNormal;
	tangent = normalize(tangent);
	float3 bitangent = cross(geometricNormal, tangent) * input.Tangent.w;
	if (!frontFace)
	{
		// The geometric normal is already flipped; the texture's tangent space flips with it
		tangent = -tangent;
		bitangent = -bitangent;
	}
	float3 mapped = g_NormalTexture.Sample(g_NormalSampler, input.TexCoord).xyz * 2.0 - 1.0;
	mapped.xy *= g_Material.NormalScale;
	const float3 normal = mapped.x * tangent + mapped.y * bitangent + mapped.z * geometricNormal;
	return dot(normal, normal) > 1e-12 ? normalize(normal) : geometricNormal;
}

float4 WriteViewNormal(float3 worldNormal)
{
	return float4(normalize(mul((float3x3)g_Frame.View, worldNormal)), 1.0);
}

float4 PSPrepass(Vertex input, bool frontFace : SV_IsFrontFace) : SV_Target
{
	const float3 geometricNormal = GetGeometricNormal(input, frontFace);
	return WriteViewNormal(GetShadingNormal(input, geometricNormal, frontFace));
}

float4 PSPrepassMasked(Vertex input, bool frontFace : SV_IsFrontFace) : SV_Target
{
	clip(SampleOpacity(input.TexCoord) - g_Material.AlphaCutoff);
	const float3 geometricNormal = GetGeometricNormal(input, frontFace);
	return WriteViewNormal(GetShadingNormal(input, geometricNormal, frontFace));
}

float4 Shade(Vertex input, bool frontFace, float opacity, bool screenSpaceOcclusion)
{
	const float4 baseColor = g_Material.BaseColor * g_BaseColorTexture.Sample(g_BaseColorSampler, input.TexCoord);
	// glTF packs roughness in green and metalness in blue
	const float4 metallicRoughness = g_MetallicRoughnessTexture.Sample(g_MetallicRoughnessSampler, input.TexCoord);
	const float3 geometricNormal = GetGeometricNormal(input, frontFace);
	const float3 normal = GetShadingNormal(input, geometricNormal, frontFace);

	Surface surface = MakeSurface(input.WorldPosition, normal, baseColor.rgb,
		saturate(g_Material.Metallic * metallicRoughness.b), saturate(g_Material.Roughness * metallicRoughness.g));
	const float occlusion = g_OcclusionTexture.Sample(g_OcclusionSampler, input.TexCoord).r;
	surface.Occlusion = 1.0 + g_Material.OcclusionStrength * (occlusion - 1.0);
	surface.Emissive = g_Material.Emissive.rgb * g_EmissiveTexture.Sample(g_EmissiveSampler, input.TexCoord).rgb;
	surface.Opacity = opacity;

	float screenOcclusion = 1.0;
	if (screenSpaceOcclusion && g_Frame.AmbientOcclusion.x > 0.0)
		screenOcclusion = g_AmbientOcclusionTexture.Load(int3(input.Position.xy, 0));
	const float viewDepth = -mul(g_Frame.View, float4(input.WorldPosition, 1.0)).z;
	const float3 color = ShadeSurface(surface, geometricNormal, viewDepth, input.Position.xy, g_Draw.LightOffset,
		g_Draw.LightCount, screenOcclusion);
	return float4(color, opacity);
}

float4 PSForward(Vertex input, bool frontFace : SV_IsFrontFace) : SV_Target
{
	return Shade(input, frontFace, 1.0, true);
}

float4 PSForwardMasked(Vertex input, bool frontFace : SV_IsFrontFace) : SV_Target
{
	// The prepass left the depth of what's behind where this clips, so it must clip the same pixels
	clip(SampleOpacity(input.TexCoord) - g_Material.AlphaCutoff);
	return Shade(input, frontFace, 1.0, true);
}

float4 PSForwardBlended(Vertex input, bool frontFace : SV_IsFrontFace) : SV_Target
{
	// Transparent surfaces aren't in the prepass, so the screen's ambient occlusion is of what's behind them
	return Shade(input, frontFace, saturate(SampleOpacity(input.TexCoord)), false);
}
