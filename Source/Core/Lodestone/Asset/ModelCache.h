#pragma once

#include "Lodestone/Asset/Model.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/UUID.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <unordered_map>

namespace Lodestone {

	class AssetRegistry;

	// Model assets imported from a project on first use, without their textures - what animation, physics and the
	// editor's model tools need of them, wherever they run, renderer or not. A model that fails to import is
	// remembered, and logged once, until it's refreshed. The registry must outlive the cache.
	//
	// Used from one thread
	class ModelCache
	{
	public:
		explicit ModelCache(const AssetRegistry& registry);

		// The model asset, imported if it isn't yet; why it can't be otherwise
		[[nodiscard]] std::expected<const Model*, Error> Load(UUID id);
		// After the registry is scanned: forgets models whose files changed or went away, and failures - the assets
		// may have been added or fixed - so they're imported again the next time they're used. Pointers to models it
		// forgets become invalid
		void Refresh();

	private:
		struct Entry
		{
			std::optional<Model> Model;
			std::optional<Error> Failure;
			// When the file last changed, as it was imported
			std::filesystem::file_time_type WriteTime;
		};

	private:
		const AssetRegistry* m_Registry;
		std::unordered_map<UUID, Entry> m_Models;
	};

}
