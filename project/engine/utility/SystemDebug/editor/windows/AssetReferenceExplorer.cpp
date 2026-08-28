#include "AssetReferenceExplorer.h"

#ifdef USE_IMGUI

#include "DebugEditor.h"
#include "Object3d.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string StripResourcesPrefix(const std::string& path) {
    const std::string normalized = AssetReferenceExplorer::Normalize(path);
    constexpr const char* prefix = "resources/";
    return normalized.rfind(prefix, 0) == 0 ? normalized.substr(std::strlen(prefix)) : normalized;
}

std::string FileStemLower(const std::string& path) {
    std::string stem = fs::path(path).stem().generic_string();
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return stem;
}

int CommonPrefixScore(const std::string& lhs, const std::string& rhs) {
    const std::size_t count = std::min(lhs.size(), rhs.size());
    int score = 0;
    for (std::size_t index = 0; index < count && lhs[index] == rhs[index]; ++index) {
        ++score;
    }
    return score;
}

} // namespace

void AssetReferenceExplorer::Initialize(DebugEditor* editor) {
    editor_ = editor;
}

void AssetReferenceExplorer::Finalize() {
    editor_ = nullptr;
    open_ = false;
    aliasToAssetIndices_.clear();
    usagesByGuid_.clear();
    missingReferences_.clear();
}

void AssetReferenceExplorer::Open(const std::string& assetPath) {
    open_ = true;
    if (!assetPath.empty()) {
        requestedAssetPath_ = assetPath;
    }
    rescanRequested_ = true;
}

