#include "Lodestone/Editor/UI/ContentBrowserPanel.h"

#include "Lodestone/Core/FileSystem.h"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <vector>

namespace Lodestone {

	namespace {

		std::string_view GetParentDirectory(std::string_view path)
		{
			const size_t slash = path.rfind('/');
			return slash == std::string_view::npos ? std::string_view() : path.substr(0, slash);
		}

		std::string_view GetFileName(std::string_view path)
		{
			const size_t slash = path.rfind('/');
			return slash == std::string_view::npos ? path : path.substr(slash + 1);
		}

		std::string ToLower(std::string_view text)
		{
			std::string lower(text);
			std::ranges::transform(lower, lower.begin(),
				[](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			return lower;
		}

		std::string JoinPath(std::string_view directory, std::string_view name)
		{
			return directory.empty() ? std::string(name) : fmt::format("{}/{}", directory, name);
		}

		void OpenAsset(
			EditorContext& context, const ConfirmDiscard& confirmDiscard, AssetType type, const std::string& path)
		{
			if (type != AssetType::Scene && type != AssetType::Prefab)
				return;
			confirmDiscard(fmt::format("Opening {}", path),
				[&context, type, path = std::string(path)]
				{
					auto opened = type == AssetType::Scene ? context.OpenScene(path) : context.OpenPrefab(path);
					if (!opened)
						ReportFailure(fmt::format("Opening {}", path), opened.error());
				});
		}

		// The subdirectories of a directory inside the asset directory, sorted
		std::vector<std::string> ListDirectories(const std::filesystem::path& directory)
		{
			std::vector<std::string> names;
			std::error_code error;
			for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
				it.increment(error))
			{
				std::error_code typeError;
				if (it->is_directory(typeError) && !typeError)
					names.push_back(PathToUtf8(it->path().filename()));
			}
			std::ranges::sort(names);
			return names;
		}

	}

	void ContentBrowserPanel::Draw(EditorContext& context, const ConfirmDiscard& confirmDiscard, bool* open)
	{
		if (!ImGui::Begin(Title, open))
		{
			ImGui::End();
			return;
		}
		if (!context.HasProject())
		{
			ImGui::TextDisabled("Create or open a project from the File menu to see its assets");
			ImGui::End();
			return;
		}

		const std::filesystem::path assetDirectory = context.GetProject()->GetAssetDirectory();
		std::error_code error;
		if (!std::filesystem::is_directory(assetDirectory / PathFromUtf8(m_Directory), error))
			m_Directory.clear();

		DrawToolbar(context);
		ImGui::Separator();

		if (ImGui::BeginChild("##Assets"))
		{
			if (m_Filter.empty())
			{
				for (const std::string& name : ListDirectories(assetDirectory / PathFromUtf8(m_Directory)))
				{
					const std::string label = fmt::format("[Folder] {}", name);
					if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
						ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						m_Directory = JoinPath(m_Directory, name);
				}
			}

			const std::string filter = ToLower(m_Filter);
			for (const AssetInfo* asset : context.GetAssets()->GetAll())
			{
				// A filter searches every directory; otherwise the current one is shown
				const bool shown = filter.empty() ? GetParentDirectory(asset->Path) == m_Directory
												  : ToLower(asset->Path).contains(filter);
				if (shown)
					DrawAsset(context, confirmDiscard, *asset);
			}
		}
		ImGui::EndChild();
		ImGui::End();

		std::vector<std::function<void()>> changes;
		changes.swap(m_Deferred);
		for (const std::function<void()>& change : changes)
			change();
	}

	void ContentBrowserPanel::DrawToolbar(EditorContext& context)
	{
		ImGui::BeginDisabled(m_Directory.empty());
		if (ImGui::Button("Up"))
			m_Directory = std::string(GetParentDirectory(m_Directory));
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Rescan"))
		{
			m_Deferred.emplace_back(
				[&context]
				{
					if (auto scanned = context.GetAssets()->Scan(); !scanned)
						ReportFailure("Rescanning the assets", scanned.error());
				});
		}
		ImGui::SetItemTooltip("Find new, moved and removed assets");
		ImGui::SameLine();

		// The path, each part a button that goes there
		if (ImGui::SmallButton("Assets"))
			m_Directory.clear();
		size_t start = 0;
		while (start < m_Directory.size())
		{
			const size_t end = std::min(m_Directory.find('/', start), m_Directory.size());
			ImGui::SameLine(0.0f, 2.0f);
			ImGui::TextDisabled("/");
			ImGui::SameLine(0.0f, 2.0f);
			ImGui::PushID(static_cast<int>(start));
			if (ImGui::SmallButton(m_Directory.substr(start, end - start).c_str()))
				m_Directory.resize(end);
			ImGui::PopID();
			start = end + 1;
		}

		ImGui::SameLine();
		const float filterWidth = ImGui::GetFontSize() * 14.0f;
		if (const float available = ImGui::GetContentRegionAvail().x; available > filterWidth)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - filterWidth);
		ImGui::SetNextItemWidth(filterWidth);
		ImGui::InputTextWithHint("##Filter", "Search", &m_Filter);
	}

	void ContentBrowserPanel::DrawAsset(
		EditorContext& context, const ConfirmDiscard& confirmDiscard, const AssetInfo& asset)
	{
		const AssetType type = asset.Metadata.Type;
		std::string path = asset.Path;
		const std::string label =
			fmt::format("[{}] {}", ToString(type), m_Filter.empty() ? GetFileName(path) : std::string_view(path));
		// Changes wait until the list is drawn, since they can change the assets it's iterating over
		const auto open = [this, &context, &confirmDiscard, type, path]
		{
			m_Deferred.emplace_back(
				[&context, &confirmDiscard, type, path] { OpenAsset(context, confirmDiscard, type, path); });
		};

		ImGui::PushID(path.c_str());
		if (ImGui::Selectable(label.c_str(), m_Selected == path, ImGuiSelectableFlags_AllowDoubleClick))
		{
			m_Selected = path;
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				open();
		}
		ImGui::SetItemTooltip("%s\n%s", path.c_str(), asset.Metadata.Id.ToString().c_str());
		if (type == AssetType::Prefab && ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(AssetPayload, path.data(), path.size());
			ImGui::TextUnformatted(path.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginPopupContextItem())
		{
			const bool editing = context.GetPlayState() == PlayState::Editing;
			if ((type == AssetType::Scene || type == AssetType::Prefab) &&
				ImGui::MenuItem("Open", nullptr, false, editing))
				open();
			if (type == AssetType::Prefab && ImGui::MenuItem("Instance in Scene", nullptr, false, editing))
			{
				m_Deferred.emplace_back(
					[&context, path]
					{
						if (const auto instance = context.InstantiatePrefab(path); instance)
							context.Select(*instance);
						else
							ReportFailure(fmt::format("Instancing {}", path), instance.error());
					});
			}
			if (ImGui::MenuItem("Copy Path"))
				ImGui::SetClipboardText(path.c_str());
			if (ImGui::MenuItem("Copy UUID"))
				ImGui::SetClipboardText(asset.Metadata.Id.ToString().c_str());
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}

}
