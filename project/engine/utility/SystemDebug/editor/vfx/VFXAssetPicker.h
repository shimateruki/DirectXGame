#pragma once

#ifdef USE_IMGUI

#include <imgui.h>
#include <algorithm>
#include <string>
#include <vector>

namespace VFXAssetPicker {

// ファイル名とフォルダー名の両方を検索し、候補が多くても必要な行だけ描画します。
inline bool Matches(const std::string& value, const char* query) {
    if (!query || !*query) return true;
    std::string lowerValue = value;
    std::string lowerQuery = query;
    // UTF-8 の日本語バイト列は触らず、英字だけを大文字小文字を区別せず比較します。
    const auto toLowerAscii = [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    };
    std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), toLowerAscii);
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), toLowerAscii);
    return lowerValue.find(lowerQuery) != std::string::npos;
}

inline bool DrawCombo(const char* label, const std::vector<std::string>& entries,
                      int& selectedIndex, char* query, size_t queryCapacity,
                      const char* emptyLabel = "未選択", bool allowNone = false) {
    const bool validSelection = selectedIndex >= 0 && selectedIndex < static_cast<int>(entries.size());
    const char* preview = validSelection ? entries[selectedIndex].c_str() : emptyLabel;
    bool changed = false;
    if (!ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLargest)) {
        if (validSelection && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", preview);
        return false;
    }

    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##AssetSearch", "名前・フォルダーで検索", query, queryCapacity);
    std::vector<int> visibleIndices;
    visibleIndices.reserve(entries.size());
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (Matches(entries[i], query)) visibleIndices.push_back(i);
    }
    ImGui::TextDisabled("%zu / %zu 件", visibleIndices.size(), entries.size());
    ImGui::BeginChild("##AssetResults", ImVec2(0.0f, 220.0f), false);
    if (allowNone && ImGui::Selectable(emptyLabel, selectedIndex == -1)) {
        selectedIndex = -1;
        query[0] = '\0';
        changed = true;
        ImGui::CloseCurrentPopup();
    }
    if (visibleIndices.empty()) ImGui::TextDisabled("一致する項目はありません");
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visibleIndices.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const int index = visibleIndices[row];
            const std::string& fullName = entries[index];
            const size_t slash = fullName.find_last_of("/\\");
            const std::string displayName = slash == std::string::npos
                ? fullName : fullName.substr(slash + 1) + "  —  " + fullName.substr(0, slash);
            ImGui::PushID(index);
            if (ImGui::Selectable(displayName.c_str(), selectedIndex == index)) {
                selectedIndex = index;
                query[0] = '\0';
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fullName.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::EndCombo();
    return changed;
}

} // namespace VFXAssetPicker

#endif
