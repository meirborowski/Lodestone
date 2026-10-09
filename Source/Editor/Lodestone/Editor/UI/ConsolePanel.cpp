#include "Lodestone/Editor/UI/ConsolePanel.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <vector>

namespace Lodestone {

	namespace {

		constexpr std::array<const char*, 6> LevelNames = {"Trace", "Debug", "Info", "Warn", "Error", "Critical"};

		ImVec4 GetLevelColor(LogLevel level)
		{
			switch (level)
			{
				case LogLevel::Trace:
				case LogLevel::Debug:
					return {0.6f, 0.6f, 0.6f, 1.0f};
				case LogLevel::Warn:
					return {1.0f, 0.8f, 0.3f, 1.0f};
				case LogLevel::Error:
				case LogLevel::Critical:
					return {1.0f, 0.4f, 0.4f, 1.0f};
				case LogLevel::Info:
				case LogLevel::Off:
					break;
			}
			return {0.9f, 0.9f, 0.9f, 1.0f};
		}

		bool ContainsIgnoringCase(std::string_view text, std::string_view part)
		{
			const auto equal = [](char a, char b)
			{ return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); };
			return part.empty() || !std::ranges::search(text, part, equal).empty();
		}

	}

	void ConsolePanel::Draw(const LogBuffer& log, bool* open)
	{
		Update(log);
		if (!ImGui::Begin(Title, open))
		{
			ImGui::End();
			return;
		}

		if (ImGui::Button("Clear"))
			m_Entries.clear();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
		ImGui::Combo("##Level", &m_MinimumLevel, LevelNames.data(), static_cast<int>(LevelNames.size()));
		ImGui::SetItemTooltip("The least severe messages shown");
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##Filter", "Filter", &m_Filter);
		ImGui::Separator();

		std::vector<const LogEntry*> shown;
		shown.reserve(m_Entries.size());
		for (const LogEntry& entry : m_Entries)
		{
			if (static_cast<int>(entry.Level) >= m_MinimumLevel && ContainsIgnoringCase(entry.Message, m_Filter))
				shown.push_back(&entry);
		}

		if (ImGui::BeginChild(
				"##Messages", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(shown.size()));
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const LogEntry& entry = *shown[static_cast<size_t>(row)];
					ImGui::PushID(row);
					ImGui::PushStyleColor(ImGuiCol_Text, GetLevelColor(entry.Level));
					// The time without the date, which is the same all session
					const std::string_view time =
						std::string_view(entry.Time)
							.substr(std::min<size_t>(entry.Time.find('T') + 1, entry.Time.size()));
					ImGui::Text("%.*s  %s", static_cast<int>(time.size()), time.data(), entry.Message.c_str());
					ImGui::PopStyleColor();
					// Text has no ID of its own, so the menu needs one
					if (ImGui::BeginPopupContextItem("##Message"))
					{
						if (ImGui::MenuItem("Copy"))
							ImGui::SetClipboardText(entry.Message.c_str());
						ImGui::EndPopup();
					}
					ImGui::PopID();
				}
			}
			// Follows new messages while scrolled to the bottom
			if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();
		ImGui::End();
	}

	void ConsolePanel::Update(const LogBuffer& log)
	{
		if (log.GetLatestSequence() == m_LastSequence)
			return;
		for (LogEntry& entry : log.GetEntries(m_LastSequence, LogLevel::Trace, Capacity))
		{
			m_LastSequence = entry.Sequence;
			m_Entries.push_back(std::move(entry));
		}
		while (m_Entries.size() > Capacity)
			m_Entries.pop_front();
	}

}
