#include "Lodestone/Scene/PrefabSerializer.h"

#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Scene/EntitySerializer.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <array>

namespace Lodestone {

	namespace {

		constexpr const char* EntitiesKey = "entities";

		std::expected<Json::Value, Error> DeserializePrefab(
			const Json::Value& document, const ComponentRegistry& components)
		{
			constexpr std::array<std::string_view, 3> documentMembers = {"format", "version", EntitiesKey};
			if (auto checked = Json::CheckMembers(document, documentMembers, "The prefab"); !checked)
				return std::unexpected(checked.error());
			const auto entities = Json::GetMember(document, EntitiesKey, "The prefab");
			if (!entities)
				return std::unexpected(entities.error());

			// Instancing into a scratch scene checks everything an instance into a real scene will need
			Scene scratch(components);
			if (auto instance =
					EntitySerializer::InstantiateTree(scratch, **entities, EntitySerializer::IdPolicy::Keep);
				!instance)
				return std::unexpected(instance.error());
			return **entities;
		}

	}

	const FileFormat& PrefabSerializer::GetFormat()
	{
		static const FileFormat Format{.Name = "Lodestone.Prefab", .CurrentVersion = 1, .Migrations = {}};
		return Format;
	}

	Json::Value PrefabSerializer::Serialize(const Scene& scene, Entity root)
	{
		LS_CORE_ASSERT(root.GetScene() == &scene && root.IsValid(), "The prefab's root isn't in the scene");
		Json::Value document = CreateDocument(GetFormat());
		document[EntitiesKey] = EntitySerializer::SerializeTree(scene, root.GetHandle());
		return document;
	}

	std::string PrefabSerializer::SerializeToText(const Scene& scene, Entity root)
	{
		return Json::Write(Serialize(scene, root));
	}

	std::expected<Json::Value, Error> PrefabSerializer::Deserialize(
		Json::Value document, const ComponentRegistry& components)
	{
		if (auto upgraded = UpgradeDocument(document, GetFormat()); !upgraded)
			return std::unexpected(upgraded.error());
		try
		{
			return DeserializePrefab(document, components);
		}
		catch (const Json::Value::exception& exception)
		{
			return std::unexpected(Error(ErrorCode::ParseError, exception.what()));
		}
	}

	std::expected<Json::Value, Error> PrefabSerializer::DeserializeFromText(
		std::string_view text, const ComponentRegistry& components)
	{
		auto document = Json::Parse(text);
		if (!document)
			return std::unexpected(document.error());
		return Deserialize(std::move(*document), components);
	}

	std::expected<void, Error> PrefabSerializer::Save(
		const Scene& scene, Entity root, const std::filesystem::path& path)
	{
		return WriteFileAtomically(path, SerializeToText(scene, root))
			.transform_error(
				[&path](const Error& error) { return error.WithContext(fmt::format("Saving prefab {}", path)); });
	}

	std::expected<Json::Value, Error> PrefabSerializer::Load(
		const std::filesystem::path& path, const ComponentRegistry& components)
	{
		const auto text = ReadFile(path);
		if (!text)
			return std::unexpected(text.error().WithContext(fmt::format("Loading prefab {}", path)));
		return DeserializeFromText(*text, components)
			.transform_error(
				[&path](const Error& error) { return error.WithContext(fmt::format("Loading prefab {}", path)); });
	}

	std::expected<Entity, Error> PrefabSerializer::Instantiate(
		Scene& scene, const Json::Value& entities, UUID prefab, Entity parent, std::optional<size_t> index)
	{
		auto instance =
			EntitySerializer::InstantiateTree(scene, entities, EntitySerializer::IdPolicy::Regenerate, parent, index);
		if (!instance)
			return instance;
		if (!prefab.IsNil())
		{
			scene.GetRegistry().emplace_or_replace<PrefabInstanceComponent>(
				instance->GetHandle(), PrefabInstanceComponent{.Prefab = prefab});
		}
		return instance;
	}

}
