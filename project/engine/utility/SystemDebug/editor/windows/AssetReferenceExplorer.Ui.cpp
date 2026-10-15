#include "AssetReferenceExplorer.h"

#ifdef USE_IMGUI

#include "DebugEditor.h"
#include "Object3d.h"
#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// 参照の検出結果と修復候補だけを描画します。走査・変更処理は本体側に置きます。
void AssetReferenceExplorer::DrawAssetBrowser() {
    // 退避先を除いたAssetだけを表示し、パス文字列で絞り込みます。
    AssetDatabase* database = AssetDatabase::GetInstance();
    ImGui::InputTextWithHint("##AssetReferenceFilter", "Asset名・Pathで絞り込み", assetFilter_, sizeof(assetFilter_));
    ImGui::Separator();
    const std::string filter = Normalize(assetFilter_);
    ImGui::BeginChild("AssetReferenceBrowser", ImVec2(330.0f, 0.0f), true);
    for (const EditorAssetRecord& asset : database->GetAssets()) {
        if (Normalize(asset.sourcePath).find("/.trash/") != std::string::npos) continue;
        if (!filter.empty() && Normalize(asset.sourcePath).find(filter) == std::string::npos) continue;
        const bool selected = asset.guid == selectedGuid_;
        if (ImGui::Selectable(asset.sourcePath.c_str(), selected)) {
            selectedGuid_ = asset.guid;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\nGUID: %s", AssetDatabase::GetAssetTypeName(asset.type), asset.guid.c_str());
        }
    }
    ImGui::EndChild();
}

void AssetReferenceExplorer::DrawUsageList(const std::vector<Usage>& usages, UsageKind kind) const {
    // 参照元とJSON Pointerを併記し、同名Assetのどの設定か追えるようにします。
    int count = 0;
    for (const Usage& usage : usages) {
        if (usage.kind != kind) continue;
        ++count;
        ImGui::BulletText("[%s] %s", usage.ownerCategory.c_str(), usage.ownerPath.c_str());
        ImGui::Indent();
        ImGui::TextDisabled("%s = %s", usage.jsonPointer.c_str(), usage.value.c_str());
        ImGui::Unindent();
    }
    if (count == 0) ImGui::TextDisabled("該当なし");
}

