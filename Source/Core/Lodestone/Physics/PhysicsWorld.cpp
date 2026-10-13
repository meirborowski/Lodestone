#include "Lodestone/Physics/PhysicsWorld.h"

#include "Lodestone/Core/Assert.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Input/InputCommand.h"
#include "Lodestone/Physics/BodyIdAllocator.h"
#include "Lodestone/Physics/JoltRuntime.h"
#include "Lodestone/Physics/PhysicsComponents.h"
#include "Lodestone/Physics/PhysicsLayers.h"
#include "Lodestone/Physics/StateBuffer.h"
#include "Lodestone/Scene/Components.h"
#include "Lodestone/Scene/Scene.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Geometry/AABox.h>
#include <Jolt/Geometry/IndexedTriangle.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollector.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace Lodestone {

	namespace {

		// Scales and extents are kept at least this far from zero, which Jolt can't represent
		constexpr float MinExtent = 1.0e-4f;
		// Stick deflection below this is ignored, so a resting stick doesn't drift
		constexpr float StickDeadZone = 0.2f;
		constexpr JPH::uint MaxPhysicsJobs = 2048;
		constexpr JPH::uint MaxPhysicsBarriers = 8;

		constexpr uint32_t MaxDefaultWorkerThreads = 4;
		// Bodies this close to one that moves or goes away are woken, so the ones resting on it are found
		constexpr float WakeMargin = 0.1f;
		constexpr glm::quat Identity = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
		constexpr glm::vec3 Up(0.0f, 1.0f, 0.0f);

		enum class BodyKind : uint8_t
		{
			Body = 0,
			Character = 1,
		};

		enum class ColliderShape : uint8_t
		{
			Box = 0,
			Sphere = 1,
			Capsule = 2,
			Mesh = 3,
			ConvexMesh = 4,
		};

		// Where an entity is in the world: positions and rotations composed through the hierarchy, and scales
		// multiplied (a rotated child of a non-uniformly scaled parent is approximated, as in most engines)
		struct Pose
		{
			glm::vec3 Position{0.0f};
			glm::quat Rotation = Identity;
			glm::vec3 Scale{1.0f};
		};

		// One collider of a body, relative to the body's position and rotation
		struct ColliderPart
		{
			// The entity the collider is on, which orders a body's colliders
			uint32_t Entity = 0;
			ColliderShape Shape = ColliderShape::Box;
			// A box's size; a sphere's radius in X; a capsule's radius in X and height in Y
			glm::vec3 Size{0.0f};
			glm::vec3 Center{0.0f};
			UUID Model;
			uint32_t Mesh = 0;
			// A dynamic body's mesh collider asked for triangles, and gets the convex hull instead
			bool HullForDynamic = false;
			// The collider's entity, relative to the body
			glm::vec3 Position{0.0f};
			glm::quat Rotation = Identity;
			glm::vec3 Scale{1.0f};

			bool operator==(const ColliderPart& other) const = default;
		};

		// Everything Jolt needs to make an entity's body or character, from its components
		struct BodyDescription
		{
			BodyKind Kind = BodyKind::Body;
			BodyType Type = BodyType::Static;
			bool Trigger = false;
			std::vector<ColliderPart> Parts;
			float Mass = 1.0f;
			float Friction = RigidBodyComponent{}.Friction;
			float Restitution = RigidBodyComponent{}.Restitution;
			float LinearDamping = RigidBodyComponent{}.LinearDamping;
			float AngularDamping = RigidBodyComponent{}.AngularDamping;
			float GravityFactor = 1.0f;
			bool ContinuousCollision = false;
			bool AllowSleeping = true;
			// Characters
			float Height = 0.0f;
			float Radius = 0.0f;
			float MaxSlope = 0.0f;
			float PushStrength = 0.0f;

			bool operator==(const BodyDescription& other) const = default;
		};

		// A body or character in the world, and what physics last did with its entity
		struct BodyRecord
		{
			entt::entity Entity = entt::null;
			// For events about the entity after it's gone
			UUID EntityId;
			// A Jolt BodyID: a character's is its inner body's, which is also its CharacterID
			uint32_t Id = 0;
			BodyDescription Description;
			// Dynamic bodies and characters: the local position and rotation, and the velocities, physics last wrote
			// to the entity, to tell when something else changed them. Static and kinematic bodies: the world
			// position and rotation they were last moved to
			glm::vec3 Position{0.0f};
			glm::quat Rotation = Identity;
			glm::vec3 LinearVelocity{0.0f};
			glm::vec3 AngularVelocity{0.0f};
			// Not part of the saved state: Jolt's character, for characters
			JPH::Ref<JPH::CharacterVirtual> Character;
		};

		// Two bodies touching, by Jolt body ID, the lower first
		struct ContactPair
		{
			uint32_t First = 0;
			uint32_t Second = 0;
			bool Trigger = false;
			// The entities, for the end event after one is gone; for a trigger, the trigger first
			UUID FirstEntity;
			UUID SecondEntity;

			std::pair<uint32_t, uint32_t> GetKey() const { return {First, Second}; }
		};

		using BodyPair = std::pair<uint32_t, uint32_t>;

		BodyPair MakePair(uint32_t first, uint32_t second)
		{
			return first < second ? BodyPair(first, second) : BodyPair(second, first);
		}

		// Collects the pairs of bodies that start touching during a step. Jolt calls it from several threads, in no
		// particular order, so the pairs are sorted afterwards
		class ContactCollector final : public JPH::ContactListener
		{
		public:
			void OnContactAdded(const JPH::Body& first, const JPH::Body& second,
				[[maybe_unused]] const JPH::ContactManifold& manifold,
				[[maybe_unused]] JPH::ContactSettings& settings) override
			{
				const std::scoped_lock lock(m_Mutex);
				m_Added.push_back(
					MakePair(first.GetID().GetIndexAndSequenceNumber(), second.GetID().GetIndexAndSequenceNumber()));
			}

			// The pairs added since the last call, sorted and without duplicates
			std::vector<BodyPair> TakeAdded()
			{
				const std::scoped_lock lock(m_Mutex);
				std::vector<BodyPair> added = std::move(m_Added);
				m_Added.clear();
				std::ranges::sort(added);
				const auto duplicates = std::ranges::unique(added);
				added.erase(duplicates.begin(), duplicates.end());
				return added;
			}

		private:
			std::mutex m_Mutex;
			std::vector<BodyPair> m_Added;
		};

		// The closest ray hit; between hits at the same distance, the lowest body ID, so the result doesn't depend
		// on the order the broad phase finds bodies in
		class ClosestHitCollector final : public JPH::CastRayCollector
		{
		public:
			void AddHit(const JPH::RayCastResult& hit) override
			{
				if (!m_Hit || IsBefore(hit, *m_Hit))
					m_Hit = hit;
			}

			const std::optional<JPH::RayCastResult>& GetHit() const { return m_Hit; }

			static bool IsBefore(const JPH::RayCastResult& first, const JPH::RayCastResult& second)
			{
				return std::tuple(first.mFraction, first.mBodyID.GetIndexAndSequenceNumber()) <
					std::tuple(second.mFraction, second.mBodyID.GetIndexAndSequenceNumber());
			}

		private:
			std::optional<JPH::RayCastResult> m_Hit;
		};

		// The closest hit on each body a ray passes through
		class EveryBodyCollector final : public JPH::CastRayCollector
		{
		public:
			void AddHit(const JPH::RayCastResult& hit) override
			{
				const auto [found, inserted] = m_Hits.try_emplace(hit.mBodyID.GetIndexAndSequenceNumber(), hit);
				if (!inserted && hit.mFraction < found->second.mFraction)
					found->second = hit;
			}

			// Nearest first
			std::vector<JPH::RayCastResult> TakeHits()
			{
				std::vector<JPH::RayCastResult> hits;
				hits.reserve(m_Hits.size());
				for (const auto& [id, hit] : m_Hits)
					hits.push_back(hit);
				std::ranges::sort(hits, ClosestHitCollector::IsBefore);
				return hits;
			}

		private:
			std::map<uint32_t, JPH::RayCastResult> m_Hits;
		};

		// Exact comparisons for telling whether something changed a value since physics wrote it: bit for bit, so
		// a NaN compares equal to itself and doesn't look like a change every tick
		bool IsSame(float first, float second)
		{
			return std::bit_cast<uint32_t>(first) == std::bit_cast<uint32_t>(second);
		}

		bool IsSame(const glm::vec3& first, const glm::vec3& second)
		{
			return IsSame(first.x, second.x) && IsSame(first.y, second.y) && IsSame(first.z, second.z);
		}

		bool IsSame(const glm::quat& first, const glm::quat& second)
		{
			return IsSame(first.x, second.x) && IsSame(first.y, second.y) && IsSame(first.z, second.z) &&
				IsSame(first.w, second.w);
		}

		glm::quat SafeNormalize(const glm::quat& rotation)
		{
			const float length = glm::length(rotation);
			if (!std::isfinite(length) || length < 1.0e-6f)
				return Identity;
			return rotation / length;
		}

		glm::vec3 SafeScale(const glm::vec3& scale)
		{
			glm::vec3 safe;
			for (glm::length_t axis = 0; axis < 3; ++axis)
				safe[axis] = std::abs(scale[axis]) < MinExtent ? std::copysign(MinExtent, scale[axis]) : scale[axis];
			return safe;
		}

		bool IsFinite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool IsFinite(const Pose& pose)
		{
			return IsFinite(pose.Position) && IsFinite(pose.Scale) && std::isfinite(pose.Rotation.x) &&
				std::isfinite(pose.Rotation.y) && std::isfinite(pose.Rotation.z) && std::isfinite(pose.Rotation.w);
		}

		Pose Compose(const Pose& parent, const TransformComponent& local)
		{
			return {
				.Position = parent.Position + parent.Rotation * (parent.Scale * local.Position),
				.Rotation = SafeNormalize(parent.Rotation * local.Rotation),
				.Scale = parent.Scale * local.Scale,
			};
		}

		// A world position and rotation relative to a parent
		std::pair<glm::vec3, glm::quat> ToLocal(
			const Pose& parent, const glm::vec3& position, const glm::quat& rotation)
		{
			const glm::quat inverse = glm::inverse(parent.Rotation);
			return {
				(inverse * (position - parent.Position)) / SafeScale(parent.Scale), SafeNormalize(inverse * rotation)};
		}

		// The rotation around the vertical axis that faces the way the rotation does, as an angle in radians
		float GetYaw(const glm::quat& rotation)
		{
			const glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
			if (forward.x * forward.x + forward.z * forward.z < 1.0e-12f)
				return 0.0f;
			return std::atan2(-forward.x, -forward.z);
		}

		float ApplyDeadZone(float value)
		{
			const float magnitude = std::abs(value);
			if (magnitude < StickDeadZone)
				return 0.0f;
			return std::copysign(std::min((magnitude - StickDeadZone) / (1.0f - StickDeadZone), 1.0f), value);
		}

		glm::vec2 ApplyDeadZone(const glm::vec2& stick)
		{
			const float length = glm::length(stick);
			if (!std::isfinite(length) || length < StickDeadZone)
				return glm::vec2(0.0f);
			return stick / length * std::min((length - StickDeadZone) / (1.0f - StickDeadZone), 1.0f);
		}

		// Where the player wants to walk: X to the right, Y forward, at most 1 long
		glm::vec2 ReadMovement(const InputCommand& input)
		{
			glm::vec2 movement(0.0f);
			if (input.IsKeyDown(Key::W) || input.IsKeyDown(Key::Up))
				movement.y += 1.0f;
			if (input.IsKeyDown(Key::S) || input.IsKeyDown(Key::Down))
				movement.y -= 1.0f;
			if (input.IsKeyDown(Key::D) || input.IsKeyDown(Key::Right))
				movement.x += 1.0f;
			if (input.IsKeyDown(Key::A) || input.IsKeyDown(Key::Left))
				movement.x -= 1.0f;
			if (input.GamepadConnected)
				// Down is positive on sticks
				movement += ApplyDeadZone(
					glm::vec2(input.GetGamepadAxis(GamepadAxis::LeftX), -input.GetGamepadAxis(GamepadAxis::LeftY)));
			const float length = glm::length(movement);
			return length > 1.0f ? movement / length : movement;
		}

		JPH::EMotionType ToMotionType(BodyType type)
		{
			switch (type)
			{
				case BodyType::Static:
					return JPH::EMotionType::Static;
				case BodyType::Kinematic:
					return JPH::EMotionType::Kinematic;
				case BodyType::Dynamic:
					return JPH::EMotionType::Dynamic;
			}
			return JPH::EMotionType::Static;
		}

		JPH::ObjectLayer GetLayer(const BodyDescription& description)
		{
			if (description.Trigger)
				return PhysicsLayers::Trigger;
			return description.Type == BodyType::Static ? PhysicsLayers::Static : PhysicsLayers::Moving;
		}

		BodyDescription DescribeRigidBody(const RigidBodyComponent& body)
		{
			return {
				.Kind = BodyKind::Body,
				.Type = static_cast<BodyType>(std::min(body.Type, static_cast<uint32_t>(BodyType::Dynamic))),
				.Trigger = body.IsTrigger,
				.Mass = std::max(body.Mass, MinExtent),
				.Friction = std::max(body.Friction, 0.0f),
				.Restitution = std::clamp(body.Restitution, 0.0f, 1.0f),
				.LinearDamping = std::max(body.LinearDamping, 0.0f),
				.AngularDamping = std::max(body.AngularDamping, 0.0f),
				.GravityFactor = body.GravityFactor,
				.ContinuousCollision = body.ContinuousCollision,
				.AllowSleeping = body.AllowSleeping,
			};
		}

		BodyDescription DescribeCharacter(const CharacterControllerComponent& character)
		{
			return {
				.Kind = BodyKind::Character,
				.Type = BodyType::Kinematic,
				.Mass = std::max(character.Mass, MinExtent),
				.Height = std::max(character.Height, 0.01f),
				.Radius = std::max(character.Radius, 0.005f),
				.MaxSlope = std::clamp(character.MaxSlope, 0.0f, 89.0f),
				.PushStrength = std::max(character.PushStrength, 0.0f),
			};
		}

		// Whether a body must be made again to change into the description, rather than updated
		// A character's capsule radius: at most half its height, below which it's a sphere
		float GetCharacterRadius(const BodyDescription& description)
		{
			return std::min(description.Radius, 0.5f * description.Height);
		}

		bool MustRecreate(const BodyDescription& current, const BodyDescription& wanted)
		{
			if (current.Kind != wanted.Kind)
				return true;
			if (current.Kind == BodyKind::Character)
				return current.Height != wanted.Height || current.Radius != wanted.Radius;
			return current.Type != wanted.Type || current.Trigger != wanted.Trigger;
		}

		// Writes and reads the saved state, field by field, so padding never reaches it
		void Write(StateBuffer& stream, const UUID& id)
		{
			stream.Write(id.GetHigh());
			stream.Write(id.GetLow());
		}

		void Read(StateBuffer& stream, UUID& id)
		{
			uint64_t high = 0;
			uint64_t low = 0;
			stream.Read(high);
			stream.Read(low);
			id = UUID(high, low);
		}

		void Write(StateBuffer& stream, const glm::vec3& value)
		{
			stream.Write(value.x);
			stream.Write(value.y);
			stream.Write(value.z);
		}

		void Read(StateBuffer& stream, glm::vec3& value)
		{
			stream.Read(value.x);
			stream.Read(value.y);
			stream.Read(value.z);
		}

		void Write(StateBuffer& stream, const glm::quat& value)
		{
			stream.Write(value.x);
			stream.Write(value.y);
			stream.Write(value.z);
			stream.Write(value.w);
		}

		void Read(StateBuffer& stream, glm::quat& value)
		{
			stream.Read(value.x);
			stream.Read(value.y);
			stream.Read(value.z);
			stream.Read(value.w);
		}

		template <typename Enum>
		void WriteEnum(StateBuffer& stream, Enum value)
		{
			stream.Write(std::to_underlying(value));
		}

		template <typename Enum>
		void ReadEnum(StateBuffer& stream, Enum& value)
		{
			std::underlying_type_t<Enum> raw{};
			stream.Read(raw);
			value = static_cast<Enum>(raw);
		}

		void Write(StateBuffer& stream, const ColliderPart& part)
		{
			stream.Write(part.Entity);
			WriteEnum(stream, part.Shape);
			Write(stream, part.Size);
			Write(stream, part.Center);
			Write(stream, part.Model);
			stream.Write(part.Mesh);
			stream.Write(part.HullForDynamic);
			Write(stream, part.Position);
			Write(stream, part.Rotation);
			Write(stream, part.Scale);
		}

		void Read(StateBuffer& stream, ColliderPart& part)
		{
			stream.Read(part.Entity);
			ReadEnum(stream, part.Shape);
			Read(stream, part.Size);
			Read(stream, part.Center);
			Read(stream, part.Model);
			stream.Read(part.Mesh);
			stream.Read(part.HullForDynamic);
			Read(stream, part.Position);
			Read(stream, part.Rotation);
			Read(stream, part.Scale);
		}

		void Write(StateBuffer& stream, const BodyDescription& description)
		{
			WriteEnum(stream, description.Kind);
			WriteEnum(stream, description.Type);
			stream.Write(description.Trigger);
			stream.Write(static_cast<uint32_t>(description.Parts.size()));
			for (const ColliderPart& part : description.Parts)
				Write(stream, part);
			stream.Write(description.Mass);
			stream.Write(description.Friction);
			stream.Write(description.Restitution);
			stream.Write(description.LinearDamping);
			stream.Write(description.AngularDamping);
			stream.Write(description.GravityFactor);
			stream.Write(description.ContinuousCollision);
			stream.Write(description.AllowSleeping);
			stream.Write(description.Height);
			stream.Write(description.Radius);
			stream.Write(description.MaxSlope);
			stream.Write(description.PushStrength);
		}

		void Read(StateBuffer& stream, BodyDescription& description, uint32_t maxParts)
		{
			ReadEnum(stream, description.Kind);
			ReadEnum(stream, description.Type);
			stream.Read(description.Trigger);
			uint32_t partCount = 0;
			stream.Read(partCount);
			description.Parts.resize(std::min(partCount, maxParts));
			for (ColliderPart& part : description.Parts)
				Read(stream, part);
			stream.Read(description.Mass);
			stream.Read(description.Friction);
			stream.Read(description.Restitution);
			stream.Read(description.LinearDamping);
			stream.Read(description.AngularDamping);
			stream.Read(description.GravityFactor);
			stream.Read(description.ContinuousCollision);
			stream.Read(description.AllowSleeping);
			stream.Read(description.Height);
			stream.Read(description.Radius);
			stream.Read(description.MaxSlope);
			stream.Read(description.PushStrength);
		}

		uint32_t GetDefaultWorkerThreads()
		{
			const uint32_t threads = std::thread::hardware_concurrency();
			return std::min(threads > 1 ? threads - 1 : 0, MaxDefaultWorkerThreads);
		}

	}

	class PhysicsWorld::Implementation
	{
	public:
		Implementation(Scene& scene, PhysicsWorldConfig config);
		~Implementation();

		Implementation(const Implementation&) = delete;
		Implementation& operator=(const Implementation&) = delete;
		Implementation(Implementation&&) = delete;
		Implementation& operator=(Implementation&&) = delete;

		void Update(SimulationContext& context);
		void SaveState(std::vector<std::byte>& state) const;
		void RestoreState(std::span<const std::byte> state);
		// Brings Jolt in line with the components. With the tick's length, kinematic bodies move to their entities
		// over the tick; without, everything is placed where its entity is
		void Sync(std::optional<float> deltaTime);

		std::span<const PhysicsEvent> GetEvents() const { return m_Events; }
		std::vector<RaycastHit> Raycast(
			const glm::vec3& origin, const glm::vec3& direction, const RaycastOptions& options, bool all) const;
		uint32_t GetBodyCount() const { return m_System.GetNumBodies(); }
		bool HasBody(UUID entity) const;
		bool IsSleeping(UUID entity) const;

	private:
		// Caches the world poses of entities during a sync, which colliders and bodies are placed by
		class PoseCache
		{
		public:
			explicit PoseCache(const Scene& scene)
				: m_Scene(&scene)
			{
			}

			const Pose& Get(entt::entity entity);

		private:
			const Scene* m_Scene;
			std::unordered_map<entt::entity, Pose> m_Poses;
		};

	private:
		entt::entity GetParent(entt::entity entity) const;
		// The entity's world pose, without caching, for entities whose ancestors physics may just have moved
		Pose GetWorldPose(entt::entity entity) const;

		void ApplyGravity();
		std::map<entt::entity, BodyDescription> DescribeBodies(PoseCache& poses) const;
		// The nearest ancestor-or-self with a RigidBody, which the entity's colliders belong to; the entity itself
		// when there's none; nothing below a character
		std::optional<entt::entity> FindColliderOwner(entt::entity entity) const;

		void CreateBody(entt::entity entity, const BodyDescription& description, PoseCache& poses);
		// Puts a body or character with a known ID into Jolt, from its record's description. Returns false if Jolt
		// refused it
		bool AddToJolt(BodyRecord& record, const glm::vec3& position, const glm::quat& rotation,
			const glm::vec3& linearVelocity, const glm::vec3& angularVelocity);
		void RemoveFromJolt(BodyRecord& record);
		void UpdateBody(BodyRecord& record, const BodyDescription& description);
		void SyncPose(BodyRecord& record, PoseCache& poses, std::optional<float> deltaTime);
		// Wakes the bodies around a body: before it's moved, reshaped or removed, so bodies resting on it don't stay
		// asleep in the air, and after, so bodies it now overlaps respond
		void WakeAround(uint32_t id);
		// The characters in the order of their IDs, for character-vs-character collision
		void RefreshCharacterList();

		void MoveCharacters(SimulationContext& context);
		void MoveCharacter(BodyRecord& record, const CharacterControllerComponent& component, const InputCommand& input,
			float deltaTime);
		void WriteBack();
		void GatherEvents();

		JPH::RefConst<JPH::Shape> BuildBodyShape(const BodyRecord& record) const;
		JPH::RefConst<JPH::Shape> BuildPartShape(const ColliderPart& part, const BodyRecord& record) const;
		JPH::RefConst<JPH::Shape> GetMeshShape(UUID model, uint32_t mesh, bool convex) const;
		JPH::RefConst<JPH::Shape> BuildCharacterShape(const BodyDescription& description) const;
		JPH::MassProperties GetMassProperties(const BodyDescription& description, const JPH::Shape& shape) const;

		const BodyRecord* FindRecord(uint32_t id) const;
		std::string DescribeEntity(const BodyRecord& record) const;

	private:
		Scene* m_Scene;
		PhysicsWorldConfig m_Config;
		PhysicsLayers m_Layers;
		Scope<JPH::TempAllocator> m_TempAllocator;
		Scope<JPH::JobSystemThreadPool> m_JobSystem;
		ContactCollector m_Contacts;
		JPH::CharacterVsCharacterCollisionSimple m_CharacterCollision;
		JPH::PhysicsSystem m_System;
		BodyIdAllocator m_Ids;
		// By body index, so they're visited in the same order every run. Declared after the system: characters
		// remove their inner bodies from it when they're destroyed
		std::map<uint32_t, BodyRecord> m_Bodies;
		// Body indices by entity, for lookups only: unordered, so never iterated
		std::unordered_map<entt::entity, uint32_t> m_BodyOfEntity;
		// Touching pairs, sorted
		std::vector<ContactPair> m_Pairs;
		// Pairs characters collided with during the latest tick, sorted
		std::vector<BodyPair> m_CharacterContacts;
		std::vector<PhysicsEvent> m_Events;
		// Shapes of meshes by model, mesh and convexity, including failures (null), which are reported once
		mutable std::map<std::tuple<UUID, uint32_t, bool>, JPH::RefConst<JPH::Shape>> m_MeshShapes;
		JPH::EPhysicsUpdateError m_LastUpdateError = JPH::EPhysicsUpdateError::None;
	};

	PhysicsWorld::Implementation::Implementation(Scene& scene, PhysicsWorldConfig config)
		: m_Scene(&scene),
		  m_Config(std::move(config)),
		  m_Ids(std::clamp(m_Config.MaxBodies, 1u, BodyIdAllocator::MaxCapacity))
	{
		LS_CORE_ASSERT(m_Config.MaxBodies > 0 && m_Config.MaxBodyPairs > 0 && m_Config.MaxContactConstraints > 0,
			"A physics world needs room for bodies, pairs and contacts");
#if defined(JPH_DISABLE_TEMP_ALLOCATOR)
		m_TempAllocator = CreateScope<JPH::TempAllocatorMalloc>();
#else
		m_TempAllocator = CreateScope<JPH::TempAllocatorImplWithMallocFallback>(std::max(m_Config.TempMemory, 1024u));
#endif
		const uint32_t workers = m_Config.WorkerThreads.value_or(GetDefaultWorkerThreads());
		m_JobSystem =
			CreateScope<JPH::JobSystemThreadPool>(MaxPhysicsJobs, MaxPhysicsBarriers, static_cast<int>(workers));
		m_System.Init(std::clamp(m_Config.MaxBodies, 1u, BodyIdAllocator::MaxCapacity), 0, m_Config.MaxBodyPairs,
			m_Config.MaxContactConstraints, m_Layers.GetBroadPhaseLayers(), m_Layers.GetObjectVsBroadPhaseFilter(),
			m_Layers.GetObjectLayerPairFilter());
		m_System.SetContactListener(&m_Contacts);
		m_System.SetGravity(ToJolt(PhysicsSettingsComponent{}.Gravity));
	}

	PhysicsWorld::Implementation::~Implementation()
	{
		// Characters remove their inner bodies from the system, so they go first
		for (auto& [index, record] : m_Bodies)
			RemoveFromJolt(record);
		m_Bodies.clear();
	}

	const Pose& PhysicsWorld::Implementation::PoseCache::Get(entt::entity entity)
	{
		if (const auto found = m_Poses.find(entity); found != m_Poses.end())
			return found->second;

		// Up to the nearest ancestor already known (or the root), then down again: no recursion, however deep the
		// hierarchy
		const entt::registry& registry = m_Scene->GetRegistry();
		std::vector<entt::entity> chain;
		for (entt::entity current = entity; current != entt::null && !m_Poses.contains(current);)
		{
			chain.push_back(current);
			current = m_Scene->FindHandle(registry.get<HierarchyComponent>(current).Parent);
		}
		for (auto link = chain.rbegin(); link != chain.rend(); ++link)
		{
			const entt::entity parent = m_Scene->FindHandle(registry.get<HierarchyComponent>(*link).Parent);
			const auto& transform = registry.get<TransformComponent>(*link);
			m_Poses[*link] = parent == entt::null ? Compose(Pose{}, transform) : Compose(m_Poses.at(parent), transform);
		}
		return m_Poses.at(entity);
	}

	entt::entity PhysicsWorld::Implementation::GetParent(entt::entity entity) const
	{
		return m_Scene->FindHandle(m_Scene->GetRegistry().get<HierarchyComponent>(entity).Parent);
	}

	Pose PhysicsWorld::Implementation::GetWorldPose(entt::entity entity) const
	{
		PoseCache poses(*m_Scene);
		return poses.Get(entity);
	}

	void PhysicsWorld::Implementation::ApplyGravity()
	{
		// The first PhysicsSettings in the scene, or the defaults
		glm::vec3 gravity = PhysicsSettingsComponent{}.Gravity;
		const auto settings = m_Scene->GetRegistry().view<const PhysicsSettingsComponent>();
		if (!settings.empty())
			gravity = settings.get<const PhysicsSettingsComponent>(settings.front()).Gravity;
		if (!IsFinite(gravity))
			gravity = glm::vec3(0.0f);
		if (IsSame(ToGlm(m_System.GetGravity()), gravity))
			return;
		m_System.SetGravity(ToJolt(gravity));
		// Jolt leaves sleeping bodies asleep when gravity changes
		JPH::BodyInterface& bodies = m_System.GetBodyInterface();
		for (const auto& [index, record] : m_Bodies)
			if (record.Description.Kind == BodyKind::Body && record.Description.Type == BodyType::Dynamic)
				bodies.ActivateBody(JPH::BodyID(record.Id));
	}

	std::optional<entt::entity> PhysicsWorld::Implementation::FindColliderOwner(entt::entity entity) const
	{
		const entt::registry& registry = m_Scene->GetRegistry();
		for (entt::entity current = entity; current != entt::null; current = GetParent(current))
		{
			if (registry.all_of<CharacterControllerComponent>(current))
				return std::nullopt;
			if (registry.all_of<RigidBodyComponent>(current))
				return current;
		}
		return entity;
	}

	std::map<entt::entity, BodyDescription> PhysicsWorld::Implementation::DescribeBodies(PoseCache& poses) const
	{
		const entt::registry& registry = m_Scene->GetRegistry();
		std::map<entt::entity, BodyDescription> bodies;
		for (const auto [entity, body] : registry.view<const RigidBodyComponent>().each())
			bodies.emplace(entity, DescribeRigidBody(body));
		// A character is a capsule of its own: a RigidBody beside it is ignored
		for (const auto [entity, character] : registry.view<const CharacterControllerComponent>().each())
			bodies.insert_or_assign(entity, DescribeCharacter(character));

		const auto addCollider = [&](entt::entity entity, ColliderPart part)
		{
			const std::optional<entt::entity> owner = FindColliderOwner(entity);
			if (!owner)
				return;
			// Colliders without a body above them are a static body of their own
			BodyDescription& body = bodies.try_emplace(*owner).first->second;
			// Placed relative to the body by the local transforms between them, from the body's own scale down -
			// not by world poses, whose rounding would move the colliders a little whenever the body moves
			std::vector<entt::entity> chain;
			for (entt::entity current = entity; current != *owner; current = GetParent(current))
				chain.push_back(current);
			Pose placement{.Scale = poses.Get(*owner).Scale};
			for (auto link = chain.rbegin(); link != chain.rend(); ++link)
				placement = Compose(placement, registry.get<TransformComponent>(*link));
			part.Entity = entt::to_integral(entity);
			part.Position = placement.Position;
			part.Rotation = placement.Rotation;
			part.Scale = placement.Scale;
			body.Parts.push_back(part);
		};
		for (const auto [entity, box] : registry.view<const BoxColliderComponent>().each())
			addCollider(entity, {.Shape = ColliderShape::Box, .Size = box.Size, .Center = box.Center});
		for (const auto [entity, sphere] : registry.view<const SphereColliderComponent>().each())
			addCollider(entity,
				{.Shape = ColliderShape::Sphere,
					.Size = glm::vec3(sphere.Radius, 0.0f, 0.0f),
					.Center = sphere.Center});
		for (const auto [entity, capsule] : registry.view<const CapsuleColliderComponent>().each())
			addCollider(entity,
				{.Shape = ColliderShape::Capsule,
					.Size = glm::vec3(capsule.Radius, capsule.Height, 0.0f),
					.Center = capsule.Center});
		for (const auto [entity, mesh] : registry.view<const MeshColliderComponent>().each())
			addCollider(entity,
				{.Shape = mesh.Convex ? ColliderShape::ConvexMesh : ColliderShape::Mesh,
					.Model = mesh.Model,
					.Mesh = mesh.Mesh});

		for (auto& [entity, body] : bodies)
		{
			// The same order however the components were added, so reordering storage doesn't rebuild shapes
			std::ranges::sort(
				body.Parts, {}, [](const ColliderPart& part) { return std::pair(part.Entity, part.Shape); });
			if (body.Kind != BodyKind::Body || body.Type != BodyType::Dynamic)
				continue;
			for (ColliderPart& part : body.Parts)
				if (part.Shape == ColliderShape::Mesh)
				{
					part.Shape = ColliderShape::ConvexMesh;
					part.HullForDynamic = true;
				}
		}
		return bodies;
	}

	void PhysicsWorld::Implementation::Sync(std::optional<float> deltaTime)
	{
		ApplyGravity();
		PoseCache poses(*m_Scene);
		const std::map<entt::entity, BodyDescription> bodies = DescribeBodies(poses);

		// Bodies no longer wanted, or wanted as something else, go
		bool charactersChanged = false;
		for (auto record = m_Bodies.begin(); record != m_Bodies.end();)
		{
			const auto wanted = bodies.find(record->second.Entity);
			if (wanted != bodies.end() && !MustRecreate(record->second.Description, wanted->second))
			{
				++record;
				continue;
			}
			charactersChanged |= record->second.Description.Kind == BodyKind::Character;
			if (record->second.Description.Kind == BodyKind::Body)
				WakeAround(record->second.Id);
			RemoveFromJolt(record->second);
			m_Ids.Free(record->second.Id);
			m_BodyOfEntity.erase(record->second.Entity);
			record = m_Bodies.erase(record);
		}

		// New bodies are made, in entity order, and existing ones updated
		for (const auto& [entity, description] : bodies)
		{
			if (const auto found = m_BodyOfEntity.find(entity); found != m_BodyOfEntity.end())
			{
				UpdateBody(m_Bodies.at(found->second), description);
				continue;
			}
			CreateBody(entity, description, poses);
			charactersChanged |= description.Kind == BodyKind::Character;
		}
		if (charactersChanged)
			RefreshCharacterList();

		for (auto& [index, record] : m_Bodies)
			SyncPose(record, poses, deltaTime);
	}

	void PhysicsWorld::Implementation::CreateBody(
		entt::entity entity, const BodyDescription& description, PoseCache& poses)
	{
		const entt::registry& registry = m_Scene->GetRegistry();
		BodyRecord record{
			.Entity = entity,
			.EntityId = registry.get<IDComponent>(entity).ID,
			.Description = description,
		};
		const Pose& pose = poses.Get(entity);
		if (!IsFinite(pose))
		{
			LS_CORE_WARN("Entity '{}' isn't given a physics body: its transform isn't finite", DescribeEntity(record));
			return;
		}
		const std::optional<uint32_t> id = m_Ids.Allocate();
		if (!id)
		{
			LS_CORE_ERROR("Entity '{}' isn't given a physics body: the world already has its most, {}",
				DescribeEntity(record), m_Config.MaxBodies);
			return;
		}
		record.Id = *id;

		const auto& transform = registry.get<TransformComponent>(entity);
		glm::vec3 linearVelocity(0.0f);
		glm::vec3 angularVelocity(0.0f);
		if (description.Kind == BodyKind::Character)
		{
			linearVelocity = registry.get<CharacterControllerComponent>(entity).Velocity;
			record.Position = transform.Position;
			record.Rotation = transform.Rotation;
		}
		else if (description.Type == BodyType::Dynamic)
		{
			const auto& body = registry.get<RigidBodyComponent>(entity);
			linearVelocity = body.LinearVelocity;
			angularVelocity = body.AngularVelocity;
			record.Position = transform.Position;
			record.Rotation = transform.Rotation;
		}
		else
		{
			record.Position = pose.Position;
			record.Rotation = pose.Rotation;
		}
		record.LinearVelocity = linearVelocity;
		record.AngularVelocity = angularVelocity;

		if (!AddToJolt(record, pose.Position, pose.Rotation, linearVelocity, angularVelocity))
		{
			m_Ids.Free(record.Id);
			return;
		}
		m_BodyOfEntity[entity] = BodyIdAllocator::GetIndex(record.Id);
		m_Bodies.emplace(BodyIdAllocator::GetIndex(record.Id), std::move(record));
	}

	bool PhysicsWorld::Implementation::AddToJolt(BodyRecord& record, const glm::vec3& position,
		const glm::quat& rotation, const glm::vec3& linearVelocity, const glm::vec3& angularVelocity)
	{
		const BodyDescription& description = record.Description;
		if (description.Kind == BodyKind::Character)
		{
			const JPH::RefConst<JPH::Shape> shape = BuildCharacterShape(description);
			const JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
			settings->mShape = shape;
			settings->mInnerBodyShape = shape;
			settings->mInnerBodyIDOverride = JPH::BodyID(record.Id);
			settings->mInnerBodyLayer = PhysicsLayers::Character;
			settings->mID = JPH::CharacterID(record.Id);
			settings->mMaxSlopeAngle = glm::radians(description.MaxSlope);
			settings->mMass = description.Mass;
			settings->mMaxStrength = description.PushStrength;
			// Contacts below the center of the capsule's bottom hemisphere hold the character up
			settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -GetCharacterRadius(description));
			record.Character = new JPH::CharacterVirtual(
				settings, ToJolt(position), ToJolt(glm::angleAxis(GetYaw(rotation), Up)), record.Id, &m_System);
			if (record.Character->GetInnerBodyID().IsInvalid())
			{
				LS_CORE_ERROR("Jolt refused the character of entity '{}'", DescribeEntity(record));
				record.Character = nullptr;
				return false;
			}
			record.Character->SetLinearVelocity(ToJolt(linearVelocity));
			return true;
		}

		const JPH::RefConst<JPH::Shape> shape = BuildBodyShape(record);
		JPH::BodyCreationSettings settings(
			shape, ToJolt(position), ToJolt(rotation), ToMotionType(description.Type), GetLayer(description));
		settings.mIsSensor = description.Trigger;
		// Lets triggers detect kinematic bodies and characters, whose inner bodies are kinematic
		settings.mCollideKinematicVsNonDynamic = description.Trigger;
		settings.mFriction = description.Friction;
		settings.mRestitution = description.Restitution;
		settings.mLinearDamping = description.LinearDamping;
		settings.mAngularDamping = description.AngularDamping;
		settings.mGravityFactor = description.GravityFactor;
		settings.mMotionQuality =
			description.ContinuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
		settings.mAllowSleeping = description.AllowSleeping;
		settings.mUserData = record.Id;
		if (description.Type != BodyType::Static)
		{
			settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
			settings.mMassPropertiesOverride = GetMassProperties(description, *shape);
		}
		if (description.Type == BodyType::Dynamic)
		{
			settings.mLinearVelocity = ToJolt(linearVelocity);
			settings.mAngularVelocity = ToJolt(glm::radians(angularVelocity));
		}

		JPH::BodyInterface& bodies = m_System.GetBodyInterface();
		const JPH::Body* body = bodies.CreateBodyWithID(JPH::BodyID(record.Id), settings);
		if (body == nullptr)
		{
			LS_CORE_ERROR("Jolt refused the body of entity '{}'", DescribeEntity(record));
			return false;
		}
		bodies.AddBody(body->GetID(),
			description.Type == BodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
		return true;
	}

	void PhysicsWorld::Implementation::RemoveFromJolt(BodyRecord& record)
	{
		if (record.Character != nullptr)
		{
			m_CharacterCollision.Remove(record.Character);
			// Its destructor removes its inner body
			record.Character = nullptr;
			return;
		}
		JPH::BodyInterface& bodies = m_System.GetBodyInterface();
		bodies.RemoveBody(JPH::BodyID(record.Id));
		bodies.DestroyBody(JPH::BodyID(record.Id));
	}

	void PhysicsWorld::Implementation::UpdateBody(BodyRecord& record, const BodyDescription& description)
	{
		if (record.Description == description)
			return;
		const BodyDescription previous = std::exchange(record.Description, description);
		if (description.Kind == BodyKind::Character)
		{
			record.Character->SetMaxSlopeAngle(glm::radians(description.MaxSlope));
			record.Character->SetMass(description.Mass);
			record.Character->SetMaxStrength(description.PushStrength);
			return;
		}

		const JPH::BodyID id(record.Id);
		JPH::BodyInterface& bodies = m_System.GetBodyInterface();
		const JPH::EActivation activation =
			description.Type == BodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
		if (previous.Parts != description.Parts)
		{
			WakeAround(record.Id);
			bodies.SetShape(id, BuildBodyShape(record), false, activation);
			WakeAround(record.Id);
		}
		bodies.SetFriction(id, description.Friction);
		bodies.SetRestitution(id, description.Restitution);
		if (description.Type == BodyType::Static)
			return;

		bodies.SetGravityFactor(id, description.GravityFactor);
		bodies.SetMotionQuality(
			id, description.ContinuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete);
		{
			const JPH::BodyLockWrite lock(m_System.GetBodyLockInterface(), id);
			if (lock.Succeeded())
			{
				JPH::Body& body = lock.GetBody();
				JPH::MotionProperties* motion = body.GetMotionProperties();
				motion->SetLinearDamping(description.LinearDamping);
				motion->SetAngularDamping(description.AngularDamping);
				motion->SetMassProperties(JPH::EAllowedDOFs::All, GetMassProperties(description, *body.GetShape()));
				body.SetAllowSleeping(description.AllowSleeping);
			}
		}
		bodies.ActivateBody(id);
	}

	void PhysicsWorld::Implementation::SyncPose(BodyRecord& record, PoseCache& poses, std::optional<float> deltaTime)
	{
		entt::registry& registry = m_Scene->GetRegistry();
		const auto& transform = registry.get<TransformComponent>(record.Entity);
		const BodyDescription& description = record.Description;

		// Static and kinematic bodies follow their entities
		if (description.Kind == BodyKind::Body && description.Type != BodyType::Dynamic)
		{
			const Pose& pose = poses.Get(record.Entity);
			const JPH::BodyID id(record.Id);
			JPH::BodyInterface& bodies = m_System.GetBodyInterface();
			if (IsSame(pose.Position, record.Position) && IsSame(pose.Rotation, record.Rotation))
			{
				// A kinematic body that moved last tick keeps its velocity unless it's stopped
				if (deltaTime && description.Type == BodyType::Kinematic && bodies.IsActive(id))
					bodies.SetLinearAndAngularVelocity(id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
				return;
			}
			record.Position = pose.Position;
			record.Rotation = pose.Rotation;
			if (!IsFinite(pose))
				return;
			if (deltaTime && description.Type == BodyType::Kinematic)
			{
				bodies.MoveKinematic(id, ToJolt(pose.Position), ToJolt(pose.Rotation), *deltaTime);
				return;
			}
			// A teleport, which pushes nothing out of the way
			WakeAround(record.Id);
			bodies.SetPositionAndRotation(
				id, ToJolt(pose.Position), ToJolt(pose.Rotation), JPH::EActivation::DontActivate);
			WakeAround(record.Id);
			return;
		}

		// Dynamic bodies and characters move where something else put them, and take the velocity it gave them
		const bool moved = !IsSame(transform.Position, record.Position) || !IsSame(transform.Rotation, record.Rotation);
		if (moved)
		{
			record.Position = transform.Position;
			record.Rotation = transform.Rotation;
		}
		const Pose* pose = moved ? &poses.Get(record.Entity) : nullptr;
		const bool teleport = pose != nullptr && IsFinite(*pose);

		if (description.Kind == BodyKind::Character)
		{
			JPH::CharacterVirtual& character = *record.Character;
			if (teleport)
			{
				character.SetPosition(ToJolt(pose->Position));
				character.SetRotation(ToJolt(glm::angleAxis(GetYaw(pose->Rotation), Up)));
			}
			const glm::vec3& velocity = registry.get<CharacterControllerComponent>(record.Entity).Velocity;
			if (!IsSame(velocity, record.LinearVelocity))
			{
				record.LinearVelocity = velocity;
				if (IsFinite(velocity))
					character.SetLinearVelocity(ToJolt(velocity));
			}
			return;
		}

		const JPH::BodyID id(record.Id);
		JPH::BodyInterface& bodies = m_System.GetBodyInterface();
		if (teleport)
			bodies.SetPositionAndRotation(
				id, ToJolt(pose->Position), ToJolt(pose->Rotation), JPH::EActivation::Activate);
		const auto& body = registry.get<RigidBodyComponent>(record.Entity);
		if (!IsSame(body.LinearVelocity, record.LinearVelocity))
		{
			record.LinearVelocity = body.LinearVelocity;
			if (IsFinite(body.LinearVelocity))
			{
				bodies.SetLinearVelocity(id, ToJolt(body.LinearVelocity));
				bodies.ActivateBody(id);
			}
		}
		if (!IsSame(body.AngularVelocity, record.AngularVelocity))
		{
			record.AngularVelocity = body.AngularVelocity;
			if (IsFinite(body.AngularVelocity))
			{
				bodies.SetAngularVelocity(id, ToJolt(glm::radians(body.AngularVelocity)));
				bodies.ActivateBody(id);
			}
		}
	}

	void PhysicsWorld::Implementation::WakeAround(uint32_t id)
	{
		JPH::BodyInterface& bodies = m_System.GetBodyInterface();
		JPH::AABox bounds = bodies.GetTransformedShape(JPH::BodyID(id)).GetWorldSpaceBounds();
		if (!bounds.IsValid())
			return;
		bounds.ExpandBy(JPH::Vec3::sReplicate(WakeMargin));
		bodies.ActivateBodiesInAABox(bounds, JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter());
	}

	void PhysicsWorld::Implementation::RefreshCharacterList()
	{
		for (const auto& [index, record] : m_Bodies)
			if (record.Character != nullptr)
				m_CharacterCollision.Remove(record.Character);
		for (const auto& [index, record] : m_Bodies)
			if (record.Character != nullptr)
			{
				m_CharacterCollision.Add(record.Character);
				record.Character->SetCharacterVsCharacterCollision(&m_CharacterCollision);
			}
	}

	void PhysicsWorld::Implementation::Update(SimulationContext& context)
	{
		LS_CORE_ASSERT(&context.GetScene() == m_Scene, "The physics world steps the scene it was made for");
		const float deltaTime = context.GetDeltaTime();
		Sync(deltaTime);
		MoveCharacters(context);

		const JPH::EPhysicsUpdateError error = m_System.Update(deltaTime, 1, m_TempAllocator.get(), m_JobSystem.get());
		if (error != m_LastUpdateError && error != JPH::EPhysicsUpdateError::None)
			LS_CORE_WARN("The physics world ran out of room during a step ({:#x}): raise its limits, as some "
						 "contacts were missed",
				std::to_underlying(error));
		m_LastUpdateError = error;

		WriteBack();
		GatherEvents();
	}

	void PhysicsWorld::Implementation::MoveCharacters(SimulationContext& context)
	{
		m_CharacterContacts.clear();
		const entt::registry& registry = m_Scene->GetRegistry();
		for (auto& [index, record] : m_Bodies)
			if (record.Character != nullptr)
			{
				const auto& component = registry.get<CharacterControllerComponent>(record.Entity);
				MoveCharacter(record, component, context.GetInput(component.Player), context.GetDeltaTime());
			}
		std::ranges::sort(m_CharacterContacts);
		const auto duplicates = std::ranges::unique(m_CharacterContacts);
		m_CharacterContacts.erase(duplicates.begin(), duplicates.end());
	}

	void PhysicsWorld::Implementation::MoveCharacter(
		BodyRecord& record, const CharacterControllerComponent& component, const InputCommand& input, float deltaTime)
	{
		JPH::CharacterVirtual& character = *record.Character;

		// Turn
		float yaw = GetYaw(ToGlm(character.GetRotation()));
		float turn = input.MouseDelta.x * component.MouseTurnSpeed;
		if (input.GamepadConnected)
			turn += ApplyDeadZone(input.GetGamepadAxis(GamepadAxis::RightX)) * component.StickTurnSpeed * deltaTime;
		if (std::isfinite(turn))
			yaw = std::remainder(yaw - glm::radians(turn), glm::two_pi<float>());
		const glm::quat rotation = glm::angleAxis(yaw, Up);
		character.SetRotation(ToJolt(rotation));

		// On the ground it walks where it faces, moving with the ground, and can jump (as Jolt's character samples
		// do). In the air it keeps its momentum, steered toward walking at its air acceleration; and it falls
		const glm::vec2 movement = ReadMovement(input);
		glm::vec3 walk = rotation * glm::vec3(movement.x, 0.0f, -movement.y) * component.MoveSpeed;
		if (!IsFinite(walk))
			walk = glm::vec3(0.0f);
		character.UpdateGroundVelocity();
		const JPH::Vec3 up = JPH::Vec3::sAxisY();
		const JPH::Vec3 current = character.GetLinearVelocity();
		const JPH::Vec3 verticalVelocity = current.Dot(up) * up;
		const JPH::Vec3 groundVelocity = character.GetGroundVelocity();
		// Moving up away from the ground - jumping, or pushed by something - it isn't held to the ground
		const bool towardGround = verticalVelocity.GetY() - groundVelocity.GetY() < 0.1f;
		JPH::Vec3 velocity;
		if (character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround && towardGround &&
			!character.IsSlopeTooSteep(character.GetGroundNormal()))
		{
			velocity = groundVelocity + ToJolt(walk);
			const bool jump = input.WasKeyPressed(Key::Space) ||
				(input.GamepadConnected && input.WasGamepadButtonPressed(GamepadButton::A));
			if (jump && component.JumpSpeed > 0.0f)
				velocity += component.JumpSpeed * up;
		}
		else
		{
			const glm::vec3 horizontal = ToGlm(current - verticalVelocity);
			const glm::vec3 change = walk - horizontal;
			const float length = glm::length(change);
			const float most = std::max(component.AirAcceleration, 0.0f) * deltaTime;
			const glm::vec3 steered = length <= most ? walk : horizontal + change * (most / length);
			velocity = verticalVelocity + ToJolt(steered);
		}
		velocity += m_System.GetGravity() * deltaTime;
		character.SetLinearVelocity(velocity);

		JPH::CharacterVirtual::ExtendedUpdateSettings settings;
		settings.mWalkStairsStepUp = JPH::Vec3(0.0f, std::max(component.StepHeight, 0.0f), 0.0f);
		// Characters stand on static geometry and moving bodies; other characters are handled by character-vs-
		// character collision, and triggers detect characters, not the other way around
		const LayerMaskFilter layers((1u << PhysicsLayers::Static) | (1u << PhysicsLayers::Moving));
		character.ExtendedUpdate(deltaTime, m_System.GetGravity(), settings, layers.GetBroadPhaseLayers(),
			layers.GetObjectLayers(), JPH::BodyFilter(), JPH::ShapeFilter(), *m_TempAllocator);

		for (const JPH::CharacterContact& contact : character.GetActiveContacts())
		{
			if (!contact.mHadCollision)
				continue;
			// Another character is met by its CharacterID, which is its inner body's ID
			const uint32_t other = contact.mBodyB.IsInvalid() ? contact.mCharacterIDB.GetValue()
															  : contact.mBodyB.GetIndexAndSequenceNumber();
			if (other != record.Id)
				m_CharacterContacts.push_back(MakePair(record.Id, other));
		}
	}

	void PhysicsWorld::Implementation::WriteBack()
	{
		entt::registry& registry = m_Scene->GetRegistry();
		const JPH::BodyInterface& bodies = m_System.GetBodyInterface();

		// Parents before children, so a child is written relative to where its parent now is
		std::vector<std::pair<uint32_t, BodyRecord*>> moving;
		for (auto& [index, record] : m_Bodies)
		{
			const BodyDescription& description = record.Description;
			if (description.Kind == BodyKind::Body && description.Type == BodyType::Static)
				continue;
			if (description.Kind == BodyKind::Body && !bodies.IsActive(JPH::BodyID(record.Id)))
				continue;
			uint32_t depth = 0;
			for (entt::entity parent = GetParent(record.Entity); parent != entt::null; parent = GetParent(parent))
				++depth;
			moving.emplace_back(depth, &record);
		}
		std::ranges::stable_sort(moving, {}, [](const auto& entry) { return entry.first; });

		for (const auto& [depth, record] : moving)
		{
			const entt::entity parent = GetParent(record->Entity);
			const Pose parentPose = parent == entt::null ? Pose{} : GetWorldPose(parent);
			auto& transform = registry.get<TransformComponent>(record->Entity);

			if (record->Character != nullptr)
			{
				const JPH::CharacterVirtual& character = *record->Character;
				std::tie(transform.Position, transform.Rotation) =
					ToLocal(parentPose, ToGlm(character.GetPosition()), SafeNormalize(ToGlm(character.GetRotation())));
				auto& component = registry.get<CharacterControllerComponent>(record->Entity);
				component.Velocity = ToGlm(character.GetLinearVelocity());
				component.Grounded = character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
				record->Position = transform.Position;
				record->Rotation = transform.Rotation;
				record->LinearVelocity = component.Velocity;
				continue;
			}

			const JPH::BodyID id(record->Id);
			auto& body = registry.get<RigidBodyComponent>(record->Entity);
			body.LinearVelocity = ToGlm(bodies.GetLinearVelocity(id));
			body.AngularVelocity = glm::degrees(ToGlm(bodies.GetAngularVelocity(id)));
			record->LinearVelocity = body.LinearVelocity;
			record->AngularVelocity = body.AngularVelocity;
			if (record->Description.Type != BodyType::Dynamic)
				continue;
			JPH::RVec3 position;
			JPH::Quat rotation;
			bodies.GetPositionAndRotation(id, position, rotation);
			std::tie(transform.Position, transform.Rotation) =
				ToLocal(parentPose, ToGlm(position), SafeNormalize(ToGlm(rotation)));
			record->Position = transform.Position;
			record->Rotation = transform.Rotation;
		}
	}

	void PhysicsWorld::Implementation::GatherEvents()
	{
		m_Events.clear();
		const std::vector<BodyPair> added = m_Contacts.TakeAdded();
		const JPH::BodyInterface& bodies = m_System.GetBodyInterface();

		// Pairs that were touching: a pair ends when a body is gone, or when Jolt says they no longer touch - unless
		// both are asleep, as Jolt drops the contacts of sleeping bodies
		std::vector<ContactPair> touching;
		touching.reserve(m_Pairs.size());
		std::vector<PhysicsEvent> begins;
		for (const ContactPair& pair : m_Pairs)
		{
			const BodyRecord* first = FindRecord(pair.First);
			const BodyRecord* second = FindRecord(pair.Second);
			bool stillTouching = first != nullptr && second != nullptr;
			if (stillTouching && !std::ranges::binary_search(m_CharacterContacts, pair.GetKey()))
			{
				const JPH::BodyID firstId(pair.First);
				const JPH::BodyID secondId(pair.Second);
				stillTouching = m_System.WereBodiesInContact(firstId, secondId) ||
					(!bodies.IsActive(firstId) && !bodies.IsActive(secondId));
			}
			if (stillTouching)
			{
				touching.push_back(pair);
				continue;
			}
			m_Events.push_back({
				.Type = pair.Trigger ? PhysicsEventType::TriggerExit : PhysicsEventType::CollisionEnd,
				.First = pair.FirstEntity,
				.Second = pair.SecondEntity,
			});
		}

		// Pairs that started touching: from Jolt's contacts and characters' collisions
		std::vector<BodyPair> started;
		std::ranges::set_union(added, m_CharacterContacts, std::back_inserter(started));
		for (const BodyPair& key : started)
		{
			const auto existing = std::ranges::lower_bound(touching, key, {}, &ContactPair::GetKey);
			if (existing != touching.end() && existing->GetKey() == key)
				continue;
			const BodyRecord* first = FindRecord(key.first);
			const BodyRecord* second = FindRecord(key.second);
			if (first == nullptr || second == nullptr)
				continue;
			ContactPair pair{.First = key.first, .Second = key.second};
			pair.Trigger = first->Description.Trigger || second->Description.Trigger;
			const bool secondIsTrigger = !first->Description.Trigger && second->Description.Trigger;
			pair.FirstEntity = secondIsTrigger ? second->EntityId : first->EntityId;
			pair.SecondEntity = secondIsTrigger ? first->EntityId : second->EntityId;
			touching.insert(existing, pair);
			begins.push_back({
				.Type = pair.Trigger ? PhysicsEventType::TriggerEnter : PhysicsEventType::CollisionBegin,
				.First = pair.FirstEntity,
				.Second = pair.SecondEntity,
			});
		}
		m_Pairs = std::move(touching);
		// Ends, then begins, each in the order of the pairs' body IDs
		m_Events.insert(m_Events.end(), begins.begin(), begins.end());
	}

	JPH::RefConst<JPH::Shape> PhysicsWorld::Implementation::BuildBodyShape(const BodyRecord& record) const
	{
		struct Placed
		{
			JPH::RefConst<JPH::Shape> Shape;
			glm::vec3 Position;
			glm::quat Rotation;
		};
		std::vector<Placed> shapes;
		for (const ColliderPart& part : record.Description.Parts)
		{
			JPH::RefConst<JPH::Shape> shape = BuildPartShape(part, record);
			if (shape == nullptr)
				continue;
			// The collider's center is in its entity's space, scaled with it
			shapes.push_back({
				.Shape = std::move(shape),
				.Position = part.Position + part.Rotation * (part.Center * SafeScale(part.Scale)),
				.Rotation = part.Rotation,
			});
		}

		if (shapes.empty())
			return new JPH::EmptyShape();
		if (shapes.size() == 1)
		{
			const Placed& only = shapes.front();
			if (IsSame(only.Position, glm::vec3(0.0f)) && IsSame(only.Rotation, Identity))
				return only.Shape;
			const JPH::RotatedTranslatedShapeSettings placed(ToJolt(only.Position), ToJolt(only.Rotation), only.Shape);
			const JPH::ShapeSettings::ShapeResult result = placed.Create();
			if (!result.HasError())
				return result.Get();
			LS_CORE_WARN(
				"The collider of entity '{}' can't be placed: {}", DescribeEntity(record), result.GetError().c_str());
			return new JPH::EmptyShape();
		}

		JPH::StaticCompoundShapeSettings compound;
		for (const Placed& shape : shapes)
			compound.AddShape(ToJolt(shape.Position), ToJolt(shape.Rotation), shape.Shape);
		const JPH::ShapeSettings::ShapeResult result = compound.Create();
		if (!result.HasError())
			return result.Get();
		LS_CORE_WARN(
			"The colliders of entity '{}' can't be combined: {}", DescribeEntity(record), result.GetError().c_str());
		return new JPH::EmptyShape();
	}

	JPH::RefConst<JPH::Shape> PhysicsWorld::Implementation::BuildPartShape(
		const ColliderPart& part, const BodyRecord& record) const
	{
		JPH::RefConst<JPH::Shape> shape;
		JPH::ShapeSettings::ShapeResult result;
		switch (part.Shape)
		{
			case ColliderShape::Box: {
				const glm::vec3 halfExtent = glm::max(part.Size * 0.5f, glm::vec3(MinExtent));
				const float convexRadius =
					std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({halfExtent.x, halfExtent.y, halfExtent.z}));
				result = JPH::BoxShapeSettings(ToJolt(halfExtent), convexRadius).Create();
				break;
			}
			case ColliderShape::Sphere:
				result = JPH::SphereShapeSettings(std::max(part.Size.x, MinExtent)).Create();
				break;
			case ColliderShape::Capsule: {
				const float radius = std::max(part.Size.x, MinExtent);
				const float halfHeight = 0.5f * part.Size.y - radius;
				result = halfHeight > MinExtent ? JPH::CapsuleShapeSettings(halfHeight, radius).Create()
												: JPH::SphereShapeSettings(radius).Create();
				break;
			}
			case ColliderShape::Mesh:
			case ColliderShape::ConvexMesh:
				if (part.HullForDynamic)
					LS_CORE_WARN("Entity '{}' is a dynamic body, so its mesh collider uses the mesh's convex hull: "
								 "dynamic bodies can't collide as triangles",
						DescribeEntity(record));
				shape = GetMeshShape(part.Model, part.Mesh, part.Shape == ColliderShape::ConvexMesh);
				if (shape == nullptr)
					return nullptr;
				break;
		}
		if (shape == nullptr)
		{
			if (result.HasError())
			{
				LS_CORE_WARN(
					"A collider of entity '{}' can't be made: {}", DescribeEntity(record), result.GetError().c_str());
				return nullptr;
			}
			shape = result.Get();
		}

		const glm::vec3 scale = SafeScale(part.Scale);
		if (IsSame(scale, glm::vec3(1.0f)))
			return shape;
		const JPH::ShapeSettings::ShapeResult scaled = shape->ScaleShape(ToJolt(scale));
		if (scaled.HasError())
		{
			LS_CORE_WARN(
				"A collider of entity '{}' can't be scaled: {}", DescribeEntity(record), scaled.GetError().c_str());
			return nullptr;
		}
		return scaled.Get();
	}

	JPH::RefConst<JPH::Shape> PhysicsWorld::Implementation::GetMeshShape(UUID model, uint32_t mesh, bool convex) const
	{
		const auto key = std::tuple(model, mesh, convex);
		if (const auto found = m_MeshShapes.find(key); found != m_MeshShapes.end())
			return found->second;
		JPH::RefConst<JPH::Shape>& shape = m_MeshShapes[key];

		const Model* asset = m_Config.Models ? m_Config.Models(model) : nullptr;
		if (asset == nullptr)
		{
			LS_CORE_WARN("Mesh colliders of model {} are left out: the model isn't available", model);
			return nullptr;
		}
		if (mesh >= asset->Meshes.size())
		{
			LS_CORE_WARN("Mesh colliders of mesh {} of model {} are left out: it has {} meshes", mesh, model,
				asset->Meshes.size());
			return nullptr;
		}

		const ModelMesh& source = asset->Meshes[mesh];
		JPH::ShapeSettings::ShapeResult result;
		if (convex)
		{
			JPH::Array<JPH::Vec3> points;
			points.reserve(source.Vertices.size());
			for (const MeshVertex& vertex : source.Vertices)
				points.push_back(ToJolt(vertex.Position));
			result = JPH::ConvexHullShapeSettings(points).Create();
		}
		else
		{
			JPH::VertexList vertices;
			vertices.reserve(source.Vertices.size());
			for (const MeshVertex& vertex : source.Vertices)
				vertices.emplace_back(vertex.Position.x, vertex.Position.y, vertex.Position.z);
			JPH::IndexedTriangleList triangles;
			triangles.reserve(source.Indices.size() / 3);
			for (size_t index = 0; index + 2 < source.Indices.size(); index += 3)
				triangles.emplace_back(source.Indices[index], source.Indices[index + 1], source.Indices[index + 2]);
			result = JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)).Create();
		}
		if (result.HasError())
		{
			LS_CORE_WARN(
				"Mesh colliders of mesh {} of model {} are left out: {}", mesh, model, result.GetError().c_str());
			return nullptr;
		}
		shape = result.Get();
		return shape;
	}

	JPH::RefConst<JPH::Shape> PhysicsWorld::Implementation::BuildCharacterShape(
		const BodyDescription& description) const
	{
		// Standing on its position: the capsule's bottom touches the entity's origin
		const float radius = GetCharacterRadius(description);
		const float halfHeight = 0.5f * description.Height - radius;
		const JPH::ShapeSettings::ShapeResult standing = halfHeight > MinExtent
			? JPH::CapsuleShapeSettings(halfHeight, radius).Create()
			: JPH::SphereShapeSettings(radius).Create();
		LS_CORE_ASSERT(!standing.HasError(), "A character's capsule is always valid");
		const JPH::RotatedTranslatedShapeSettings placed(
			JPH::Vec3(0.0f, 0.5f * description.Height, 0.0f), JPH::Quat::sIdentity(), standing.Get());
		const JPH::ShapeSettings::ShapeResult result = placed.Create();
		LS_CORE_ASSERT(!result.HasError(), "A character's capsule can always be placed");
		return result.Get();
	}

	JPH::MassProperties PhysicsWorld::Implementation::GetMassProperties(
		const BodyDescription& description, const JPH::Shape& shape) const
	{
		JPH::MassProperties properties = shape.GetMassProperties();
		if (description.Type == BodyType::Dynamic && properties.mMass > 0.0f && std::isfinite(properties.mMass))
		{
			properties.ScaleToMass(description.Mass);
			return properties;
		}
		// Kinematic bodies' mass doesn't matter; a dynamic body without colliders spins like a ball of its mass
		constexpr float radius = 0.5f;
		properties.mMass = description.Mass;
		properties.mInertia = JPH::Mat44::sScale(0.4f * description.Mass * radius * radius);
		return properties;
	}

	const BodyRecord* PhysicsWorld::Implementation::FindRecord(uint32_t id) const
	{
		const auto found = m_Bodies.find(BodyIdAllocator::GetIndex(id));
		return found != m_Bodies.end() && found->second.Id == id ? &found->second : nullptr;
	}

	std::string PhysicsWorld::Implementation::DescribeEntity(const BodyRecord& record) const
	{
		const auto* name = m_Scene->GetRegistry().try_get<NameComponent>(record.Entity);
		return name != nullptr && !name->Name.empty() ? name->Name : fmt::format("{}", record.EntityId);
	}

	bool PhysicsWorld::Implementation::HasBody(UUID entity) const
	{
		const entt::entity handle = m_Scene->FindHandle(entity);
		return handle != entt::null && m_BodyOfEntity.contains(handle);
	}

	bool PhysicsWorld::Implementation::IsSleeping(UUID entity) const
	{
		const entt::entity handle = m_Scene->FindHandle(entity);
		const auto found = handle != entt::null ? m_BodyOfEntity.find(handle) : m_BodyOfEntity.end();
		if (found == m_BodyOfEntity.end())
			return false;
		const BodyRecord& record = m_Bodies.at(found->second);
		return record.Description.Kind == BodyKind::Body && record.Description.Type != BodyType::Static &&
			!m_System.GetBodyInterface().IsActive(JPH::BodyID(record.Id));
	}

	std::vector<RaycastHit> PhysicsWorld::Implementation::Raycast(
		const glm::vec3& origin, const glm::vec3& direction, const RaycastOptions& options, bool all) const
	{
		const float length = glm::length(direction);
		if (!std::isfinite(length) || length < 1.0e-12f || !std::isfinite(options.MaxDistance) ||
			options.MaxDistance <= 0.0f || !IsFinite(origin))
			return {};
		const JPH::RRayCast ray(ToJolt(origin), ToJolt(direction / length * options.MaxDistance));

		uint32_t layers =
			(1u << PhysicsLayers::Static) | (1u << PhysicsLayers::Moving) | (1u << PhysicsLayers::Character);
		if (options.IncludeTriggers)
			layers |= 1u << PhysicsLayers::Trigger;
		const LayerMaskFilter filter(layers);
		JPH::BodyID ignored;
		if (const entt::entity handle = m_Scene->FindHandle(options.Ignore); handle != entt::null)
			if (const auto found = m_BodyOfEntity.find(handle); found != m_BodyOfEntity.end())
				ignored = JPH::BodyID(m_Bodies.at(found->second).Id);
		const JPH::IgnoreSingleBodyFilter bodyFilter(ignored);

		std::vector<JPH::RayCastResult> hits;
		const JPH::NarrowPhaseQuery& query = m_System.GetNarrowPhaseQuery();
		if (all)
		{
			EveryBodyCollector collector;
			query.CastRay(ray, JPH::RayCastSettings(), collector, filter.GetBroadPhaseLayers(),
				filter.GetObjectLayers(), bodyFilter);
			hits = collector.TakeHits();
		}
		else
		{
			ClosestHitCollector collector;
			query.CastRay(ray, JPH::RayCastSettings(), collector, filter.GetBroadPhaseLayers(),
				filter.GetObjectLayers(), bodyFilter);
			if (collector.GetHit())
				hits.push_back(*collector.GetHit());
		}

		std::vector<RaycastHit> results;
		results.reserve(hits.size());
		for (const JPH::RayCastResult& hit : hits)
		{
			const BodyRecord* record = FindRecord(hit.mBodyID.GetIndexAndSequenceNumber());
			if (record == nullptr)
				continue;
			const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
			glm::vec3 normal(0.0f);
			{
				const JPH::BodyLockRead lock(m_System.GetBodyLockInterface(), hit.mBodyID);
				if (lock.Succeeded())
					normal = ToGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point));
			}
			results.push_back({
				.Entity = record->EntityId,
				.Point = ToGlm(point),
				.Normal = normal,
				.Distance = hit.mFraction * options.MaxDistance,
			});
		}
		return results;
	}

	void PhysicsWorld::Implementation::SaveState(std::vector<std::byte>& state) const
	{
		StateBuffer stream(state);
		const std::span<const uint8_t> sequences = m_Ids.GetSequences();
		const std::span<const uint8_t> used = m_Ids.GetUsed();
		stream.Write(static_cast<uint32_t>(sequences.size()));
		stream.WriteBytes(sequences.data(), sequences.size());
		stream.WriteBytes(used.data(), used.size());

		stream.Write(static_cast<uint32_t>(m_Bodies.size()));
		for (const auto& [index, record] : m_Bodies)
		{
			stream.Write(entt::to_integral(record.Entity));
			Write(stream, record.EntityId);
			stream.Write(record.Id);
			Write(stream, record.Description);
			Write(stream, record.Position);
			Write(stream, record.Rotation);
			Write(stream, record.LinearVelocity);
			Write(stream, record.AngularVelocity);
		}

		stream.Write(static_cast<uint32_t>(m_Pairs.size()));
		for (const ContactPair& pair : m_Pairs)
		{
			stream.Write(pair.First);
			stream.Write(pair.Second);
			stream.Write(pair.Trigger);
			Write(stream, pair.FirstEntity);
			Write(stream, pair.SecondEntity);
		}

		m_System.SaveState(stream);
		for (const auto& [index, record] : m_Bodies)
			if (record.Character != nullptr)
				record.Character->SaveState(stream);
	}

	void PhysicsWorld::Implementation::RestoreState(std::span<const std::byte> state)
	{
		StateBuffer stream(state);
		const uint32_t capacity = std::clamp(m_Config.MaxBodies, 1u, BodyIdAllocator::MaxCapacity);

		uint32_t idCount = 0;
		stream.Read(idCount);
		std::vector<uint8_t> sequences(std::min(idCount, capacity));
		std::vector<uint8_t> used(sequences.size());
		stream.ReadBytes(sequences.data(), sequences.size());
		stream.ReadBytes(used.data(), used.size());

		uint32_t bodyCount = 0;
		stream.Read(bodyCount);
		std::map<uint32_t, BodyRecord> saved;
		for (uint32_t i = 0; i < std::min(bodyCount, capacity) && !stream.IsFailed(); ++i)
		{
			BodyRecord record;
			std::underlying_type_t<entt::entity> entity{};
			stream.Read(entity);
			record.Entity = static_cast<entt::entity>(entity);
			Read(stream, record.EntityId);
			stream.Read(record.Id);
			Read(stream, record.Description, m_Config.MaxBodies);
			Read(stream, record.Position);
			Read(stream, record.Rotation);
			Read(stream, record.LinearVelocity);
			Read(stream, record.AngularVelocity);
			saved.emplace(BodyIdAllocator::GetIndex(record.Id), std::move(record));
		}

		uint32_t pairCount = 0;
		stream.Read(pairCount);
		std::vector<ContactPair> pairs(std::min(pairCount, m_Config.MaxBodyPairs));
		for (ContactPair& pair : pairs)
		{
			stream.Read(pair.First);
			stream.Read(pair.Second);
			stream.Read(pair.Trigger);
			Read(stream, pair.FirstEntity);
			Read(stream, pair.SecondEntity);
		}
		if (stream.IsFailed() || !m_Ids.Restore(sequences, used))
		{
			LS_CORE_ASSERT(false, "The physics state to restore is corrupt");
			return;
		}

		// Bring Jolt's bodies in line with the saved ones: bodies made since go, bodies removed since come back with
		// their IDs, and bodies that changed are made again as they were. Jolt's state then applies to the same bodies
		for (auto live = m_Bodies.begin(); live != m_Bodies.end();)
		{
			const auto match = saved.find(live->first);
			if (match != saved.end() && match->second.Id == live->second.Id &&
				match->second.Entity == live->second.Entity && match->second.Description == live->second.Description)
			{
				++live;
				continue;
			}
			RemoveFromJolt(live->second);
			live = m_Bodies.erase(live);
		}
		for (auto& [index, record] : saved)
		{
			if (const auto live = m_Bodies.find(index); live != m_Bodies.end())
			{
				record.Character = std::move(live->second.Character);
				continue;
			}
			const bool added = AddToJolt(record, glm::vec3(0.0f), Identity, glm::vec3(0.0f), glm::vec3(0.0f));
			LS_CORE_ASSERT(added, "A saved body couldn't be made again");
		}
		m_Bodies = std::move(saved);
		m_BodyOfEntity.clear();
		for (const auto& [index, record] : m_Bodies)
			m_BodyOfEntity[record.Entity] = index;
		RefreshCharacterList();
		m_Pairs = std::move(pairs);

		const bool restored = m_System.RestoreState(stream);
		for (auto& [index, record] : m_Bodies)
			if (record.Character != nullptr)
				record.Character->RestoreState(stream);
		LS_CORE_ASSERT(restored && !stream.IsFailed() && stream.IsEOF(), "The physics state to restore is corrupt");
		m_Events.clear();
		m_CharacterContacts.clear();
		// Contacts Jolt reported while bodies were remade aren't part of any step
		m_Contacts.TakeAdded();
	}

	std::string_view ToString(PhysicsEventType type)
	{
		switch (type)
		{
			case PhysicsEventType::CollisionBegin:
				return "CollisionBegin";
			case PhysicsEventType::CollisionEnd:
				return "CollisionEnd";
			case PhysicsEventType::TriggerEnter:
				return "TriggerEnter";
			case PhysicsEventType::TriggerExit:
				return "TriggerExit";
		}
		return "Unknown";
	}

	PhysicsWorld::PhysicsWorld(Scene& scene, PhysicsWorldConfig config)
	{
		InitializeJoltRuntime();
		m_Implementation = CreateScope<Implementation>(scene, std::move(config));
	}

	PhysicsWorld::~PhysicsWorld() = default;

	void PhysicsWorld::Update(SimulationContext& context)
	{
		m_Implementation->Update(context);
	}

	void PhysicsWorld::SaveState(std::vector<std::byte>& state) const
	{
		m_Implementation->SaveState(state);
	}

	void PhysicsWorld::RestoreState(std::span<const std::byte> state)
	{
		m_Implementation->RestoreState(state);
	}

	void PhysicsWorld::Sync()
	{
		m_Implementation->Sync(std::nullopt);
	}

	std::span<const PhysicsEvent> PhysicsWorld::GetEvents() const
	{
		return m_Implementation->GetEvents();
	}

	std::optional<RaycastHit> PhysicsWorld::Raycast(
		const glm::vec3& origin, const glm::vec3& direction, const RaycastOptions& options) const
	{
		std::vector<RaycastHit> hits = m_Implementation->Raycast(origin, direction, options, false);
		if (hits.empty())
			return std::nullopt;
		return hits.front();
	}

	std::vector<RaycastHit> PhysicsWorld::RaycastAll(
		const glm::vec3& origin, const glm::vec3& direction, const RaycastOptions& options) const
	{
		return m_Implementation->Raycast(origin, direction, options, true);
	}

	uint32_t PhysicsWorld::GetBodyCount() const
	{
		return m_Implementation->GetBodyCount();
	}

	bool PhysicsWorld::HasBody(UUID entity) const
	{
		return m_Implementation->HasBody(entity);
	}

	bool PhysicsWorld::IsSleeping(UUID entity) const
	{
		return m_Implementation->IsSleeping(entity);
	}

}