std::string AssetReferenceExplorer::Normalize(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    while (value.rfind("./", 0) == 0) {
        value.erase(0, 2);
    }
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string AssetReferenceExplorer::EscapeJsonPointerToken(const std::string& token) {
    std::string result;
    result.reserve(token.size());
    for (char character : token) {
        if (character == '~') result += "~0";
        else if (character == '/') result += "~1";
        else result.push_back(character);
    }
    return result;
}

std::string AssetReferenceExplorer::GetOwnerCategory(const std::string& path) {
    const std::string lower = Normalize(path);
    if (lower.find("/3dobject/") != std::string::npos || lower.find("/scene/") != std::string::npos) return "Scene";
    if (lower.find("/prefab/") != std::string::npos || lower.find("/preset/") != std::string::npos) return "Prefab/Preset";
    if (lower.find("effect") != std::string::npos || lower.find("particle") != std::string::npos || lower.find("vfx") != std::string::npos) return "Effect";
    return "JSON";
}

bool AssetReferenceExplorer::IsReferenceLikeKey(const std::string& key) {
    const std::string lower = Normalize(key);
    static constexpr const char* hints[] = {
        "asset", "model", "texture", "normalmap", "ormmap", "shader", "effect",
        "particle", "prefab", "preset", "audio", "sound", "animator", "font", "path"
    };
    return std::any_of(std::begin(hints), std::end(hints), [&lower](const char* hint) {
        return lower.find(hint) != std::string::npos;
    });
}

bool AssetReferenceExplorer::IsDirectReferenceValue(const std::string& value) {
    const std::string lower = Normalize(value);
    return lower.rfind("guid:", 0) == 0 || lower.rfind("resources/", 0) == 0 ||
        lower.find('/') != std::string::npos || fs::path(lower).has_extension();
}

void AssetReferenceExplorer::BuildAliasIndex() {
    aliasToAssetIndices_.clear();
    const auto& assets = AssetDatabase::GetInstance()->GetAssets();
    auto addAlias = [this](const std::string& alias, std::size_t index) {
        const std::string normalized = Normalize(alias);
        if (!normalized.empty()) {
            aliasToAssetIndices_[normalized].push_back(index);
        }
    };

    for (std::size_t index = 0; index < assets.size(); ++index) {
        const EditorAssetRecord& asset = assets[index];
        const fs::path source(asset.sourcePath);
        addAlias(asset.guid, index);
        addAlias("guid:" + asset.guid, index);
        addAlias(asset.sourcePath, index);
        addAlias(StripResourcesPrefix(asset.sourcePath), index);
        addAlias(source.filename().generic_string(), index);
        addAlias(source.stem().generic_string(), index);
        fs::path sourceWithoutExtension = source;
        sourceWithoutExtension.replace_extension();
        addAlias(sourceWithoutExtension.generic_string(), index);

        const std::string lowerPath = Normalize(asset.sourcePath);
        constexpr const char* modelPrefix = "resources/3dmodel/";
        if (lowerPath.rfind(modelPrefix, 0) == 0) {
            fs::path relative = fs::path(lowerPath.substr(std::strlen(modelPrefix)));
            const fs::path parent = relative.parent_path();
            if (!parent.empty()) {
                addAlias(parent.generic_string(), index);
                if (Normalize(parent.filename().generic_string()) == Normalize(source.stem().generic_string())) {
                    addAlias(parent.generic_string(), index);
                }
            }
        }
    }
}

std::vector<std::size_t> AssetReferenceExplorer::ResolveCandidates(const std::string& value, const std::string& key) const {
    (void)key;
    const std::string normalized = Normalize(value);
    auto iterator = aliasToAssetIndices_.find(normalized);
    if (iterator != aliasToAssetIndices_.end()) {
        return iterator->second;
    }
    if (normalized.rfind("resources/", 0) != 0) {
        iterator = aliasToAssetIndices_.find("resources/" + normalized);
        if (iterator != aliasToAssetIndices_.end()) {
            return iterator->second;
        }
    }
    return {};
}

void AssetReferenceExplorer::VisitJson(
    const nlohmann::json& value,
    const std::string& ownerPath,
    const std::string& pointer,
    const std::string& key,
    std::unordered_map<std::string, std::vector<Usage>>& destination,
    bool collectMissing) {
    if (value.is_object()) {
        for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
            VisitJson(iterator.value(), ownerPath, pointer + "/" + EscapeJsonPointerToken(iterator.key()), iterator.key(), destination, collectMissing);
        }
        return;
    }
    if (value.is_array()) {
        for (std::size_t index = 0; index < value.size(); ++index) {
            VisitJson(value[index], ownerPath, pointer + "/" + std::to_string(index), key, destination, collectMissing);
        }
        return;
    }
    if (!value.is_string()) return;

    const std::string text = value.get<std::string>();
    if (text.empty()) return;
    const std::vector<std::size_t> candidates = ResolveCandidates(text, key);
    const auto& assets = AssetDatabase::GetInstance()->GetAssets();
    if (!candidates.empty()) {
        const bool direct = IsDirectReferenceValue(text) && candidates.size() == 1;
        for (std::size_t assetIndex : candidates) {
            if (assetIndex >= assets.size() || assets[assetIndex].sourcePath == ownerPath) continue;
            Usage usage;
            usage.ownerPath = ownerPath;
            usage.jsonPointer = pointer;
            usage.value = text;
            usage.ownerCategory = GetOwnerCategory(ownerPath);
            usage.kind = direct ? UsageKind::Direct : UsageKind::DynamicCandidate;
            destination[assets[assetIndex].guid].push_back(std::move(usage));
        }
        return;
    }

    if (collectMissing && IsReferenceLikeKey(key) &&
        (IsDirectReferenceValue(text) || text.size() >= 3) &&
        Normalize(key).find("targetscene") == std::string::npos) {
        MissingReference missing;
        missing.ownerPath = ownerPath;
        missing.jsonPointer = pointer;
        missing.key = key;
        missing.value = text;
        const std::vector<std::size_t> repairCandidates = FindRepairCandidates(missing);
        for (std::size_t candidate : repairCandidates) {
            if (candidate < assets.size()) missing.candidateGuids.push_back(assets[candidate].guid);
        }
        missingReferences_.push_back(std::move(missing));
    }
}

void AssetReferenceExplorer::ScanJsonAsset(const EditorAssetRecord& owner) {
    std::ifstream stream(owner.sourcePath);
    if (!stream.is_open()) return;
    nlohmann::json data;
    try {
        stream >> data;
    } catch (...) {
        return;
    }
    VisitJson(data, owner.sourcePath, "", "", usagesByGuid_, true);
}