void AssetReferenceExplorer::DrawSelectedAssetDetails() {
    // GUIDを索引へ引き直し、更新で消えたAssetの古いポインタは使いません。
    const EditorAssetRecord* asset = AssetDatabase::GetInstance()->FindByGuid(selectedGuid_);
    if (!asset) {
        ImGui::TextDisabled("左の一覧からAssetを選択してください。");
        return;
    }
    ImGui::TextUnformatted(asset->sourcePath.c_str());
    ImGui::TextDisabled("GUID: %s  /  Type: %s", asset->guid.c_str(), AssetDatabase::GetAssetTypeName(asset->type));
    const auto usageIterator = usagesByGuid_.find(asset->guid);
    const std::vector<Usage> empty;
    const std::vector<Usage>& usages = usageIterator == usagesByGuid_.end() ? empty : usageIterator->second;
    const int directCount = static_cast<int>(std::count_if(usages.begin(), usages.end(), [](const Usage& usage) { return usage.kind == UsageKind::Direct; }));
    const int dynamicCount = static_cast<int>(usages.size()) - directCount;
    if (directCount == 0 && dynamicCount == 0) {
        ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.2f, 1.0f), "未使用候補（確定参照・動的参照候補ともになし）");
    } else if (directCount == 0) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "動的参照の可能性あり。未使用とは断定しません。");
    }

    ImGui::Separator();
    if (renameBufferGuid_ != asset->guid) {
        renameBufferGuid_ = asset->guid;
        const std::string filename = fs::path(asset->sourcePath).filename().generic_string();
        std::snprintf(renameBuffer_, sizeof(renameBuffer_), "%s", filename.c_str());
    }
    ImGui::SetNextItemWidth(280.0f);
    ImGui::InputText("ファイル名", renameBuffer_, sizeof(renameBuffer_));
    ImGui::SameLine();
    if (ImGui::Button("GUIDを維持して名前変更")) {
        std::string error;
        const std::string previousPath = asset->sourcePath;
        if (AssetDatabase::GetInstance()->RenameAsset(previousPath, renameBuffer_, &error)) {
            statusMessage_ = "名前を変更しました。.metaのGUIDは維持されます。Path参照は一覧で確認してください。";
            requestedAssetPath_ = (fs::path(previousPath).parent_path() / renameBuffer_).generic_string();
            rescanRequested_ = true;
        } else {
            statusMessage_ = "名前変更失敗: " + error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("ゴミ箱へ移動")) requestDeleteConfirm_ = true;

    if (ImGui::CollapsingHeader(("確定参照  " + std::to_string(directCount)).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawUsageList(usages, UsageKind::Direct);
    }
    if (ImGui::CollapsingHeader(("動的参照候補  " + std::to_string(dynamicCount)).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawUsageList(usages, UsageKind::DynamicCandidate);
    }

    if (requestDeleteConfirm_) {
        ImGui::OpenPopup("Asset削除の影響確認");
        requestDeleteConfirm_ = false;
    }
    if (ImGui::BeginPopupModal("Asset削除の影響確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", asset->sourcePath.c_str());
        ImGui::Text("確定参照: %d 件 / 動的参照候補: %d 件", directCount, dynamicCount);
        if (directCount > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "削除するとScene・Prefab・Effectの参照がMissingになります。");
        }
        if (ImGui::Button("影響を理解してゴミ箱へ移動")) {
            std::string recovered;
            std::string error;
            if (AssetDatabase::GetInstance()->MoveAssetToTrash(asset->sourcePath, &recovered, &error)) {
                statusMessage_ = "Assetを復元可能なゴミ箱へ移動しました: " + recovered;
                selectedGuid_.clear();
                rescanRequested_ = true;
                ImGui::CloseCurrentPopup();
            } else {
                statusMessage_ = "削除失敗: " + error;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void AssetReferenceExplorer::DrawSelectedObjectReferences() {
    // 選択Objectをその場でJSON化して、保存前のAsset参照も確認します。
    engine::Object3d* object = editor_ ? editor_->GetSelectedObject() : nullptr;
    if (!object) {
        ImGui::TextDisabled("HierarchyまたはGameViewでObjectを選択してください。");
        return;
    }
    std::unordered_map<std::string, std::vector<Usage>> objectUsages;
    VisitJson(object->ExportToJson(), "Object: " + object->GetName(), "", "", objectUsages, false);
    ImGui::Text("%s が参照しているAsset", object->GetName().c_str());
    int count = 0;
    for (const auto& [guid, usages] : objectUsages) {
        const EditorAssetRecord* asset = AssetDatabase::GetInstance()->FindByGuid(guid);
        if (!asset) continue;
        ++count;
        if (ImGui::Selectable(asset->sourcePath.c_str())) selectedGuid_ = guid;
        for (const Usage& usage : usages) {
            ImGui::Indent();
            ImGui::TextDisabled("%s = %s", usage.jsonPointer.c_str(), usage.value.c_str());
            ImGui::Unindent();
        }
    }
    if (count == 0) ImGui::TextDisabled("Asset Databaseで解決できる参照はありません。");
}

void AssetReferenceExplorer::DrawMissingReferences() {
    // 未解決候補には名前参照も含むため、修復は自動一括ではなく個別に提示します。
    if (missingReferences_.empty()) {
        ImGui::TextDisabled("Missing参照候補はありません。");
        return;
    }
    ImGui::TextWrapped("Path・GUIDとして解決できない参照候補です。名前だけを使う独自設定も含むため、候補を確認してから1件ずつ修復してください。");
    ImGui::BeginChild("MissingReferenceList", ImVec2(0.0f, 0.0f), true);
    for (std::size_t index = 0; index < missingReferences_.size(); ++index) {
        MissingReference& missing = missingReferences_[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::SeparatorText(missing.ownerPath.c_str());
        ImGui::TextWrapped("%s = %s", missing.jsonPointer.c_str(), missing.value.c_str());
        if (missing.candidateGuids.empty()) {
            ImGui::TextDisabled("近い修復候補なし");
        } else {
            const EditorAssetRecord* preview = AssetDatabase::GetInstance()->FindByGuid(missing.candidateGuids[missing.selectedCandidate]);
            if (ImGui::BeginCombo("修復候補", preview ? preview->sourcePath.c_str() : "候補なし")) {
                for (int candidateIndex = 0; candidateIndex < static_cast<int>(missing.candidateGuids.size()); ++candidateIndex) {
                    const EditorAssetRecord* candidate = AssetDatabase::GetInstance()->FindByGuid(missing.candidateGuids[candidateIndex]);
                    if (!candidate) continue;
                    const bool selected = candidateIndex == missing.selectedCandidate;
                    if (ImGui::Selectable(candidate->sourcePath.c_str(), selected)) missing.selectedCandidate = candidateIndex;
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("この候補で修復")) {
                RepairMissingReference(missing, statusMessage_);
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void AssetReferenceExplorer::Draw() {
    // Asset索引の世代が変わった時だけ参照関係を再計算します。
    if (!open_) return;
    AssetDatabase* database = AssetDatabase::GetInstance();
    if (rescanRequested_ || indexedGeneration_ != database->GetGeneration()) RefreshIndex();
    if (!requestedAssetPath_.empty()) {
        SelectAssetByPath(requestedAssetPath_);
        requestedAssetPath_.clear();
    }

    ImGui::SetNextWindowSize(ImVec2(1180.0f, 720.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Asset参照エクスプローラー", &open_)) {
        ImGui::End();
        return;
    }
    if (database->IsInitialIndexBuildInProgress()) {
        ImGui::TextDisabled("Asset Databaseの初期索引を作成中です。");
        ImGui::End();
        return;
    }
    if (ImGui::Button("参照を再スキャン")) rescanRequested_ = true;
    ImGui::SameLine();
    ImGui::TextDisabled("%s", statusMessage_.c_str());
    ImGui::Separator();

    if (ImGui::BeginTabBar("AssetReferenceTabs")) {
        if (ImGui::BeginTabItem("Assetの参照元")) {
            DrawAssetBrowser();
            ImGui::SameLine();
            ImGui::BeginChild("AssetReferenceDetails", ImVec2(0.0f, 0.0f), true);
            DrawSelectedAssetDetails();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("選択ObjectのAsset")) {
            DrawSelectedObjectReferences();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(("Missing参照 " + std::to_string(missingReferences_.size())).c_str())) {
            DrawMissingReferences();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

#endif
