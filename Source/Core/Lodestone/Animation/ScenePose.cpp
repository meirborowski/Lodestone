#include "Lodestone/Animation/ScenePose.h"

#include "Lodestone/Animation/Animation.h"
#include "Lodestone/Scene/RenderComponents.h"
#include "Lodestone/Scene/Scene.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace Lodestone {

	ScenePose::ScenePose(const Scene& scene, PoseOptions options)
		: m_Scene(&scene),
		  m_Options(std::move(options)),
		  m_SecondsBehind(
			  (1.0f - std::clamp(m_Options.InterpolationFactor, 0.0f, 1.0f)) * std::max(m_Options.TickDuration, 0.0f))
	{
		if (!m_Options.Models)
			return;
		const entt::registry& registry = scene.GetRegistry();
		for (const auto [handle, animator] : registry.view<const AnimatorComponent>().each())
		{
			const UUID id = registry.get<IDComponent>(handle).ID;
			if (m_Options.Preview && m_Options.Preview->Entity == id)
			{
				// The preview's clip at its time, still, keeping how the animator loops
				AnimatorComponent preview = animator;
				Play(preview, m_Options.Preview->Clip);
				preview.Time = m_Options.Preview->Time;
				preview.Playing = false;
				PoseAnimator(handle, preview);
			}
			else
			{
				PoseAnimator(handle, animator);
			}
		}
	}

	TransformComponent ScenePose::GetLocalTransform(entt::entity entity) const
	{
		if (const auto found = m_Animated.find(entity); found != m_Animated.end())
			return found->second;
		return m_Scene->GetInterpolatedTransform(entity, m_Options.InterpolationFactor);
	}

	glm::mat4 ScenePose::GetWorldMatrix(entt::entity entity) const
	{
		// Up to the root, or the nearest ancestor already worked out, then down again
		const entt::registry& registry = m_Scene->GetRegistry();
		std::vector<entt::entity> chain;
		glm::mat4 world(1.0f);
		for (entt::entity current = entity; current != entt::null;)
		{
			if (const auto found = m_WorldMatrices.find(current); found != m_WorldMatrices.end())
			{
				world = found->second;
				break;
			}
			chain.push_back(current);
			const UUID parent = registry.get<HierarchyComponent>(current).Parent;
			current = parent.IsNil() ? entt::entity(entt::null) : m_Scene->FindHandle(parent);
		}
		for (const entt::entity link : std::views::reverse(chain))
		{
			world = world * GetLocalTransform(link).GetMatrix();
			m_WorldMatrices.emplace(link, world);
		}
		return world;
	}

	std::vector<glm::mat4> ScenePose::GetJointMatrices(entt::entity entity) const
	{
		const entt::registry& registry = m_Scene->GetRegistry();
		const auto* skin = registry.try_get<SkinComponent>(entity);
		const auto* renderer = registry.try_get<MeshRendererComponent>(entity);
		if (skin == nullptr || renderer == nullptr)
			return {};
		const entt::entity root = FindInstanceRoot(entity);
		if (root == entt::null || registry.get<ModelInstanceComponent>(root).Model != renderer->Model)
			return {};
		const Instance& instance = GetInstance(root);
		if (instance.Asset == nullptr || skin->Skin >= instance.Asset->Skins.size() ||
			renderer->Mesh >= instance.Asset->Meshes.size())
			return {};
		const ModelSkin& modelSkin = instance.Asset->Skins[skin->Skin];
		const ModelMesh& mesh = instance.Asset->Meshes[renderer->Mesh];
		if (!mesh.IsSkinned() || mesh.SkinJointCount > modelSkin.Joints.size())
			return {};

		std::vector<glm::mat4> matrices;
		matrices.reserve(modelSkin.Joints.size());
		for (size_t joint = 0; joint < modelSkin.Joints.size(); ++joint)
		{
			const uint32_t node = modelSkin.Joints[joint];
			if (node >= instance.Nodes.size() || instance.Nodes[node] == entt::null)
				return {};
			const glm::mat4 inverseBind =
				joint < modelSkin.InverseBindMatrices.size() ? modelSkin.InverseBindMatrices[joint] : glm::mat4(1.0f);
			matrices.push_back(GetWorldMatrix(instance.Nodes[node]) * inverseBind);
		}
		return matrices;
	}

	const ScenePose::Instance& ScenePose::GetInstance(entt::entity root) const
	{
		auto [found, inserted] = m_Instances.try_emplace(root);
		Instance& instance = found->second;
		if (!inserted)
			return instance;
		const entt::registry& registry = m_Scene->GetRegistry();
		const auto* component = registry.try_get<ModelInstanceComponent>(root);
		if (component == nullptr || !m_Options.Models)
			return instance;
		instance.Asset = m_Options.Models(component->Model);
		if (instance.Asset == nullptr)
			return instance;

		// The root's descendants with a ModelNode, down to the roots of other instances. The first entity found for a
		// node is the one used
		instance.Nodes.assign(instance.Asset->Nodes.size(), entt::null);
		std::vector<entt::entity> stack = {root};
		while (!stack.empty())
		{
			const entt::entity current = stack.back();
			stack.pop_back();
			if (const auto* node = registry.try_get<ModelNodeComponent>(current);
				node != nullptr && node->Node < instance.Nodes.size() && instance.Nodes[node->Node] == entt::null)
				instance.Nodes[node->Node] = current;
			for (const UUID child : std::views::reverse(registry.get<HierarchyComponent>(current).Children))
			{
				const entt::entity handle = m_Scene->FindHandle(child);
				if (handle != entt::null && !registry.all_of<ModelInstanceComponent>(handle))
					stack.push_back(handle);
			}
		}
		return instance;
	}

	entt::entity ScenePose::FindInstanceRoot(entt::entity entity) const
	{
		const entt::registry& registry = m_Scene->GetRegistry();
		for (entt::entity current = entity; current != entt::null;)
		{
			if (registry.all_of<ModelInstanceComponent>(current))
				return current;
			const UUID parent = registry.get<HierarchyComponent>(current).Parent;
			current = parent.IsNil() ? entt::entity(entt::null) : m_Scene->FindHandle(parent);
		}
		return entt::null;
	}

	void ScenePose::PoseAnimator(entt::entity root, const AnimatorComponent& animator)
	{
		if (!m_Scene->GetRegistry().all_of<ModelInstanceComponent>(root))
			return;
		const Instance& instance = GetInstance(root);
		if (instance.Asset == nullptr || animator.Clip >= instance.Asset->Animations.size())
			return;
		const AnimationClip& clip = instance.Asset->Animations[animator.Clip];
		const AnimationClip* fading =
			animator.FadeDuration > 0.0f && animator.FadeClip < instance.Asset->Animations.size()
			? &instance.Asset->Animations[animator.FadeClip]
			: nullptr;

		// The rest pose: each node's transform in the scene. A playing animator is drawn as far behind its latest time
		// as the view is behind the latest tick
		std::vector<TransformComponent> pose(instance.Nodes.size());
		for (size_t node = 0; node < pose.size(); ++node)
		{
			if (instance.Nodes[node] != entt::null)
				pose[node] = m_Scene->GetInterpolatedTransform(instance.Nodes[node], m_Options.InterpolationFactor);
		}
		const float behind = animator.Playing ? m_SecondsBehind : 0.0f;
		const float weight = GetFadeWeight(animator, animator.FadeElapsed - behind);
		std::vector<TransformComponent> fadingPose;
		if (fading != nullptr && weight < 1.0f)
			fadingPose = pose;
		ApplyClip(clip, GetClipTime(clip, animator.Time - behind * animator.Speed, animator.Loop), pose);
		if (!fadingPose.empty())
		{
			ApplyClip(*fading, GetClipTime(*fading, animator.FadeTime - behind * animator.Speed, true), fadingPose);
			BlendPose(fadingPose, pose, weight);
		}

		// Only the nodes the clips animate are moved
		std::vector<bool> animated(pose.size(), false);
		const auto mark = [&animated](const AnimationClip& marked)
		{
			for (const AnimationChannel& channel : marked.Channels)
			{
				if (channel.Node < animated.size())
					animated[channel.Node] = true;
			}
		};
		mark(clip);
		if (!fadingPose.empty())
			mark(*fading);
		for (size_t node = 0; node < pose.size(); ++node)
		{
			if (animated[node] && instance.Nodes[node] != entt::null)
				m_Animated[instance.Nodes[node]] = pose[node];
		}
	}

}
