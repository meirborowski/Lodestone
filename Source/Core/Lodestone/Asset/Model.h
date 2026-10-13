#pragma once

#include "Lodestone/Core/BoundingBox.h"
#include "Lodestone/Core/Image.h"
#include "Lodestone/Core/UUID.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Lodestone {

	// A model as imported from a glTF file, in CPU memory: meshes, materials, the textures they use, and the node
	// hierarchy that places the meshes. It doesn't need a GPU, so the renderer, physics and the editor's picking can
	// all use it (see docs/Decisions/0019-model-import.md)

	struct MeshVertex
	{
		glm::vec3 Position{0.0f};
		glm::vec3 Normal{0.0f, 0.0f, 1.0f};
		// xyz is the tangent; w is +1 or -1, the handedness of the bitangent: cross(Normal, Tangent.xyz) * w
		glm::vec4 Tangent{1.0f, 0.0f, 0.0f, 1.0f};
		glm::vec2 TexCoord{0.0f};
	};

	// The joints that move a vertex of a skinned mesh, and how much each does. Joints are indices into the joints of
	// the skin the mesh is drawn with (ModelSkin::Joints); the weights add up to 1
	struct SkinVertex
	{
		std::array<uint16_t, 4> Joints{};
		glm::vec4 Weights{1.0f, 0.0f, 0.0f, 0.0f};
	};

	// The bounds, in a mesh's space (its bind pose), of the vertices a joint of its skin moves
	struct MeshJointBounds
	{
		uint16_t Joint = 0;
		BoundingBox Bounds;
	};

	// Part of a mesh drawn with one material: a range of the mesh's indices
	struct MeshPrimitive
	{
		uint32_t FirstIndex = 0;
		uint32_t IndexCount = 0;
		// Into Model::Materials
		uint32_t Material = 0;
		BoundingBox Bounds;
	};

	// Triangles, indexed, with one vertex array for all of its primitives
	struct ModelMesh
	{
		std::string Name;
		std::vector<MeshVertex> Vertices;
		std::vector<uint32_t> Indices;
		std::vector<MeshPrimitive> Primitives;
		BoundingBox Bounds;

		// For meshes that can be skinned: a SkinVertex for each vertex, or none
		std::vector<SkinVertex> Skin;
		// The bounds of the vertices each joint moves, for the joints the skin weights refer to. Moved by the joints'
		// matrices, they bound the skinned mesh whatever the pose
		std::vector<MeshJointBounds> JointBounds;
		// How many joints a skin needs to skin the mesh: one more than the highest joint the weights refer to
		uint32_t SkinJointCount = 0;

		bool IsSkinned() const { return !Skin.empty(); }
	};

	enum class AlphaMode : uint8_t
	{
		// Alpha is ignored
		Opaque,
		// Pixels with alpha below the cutoff are discarded
		Mask,
		// Blended over what's behind, drawn after opaque surfaces
		Blend,
	};

	enum class TextureWrap : uint8_t
	{
		Repeat,
		ClampToEdge,
		MirroredRepeat,
	};

	// A texture as a material uses it: an image, and how to sample it
	struct ModelTexture
	{
		// Into Model::Images
		uint32_t Image = 0;
		TextureWrap WrapU = TextureWrap::Repeat;
		TextureWrap WrapV = TextureWrap::Repeat;
		// Nearest-neighbour filtering instead of linear, for pixel art
		bool Nearest = false;
	};

	// The metallic-roughness PBR material of glTF 2.0. Textures are indices into Model::Textures
	struct ModelMaterial
	{
		std::string Name;
		// Linear RGB and alpha
		glm::vec4 BaseColor{1.0f};
		float Metallic = 1.0f;
		float Roughness = 1.0f;
		// Linear RGB, with KHR_materials_emissive_strength applied
		glm::vec3 Emissive{0.0f};
		float NormalScale = 1.0f;
		float OcclusionStrength = 1.0f;
		AlphaMode Alpha = AlphaMode::Opaque;
		float AlphaCutoff = 0.5f;
		bool DoubleSided = false;

		// sRGB
		std::optional<uint32_t> BaseColorTexture;
		// Linear: roughness in green, metalness in blue
		std::optional<uint32_t> MetallicRoughnessTexture;
		// Linear, tangent space
		std::optional<uint32_t> NormalTexture;
		// Linear: occlusion in red
		std::optional<uint32_t> OcclusionTexture;
		// sRGB
		std::optional<uint32_t> EmissiveTexture;
	};

	// A node of the model's hierarchy: a transform relative to its parent, and optionally a mesh
	struct ModelNode
	{
		std::string Name;
		glm::vec3 Position{0.0f};
		glm::quat Rotation = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec3 Scale{1.0f};
		// Into Model::Meshes
		std::optional<uint32_t> Mesh;
		// Into Model::Skins: the skin the node's mesh is drawn with. The node's own transform doesn't move a skinned
		// mesh - its joints do
		std::optional<uint32_t> Skin;
		// Into Model::Nodes
		std::vector<uint32_t> Children;
	};

	// The skeleton a mesh is skinned with: the nodes that are its joints, and for each, the matrix from the mesh's
	// space to the joint's in the bind pose. A vertex's skinned position is the weighted sum, over its joints, of
	// jointWorld * InverseBindMatrices[joint] * position
	struct ModelSkin
	{
		std::string Name;
		// Into Model::Nodes
		std::vector<uint32_t> Joints;
		std::vector<glm::mat4> InverseBindMatrices;
	};

	enum class AnimationPath : uint8_t
	{
		Translation,
		Rotation,
		Scale,
	};

	enum class AnimationInterpolation : uint8_t
	{
		// Each keyframe's value holds until the next
		Step,
		// Straight between keyframes; rotations spherically
		Linear,
		// Hermite splines through the keyframes, with tangents
		CubicSpline,
	};

	// One property of one node over time
	struct AnimationChannel
	{
		// Into Model::Nodes
		uint32_t Node = 0;
		AnimationPath Path = AnimationPath::Translation;
		AnimationInterpolation Interpolation = AnimationInterpolation::Linear;
		// Keyframe times in seconds, increasing
		std::vector<float> Times;
		// A value for each keyframe: xyz for translations and scales, an xyzw quaternion for rotations. Cubic splines
		// have three per keyframe: the in-tangent, the value and the out-tangent
		std::vector<glm::vec4> Values;
	};

	// An animation of the model's nodes
	struct AnimationClip
	{
		std::string Name;
		// The last keyframe's time, in seconds
		float Duration = 0.0f;
		std::vector<AnimationChannel> Channels;
	};

	struct Model
	{
		std::vector<ModelMesh> Meshes;
		std::vector<ModelMaterial> Materials;
		std::vector<ModelTexture> Textures;
		// Decoded RGBA8 images
		std::vector<Image> Images;
		std::vector<ModelNode> Nodes;
		// The nodes at the top of the hierarchy, in order
		std::vector<uint32_t> RootNodes;
		std::vector<ModelSkin> Skins;
		std::vector<AnimationClip> Animations;

		// Every mesh's bounds, placed by the hierarchy, in the model's space
		BoundingBox GetBounds() const;
	};

	// A model asset by its UUID, if it's loaded; null otherwise. What the renderer, picking and animation are given to
	// read models with
	using ModelLookup = std::function<const Model*(UUID model)>;

}