std::vector<std::size_t> AssetReferenceExplorer::FindRepairCandidates(const MissingReference& missing) const {
    const auto& assets = AssetDatabase::GetInstance()->GetAssets();
    const std::string wanted = FileStemLower(missing.value);
    struct Scored { std::size_t index; int score; };
    std::vector<Scored> scored;
    scored.reserve(assets.size());
    for (std::size_t index = 0; index < assets.size(); ++index) {
        const std::string stem = FileStemLower(assets[index].sourcePath);
        int score = CommonPrefixScore(wanted, stem) * 4;
        if (stem.find(wanted) != std::string::npos || wanted.find(stem) != std::string::npos) score += 20;
        if (Normalize(missing.key).find("model") != std::string::npos && assets[index].type == EditorAssetType::Model) score += 12;
        if (Normalize(missing.key).find("texture") != std::string::npos && assets[index].type == EditorAssetType::Texture) score += 12;
        if (score > 5) scored.push_back({ index, score });
    }
    std::sort(scored.begin(), scored.end(), [](const Scored& lhs, const Scored& rhs) {
        return lhs.score > rhs.score;
    });
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < std::min<std::size_t>(5, scored.size()); ++index) {
        result.push_back(scored[index].index);
    }
    return result;
}

std::string AssetReferenceExplorer::MakeReferenceValue(const EditorAssetRecord& asset, const std::string& key) const {
    const std::string lowerKey = Normalize(key);
    const std::string lowerPath = Normalize(asset.sourcePath);
    if (lowerKey.find("model") != std::string::npos && lowerPath.rfind("resources/3dmodel/", 0) == 0) {
        fs::path relative(lowerPath.substr(std::strlen("resources/3dmodel/")));
        if (!relative.parent_path().empty()) return relative.parent_path().generic_string();
    }
    return asset.sourcePath;
}

bool AssetReferenceExplorer::RepairMissingReference(MissingReference& missing, std::string& message) {
    if (missing.candidateGuids.empty() || missing.selectedCandidate < 0 ||
        missing.selectedCandidate >= static_cast<int>(missing.candidateGuids.size())) {
        message = "修復候補が選択されていません。";
        return false;
    }
    const EditorAssetRecord* asset = AssetDatabase::GetInstance()->FindByGuid(missing.candidateGuids[missing.selectedCandidate]);
    if (!asset) {
        message = "修復候補がAsset Databaseから見つかりません。";
        return false;
    }
    std::ifstream input(missing.ownerPath);
    nlohmann::json data;
    try {
        input >> data;
        nlohmann::json::json_pointer pointer(missing.jsonPointer);
        if (!data.contains(pointer) || !data[pointer].is_string() || data[pointer].get<std::string>() != missing.value) {
            message = "参照元が再スキャン後に変更されています。再スキャンしてください。";
            return false;
        }
        data[pointer] = MakeReferenceValue(*asset, missing.key);
    } catch (const std::exception& exception) {
        message = std::string("JSONを更新できません: ") + exception.what();
        return false;
    }
    std::ofstream output(missing.ownerPath, std::ios::trunc);
    if (!output.is_open()) {
        message = "参照元JSONを書き込めません。";
        return false;
    }
    output << data.dump(4) << '\n';
    output.close();
    AssetDatabase::GetInstance()->RequestRefresh(true);
    message = "Missing参照を修復しました: " + missing.ownerPath;
    rescanRequested_ = true;
    return true;
}

void AssetReferenceExplorer::RefreshIndex() {
    AssetDatabase* database = AssetDatabase::GetInstance();
    if (!database->IsInitialized() || database->IsInitialIndexBuildInProgress()) return;
    BuildAliasIndex();
    usagesByGuid_.clear();
    missingReferences_.clear();
    for (const EditorAssetRecord& asset : database->GetAssets()) {
        if (asset.type == EditorAssetType::Json && Normalize(asset.sourcePath).find("/.trash/") == std::string::npos) {
            ScanJsonAsset(asset);
        }
    }
    indexedGeneration_ = database->GetGeneration();
    rescanRequested_ = false;
    statusMessage_ = "参照索引を更新しました。Asset " + std::to_string(database->GetAssets().size()) +
        " 件 / Missing候補 " + std::to_string(missingReferences_.size()) + " 件";
}

void AssetReferenceExplorer::SelectAssetByPath(const std::string& path) {
    const EditorAssetRecord* asset = AssetDatabase::GetInstance()->FindByPath(path);
    if (asset) selectedGuid_ = asset->guid;
}

void AssetReferenceExplorer::DrawAssetBrowser() {
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
    Object3d* object = editor_ ? editor_->GetSelectedObject() : nullptr;
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
