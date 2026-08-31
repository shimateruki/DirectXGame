#include "EditorQuickSearch.h"

#ifdef USE_IMGUI

#include "AssetDatabase.h"
#include "BaseScene.h"
#include "CameraEditor.h"
#include "DebugConsole.h"
#include "DebugEditor.h"
#include "DebrisEffectEditor.h"
#include "GPUParticleEditor.h"
#include "MeshEffectEditor.h"
#include "ParticleEditor.h"
#include "TrailEmitterEditor.h"
#include "VFXSequencerEditor.h"
#include "EditorCommandRegistry.h"
#include "IEditable.h"
#include "EditorManager.h"
#include "IconsFontAwesome5.h"
#include "KeyConfig.h"
#include "Object3d.h"
#include "PresetEditor.h"
#include "PresetManager.h"
#include "SceneManager.h"
#include "imgui.h"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace {
using json = nlohmann::json;

std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

std::vector<std::string> SplitWords(const std::string& text) {
    std::istringstream stream(text);
    std::vector<std::string> words;
    for (std::string word; stream >> word;) {
        words.push_back(std::move(word));
    }
    return words;
}

std::string JoinKeywords(const std::vector<std::string>& keywords) {
    std::string result;
    for (const std::string& keyword : keywords) {
        if (!result.empty()) result += ' ';
        result += keyword;
    }
    return result;
}

std::string GetPathLeaf(const std::string& path) {
    const std::filesystem::path filePath(path);
    const std::string leaf = filePath.filename().string();
    return leaf.empty() ? path : leaf;
}

std::string MakeObjectDetail(Object3d* object) {
    if (!object) {
        return {};
    }
    const Vector3 position = object->GetWorldPosition();
    std::ostringstream stream;
    stream << object->GetClassName() << " / Layer " << object->GetLayer()
           << " / (" << std::fixed << std::setprecision(1)
           << position.x << ", " << position.y << ", " << position.z << ')';
    return stream.str();
}
}

void EditorQuickSearch::Initialize(
    SceneManager* sceneManager,
    DebugEditor* editor,
    std::function<void()> ensureEditorPanelsVisible) {
    sceneManager_ = sceneManager;
    editor_ = editor;
    ensureEditorPanelsVisible_ = std::move(ensureEditorPanelsVisible);
    LoadState();
}

void EditorQuickSearch::Finalize() {
    SaveState();
    results_.clear();
    ensureEditorPanelsVisible_ = {};
    editor_ = nullptr;
    sceneManager_ = nullptr;
}

void EditorQuickSearch::Open() {
    openRequested_ = true;
    focusQueryRequested_ = true;
    selectedIndex_ = 0;
    RefreshResults(true);
}

void EditorQuickSearch::HandleShortcut() {
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && io.KeyCtrl && !io.KeyShift && !io.KeyAlt &&
        ImGui::IsKeyPressed(ImGuiKey_K, false)) {
        Open();
    }
}

void EditorQuickSearch::Draw() {
    const char* popupName = ICON_FA_SEARCH " 統合コマンドパレット###EditorQuickSearch";
    if (openRequested_) {
        ImGui::OpenPopup(popupName);
        openRequested_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(760.0f, 520.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(
        popupName,
        nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    if (focusQueryRequested_) {
        ImGui::SetKeyboardFocusHere();
        focusQueryRequested_ = false;
    }

    const bool submitted = ImGui::InputTextWithHint(
        "##EditorQuickSearchInput",
        "操作、Object、Asset、Scene、Preset、ウィンドウを検索...",
        query_,
        sizeof(query_),
        ImGuiInputTextFlags_EnterReturnsTrue);
    RefreshResults();

    ImGui::SameLine();
    ImGui::TextDisabled("%zu件", results_.size());
    ImGui::TextDisabled("Ctrl+K: 開く / ↑↓: 選択 / Enter: 実行 / Esc: 閉じる / ☆: お気に入り");
    ImGui::Separator();

    if (ImGui::BeginChild("EditorQuickSearchResults", ImVec2(0.0f, -34.0f), false)) {
        for (std::size_t index = 0; index < results_.size(); ++index) {
            SearchItem& item = results_[index];
            ImGui::PushID(item.id.c_str());

            if (item.favorite) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.24f, 1.0f));
            }
            if (ImGui::SmallButton(item.favorite ? ICON_FA_STAR : ICON_FA_STAR_OF_LIFE)) {
                ToggleFavorite(item.id);
                item.favorite = IsFavorite(item.id);
            }
            if (item.favorite) {
                ImGui::PopStyleColor();
            }
            ImGui::SameLine();

            ImGui::BeginDisabled(!item.enabled);
            const bool selected = static_cast<int>(index) == selectedIndex_;
            const std::string label = item.label + "##result";
            if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, 38.0f))) {
                selectedIndex_ = static_cast<int>(index);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    ExecuteItem(index);
                }
            }
            ImGui::EndDisabled();

            const ImVec2 rowMin = ImGui::GetItemRectMin();
            const ImVec2 rowMax = ImGui::GetItemRectMax();
            const std::string category = '[' + item.category + ']';
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(rowMax.x - ImGui::CalcTextSize(category.c_str()).x - 8.0f, rowMin.y + 3.0f),
                IM_COL32(105, 190, 235, item.enabled ? 235 : 110),
                category.c_str());
            if (!item.detail.empty()) {
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(rowMin.x + 4.0f, rowMin.y + 21.0f),
                    IM_COL32(165, 170, 180, item.enabled ? 230 : 100),
                    item.detail.c_str());
            }
            ImGui::PopID();
        }
        if (results_.empty()) {
            ImGui::TextDisabled("一致する項目がありません。");
        }
    }
    ImGui::EndChild();

    if (!results_.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
            selectedIndex_ = (std::min)(selectedIndex_ + 1, static_cast<int>(results_.size()) - 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
            selectedIndex_ = (std::max)(selectedIndex_ - 1, 0);
        }
        if (submitted || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            ExecuteItem(static_cast<std::size_t>(selectedIndex_));
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Asset結果: Model/Preset/Prefabは配置、その他はPathをクリップボードへコピーします。");
    ImGui::EndPopup();
}

void EditorQuickSearch::RefreshResults(bool force) {
    EditorCommandRegistry* commandRegistry = EditorCommandRegistry::GetInstance();
    AssetDatabase* assetDatabase = AssetDatabase::GetInstance();
    PresetManager* presetManager = PresetManager::GetInstance();
    const std::string normalizedQuery = ToLowerAscii(query_);
    const std::uint64_t commandRevision = commandRegistry->GetRevision();
    const std::uint64_t sceneGeneration = sceneManager_ ? sceneManager_->GetSceneGeneration() : 0;
    const std::uint64_t assetGeneration = assetDatabase->GetGeneration();
    const std::size_t presetCount = presetManager->GetPresets().size();
    const std::size_t prefabCount = presetManager->GetPrefabs().size();

    if (!force && normalizedQuery == lastQuery_ &&
        commandRevision == lastCommandRevision_ &&
        sceneGeneration == lastSceneGeneration_ &&
        assetGeneration == lastAssetGeneration_ &&
        presetCount == lastPresetCount_ &&
        prefabCount == lastPrefabCount_) {
        return;
    }

    lastQuery_ = normalizedQuery;
    lastCommandRevision_ = commandRevision;
    lastSceneGeneration_ = sceneGeneration;
    lastAssetGeneration_ = assetGeneration;
    lastPresetCount_ = presetCount;
    lastPrefabCount_ = prefabCount;

    std::vector<SearchItem> candidates;
    CollectCommands(candidates);
    CollectObjects(candidates);
    CollectAssets(candidates);
    CollectSceneAssets(candidates);
    CollectPresets(candidates);
    CollectEditorWindows(candidates);

    results_.clear();
    const bool hasHistory = !favoriteIds_.empty() || !recentIds_.empty();
    for (SearchItem& item : candidates) {
        item.favorite = IsFavorite(item.id);
        item.recentRank = FindRecentRank(item.id);
        item.score = ScoreItem(item, normalizedQuery);
        if (normalizedQuery.empty()) {
            if (hasHistory && !item.favorite && item.recentRank < 0) {
                continue;
            }
            if (!hasHistory && item.kind != ItemKind::Command && item.kind != ItemKind::Window) {
                continue;
            }
        }
        else if (item.score < 0) {
            continue;
        }
        results_.push_back(std::move(item));
    }

    std::stable_sort(results_.begin(), results_.end(), [](const SearchItem& left, const SearchItem& right) {
        if (left.score != right.score) return left.score > right.score;
        if (left.enabled != right.enabled) return left.enabled;
        if (left.category != right.category) return left.category < right.category;
        return left.label < right.label;
    });
    constexpr std::size_t kMaximumResults = 160;
    if (results_.size() > kMaximumResults) {
        results_.resize(kMaximumResults);
    }
    selectedIndex_ = results_.empty()
        ? 0
        : std::clamp(selectedIndex_, 0, static_cast<int>(results_.size()) - 1);
}

void EditorQuickSearch::CollectCommands(std::vector<SearchItem>& items) const {
    EditorCommandRegistry* registry = EditorCommandRegistry::GetInstance();
    for (const EditorCommand* command : registry->GetCommands()) {
        if (!command) continue;
        SearchItem item;
        item.id = "command:" + command->id;
        item.label = command->displayName;
        item.category = "操作 / " + command->category;
        item.detail = command->shortcut.empty()
            ? command->description
            : command->shortcut + "  |  " + command->description;
        item.searchableText = item.label + ' ' + item.category + ' ' + item.detail + ' ' + JoinKeywords(command->keywords);
        item.kind = ItemKind::Command;
        item.enabled = registry->CanExecute(command->id);
        const std::string commandId = command->id;
        item.execute = [commandId]() {
            EditorCommandRegistry::GetInstance()->Execute(commandId);
        };
        items.push_back(std::move(item));
    }
}

void EditorQuickSearch::CollectObjects(std::vector<SearchItem>& items) const {
    BaseScene* scene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;
    if (!scene || !editor_) {
        return;
    }
    int fallbackIndex = 0;
    for (const auto& object : scene->GetObjects()) {
        if (!object || object->IsEditorInternal()) {
            continue;
        }
        Object3d* raw = object.get();
        SearchItem item;
        const std::string guid = raw->GetPersistentGuid();
        item.id = Object3d::IsPersistentGuidValid(guid)
            ? "object:" + guid
            : "object:fallback:" + std::to_string(fallbackIndex++);
        item.label = raw->GetName().empty() ? "NoName Object" : raw->GetName();
        item.category = "Scene Object";
        item.detail = MakeObjectDetail(raw);
        item.searchableText = item.label + ' ' + item.detail + ' ' + raw->GetTag() + ' ' + raw->GetSaveCategory();
        item.kind = ItemKind::Object;
        item.enabled = !sceneManager_->IsPlaying();
        item.execute = [this, raw]() {
            BaseScene* current = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;
            if (!current || !current->IsAlive(raw) || !editor_) return;
            if (ensureEditorPanelsVisible_) ensureEditorPanelsVisible_();
            editor_->SetSelectedObject(raw);
            editor_->SyncObjectSelectionToInspector();
            editor_->FocusSceneObject(raw);
        };
        items.push_back(std::move(item));
    }
}

void EditorQuickSearch::CollectAssets(std::vector<SearchItem>& items) const {
    AssetDatabase* database = AssetDatabase::GetInstance();
    if (!database->IsInitialized() || !editor_) {
        return;
    }
    for (const EditorAssetRecord& asset : database->GetAssets()) {
        SearchItem item;
        item.id = asset.guid.empty() ? "asset:" + asset.sourcePath : "asset:" + asset.guid;
        item.label = GetPathLeaf(asset.sourcePath);
        item.category = std::string("Asset / ") + AssetDatabase::GetAssetTypeName(asset.type);
        item.detail = asset.sourcePath;
        item.searchableText = item.label + ' ' + item.category + ' ' + asset.sourcePath + ' ' + asset.importer;
        item.kind = ItemKind::Asset;
        const bool placeableModel = asset.type == EditorAssetType::Model &&
            asset.sourcePath.find("Resources/3DModel/") == 0;
        item.enabled = !placeableModel || (sceneManager_ && !sceneManager_->IsPlaying());
        const std::string assetPath = asset.sourcePath;
        item.execute = [this, assetPath, placeableModel]() {
            if (placeableModel && editor_) {
                std::error_code error;
                const std::filesystem::path relative = std::filesystem::relative(
                    std::filesystem::path(assetPath),
                    std::filesystem::path("Resources/3DModel"),
                    error);
                if (!error) {
                    editor_->InstantiateModelAtCursor(relative.generic_string());
                    return;
                }
            }
            ImGui::SetClipboardText(assetPath.c_str());
            DebugConsole::GetInstance()->AddLog("Asset Path copied: " + assetPath);
        };
        items.push_back(std::move(item));
    }
}

void EditorQuickSearch::CollectSceneAssets(std::vector<SearchItem>& items) const {
    if (!editor_) {
        return;
    }
    for (const SceneSerializer::SceneAssetInfo& asset : editor_->GetSceneAssets()) {
        SearchItem item;
        item.id = "scene:" + asset.id;
        item.label = asset.displayName.empty() ? asset.id : asset.displayName;
        item.category = "Scene Asset";
        item.detail = asset.filename + " / " + asset.runtimeScene;
        item.searchableText = item.label + ' ' + item.detail + ' ' + asset.id + ' ' + asset.controllerName;
        item.kind = ItemKind::Scene;
        item.enabled = sceneManager_ && !sceneManager_->IsPlaying() && !sceneManager_->IsTransitioning();
        const std::string filename = asset.filename;
        item.execute = [this, filename]() {
            if (!editor_) return;
            if (ensureEditorPanelsVisible_) ensureEditorPanelsVisible_();
            editor_->RequestOpenSceneAssetFromMenu(filename);
        };
        items.push_back(std::move(item));
    }
}

void EditorQuickSearch::CollectPresets(std::vector<SearchItem>& items) const {
    if (!editor_) {
        return;
    }
    PresetManager* manager = PresetManager::GetInstance();
    const bool canPlace = sceneManager_ && !sceneManager_->IsPlaying();
    for (const auto& [name, data] : manager->GetPresets()) {
        SearchItem item;
        item.id = "preset:" + name;
        item.label = name;
        item.category = "Preset";
        item.detail = "Game Viewの現在位置へ配置";
        item.searchableText = item.label + ' ' + item.category + ' ' + data.dump();
        item.kind = ItemKind::Preset;
        item.enabled = canPlace;
        item.execute = [this, name]() { if (editor_) editor_->InstantiatePresetAtCursor(name); };
        items.push_back(std::move(item));
    }
    for (const auto& [name, data] : manager->GetPrefabs()) {
        SearchItem item;
        item.id = "prefab:" + name;
        item.label = name;
        item.category = "Prefab";
        item.detail = manager->IsPrefabVariant(name) ? "VariantをGame Viewへ配置" : "Game Viewの現在位置へ配置";
        item.searchableText = item.label + ' ' + item.category + ' ' + data.dump();
        item.kind = ItemKind::Prefab;
        item.enabled = canPlace;
        item.execute = [this, name]() { if (editor_) editor_->InstantiatePrefabAtCursor(name); };
        items.push_back(std::move(item));
    }
}

void EditorQuickSearch::CollectEditorWindows(std::vector<SearchItem>& items) const {
    if (!editor_) {
        return;
    }

    struct WindowTarget {
        const char* id;
        const char* label;
        const char* keywords;
        IEditable* target;
    };
    const WindowTarget targets[] = {
        { "camera", "カメラ設定", "camera view", CameraEditor::GetInstance() },
        { "keyconfig", "キーコンフィグ", "key input", KeyConfig::GetInstance() },
        { "preset", "プリセットエディタ", "preset prefab", PresetEditor::GetInstance() },
        { "sceneValidator", "シーン視覚監査", "scene audit overlap rotate", editor_->GetSceneValidator() },
        { "levelDesignLab", "レベル設計ラボ", "level design ai plan reachability visibility", editor_->GetLevelDesignLabWindow() },
        { "sceneInventory", "配置物集計", "scene inventory", editor_->GetSceneInventoryWindow() },
        { "materialPreview", "マテリアル確認", "material pbr", editor_->GetMaterialPreviewBoard() },
        { "effectPreview", "エフェクト確認ステージ", "effect vfx preview", editor_->GetEffectPreviewStage() },
        { "enemyAttack", "敵攻撃プレビュー", "enemy attack timeline", editor_->GetEnemyAttackPreviewWindow() },
        { "animation", "アニメーション制作", "animation motion", editor_->GetAnimationWorkbench() },
        { "animator", "Animator Controller", "animator state", editor_->GetAnimatorControllerEditor() },
        { "eventLink", "イベントリンク図", "event link id", editor_->GetEventLinkGraph() },
        { "effectGraph", "演出ノード", "effect node graph", editor_->GetNodeGraphEditorWindow() },
        { "textPng", "テキストPNG生成", "text png", editor_->GetTextSpriteGenerator() },
        { "text3d", "3Dテキスト生成", "text 3d", editor_->GetText3DGenerator() },
        { "modelOptimizer", "モデル最適化", "model lod optimize", editor_->GetModelOptimizerWindow() },
        { "terrain", "地形生成", "terrain stage", editor_->GetTerrainEditorWindow() },
        { "assetAudit", "アセット監査", "asset audit", editor_->GetAssetAuditWindow() },
        { "propertyMatrix", "プロパティマトリクス", "property matrix batch", editor_->GetPropertyMatrixWindow() },
        { "status", "ステータス管理", "status parameter", editor_->GetStatusTuningWindow() },
        { "jsonBackup", "JSONバックアップ", "json backup", editor_->GetJsonBackupWindow() },
        { "audio", "音声設定", "audio sound bgm", editor_->GetAudioSettingsWindow() },
        { "package", "実行ファイルセット", "executable package", editor_->GetExecutablePackageWindow() },
        { "capture", "キャプチャツール", "capture screenshot video", editor_->GetCaptureToolWindow() },
        { "gameData", "内部データ編集", "game data", editor_->GetGameDataDebugEditor() },
        { "gpuParticle", "GPUパーティクル", "gpu particle vfx", editor_->GetGPUParticleEditor() },
        { "vfxSequencer", "VFXシーケンサー", "vfx sequence", editor_->GetVFXSequencerEditor() },
        { "particle", "通常パーティクル", "particle effect", editor_->GetParticleEditor() },
        { "meshEffect", "メッシュエフェクト", "mesh effect", editor_->GetMeshEffectEditor() },
        { "debris", "3D破片エフェクト", "debris effect", editor_->GetDebrisEffectEditor() },
        { "trail", "トレイルエミッター", "trail effect", editor_->GetTrailEmitterEditor() },
    };

    for (const WindowTarget& target : targets) {
        if (!target.target) continue;
        SearchItem item;
        item.id = std::string("window:") + target.id;
        item.label = target.label;
        item.category = "Editor Window";
        item.detail = "Inspectorへ表示";
        item.searchableText = item.label + ' ' + item.category + ' ' + target.keywords;
        item.kind = ItemKind::Window;
        IEditable* editable = target.target;
        item.execute = [this, editable]() {
            if (ensureEditorPanelsVisible_) ensureEditorPanelsVisible_();
            if (editor_) editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editable);
        };
        items.push_back(std::move(item));
    }
}

int EditorQuickSearch::ScoreItem(const SearchItem& item, const std::string& normalizedQuery) const {
    int score = 0;
    if (item.favorite) score += 300;
    if (item.recentRank >= 0) score += (std::max)(0, 160 - item.recentRank * 10);
    if (normalizedQuery.empty()) {
        return score;
    }

    const std::string label = ToLowerAscii(item.label);
    const std::string category = ToLowerAscii(item.category);
    const std::string searchable = ToLowerAscii(item.searchableText);
    for (const std::string& word : SplitWords(normalizedQuery)) {
        if (word.empty()) continue;
        if (label == word) {
            score += 260;
        }
        else if (label.find(word) == 0) {
            score += 180;
        }
        else if (label.find(word) != std::string::npos) {
            score += 120;
        }
        else if (category.find(word) != std::string::npos) {
            score += 70;
        }
        else if (searchable.find(word) != std::string::npos) {
            score += 35;
        }
        else {
            return -1;
        }
    }
    return score;
}

void EditorQuickSearch::ExecuteItem(std::size_t index) {
    if (index >= results_.size() || !results_[index].enabled || !results_[index].execute) {
        return;
    }
    const std::string id = results_[index].id;
    const auto execute = results_[index].execute;
    ImGui::CloseCurrentPopup();
    query_[0] = '\0';
    lastQuery_.clear();
    TouchRecent(id);
    execute();
}

void EditorQuickSearch::ToggleFavorite(const std::string& id) {
    const auto found = std::find(favoriteIds_.begin(), favoriteIds_.end(), id);
    if (found != favoriteIds_.end()) {
        favoriteIds_.erase(found);
    }
    else {
        favoriteIds_.insert(favoriteIds_.begin(), id);
    }
    SaveState();
}

void EditorQuickSearch::TouchRecent(const std::string& id) {
    recentIds_.erase(std::remove(recentIds_.begin(), recentIds_.end(), id), recentIds_.end());
    recentIds_.insert(recentIds_.begin(), id);
    constexpr std::size_t kRecentLimit = 16;
    if (recentIds_.size() > kRecentLimit) {
        recentIds_.resize(kRecentLimit);
    }
    SaveState();
}

bool EditorQuickSearch::IsFavorite(const std::string& id) const {
    return std::find(favoriteIds_.begin(), favoriteIds_.end(), id) != favoriteIds_.end();
}

int EditorQuickSearch::FindRecentRank(const std::string& id) const {
    const auto found = std::find(recentIds_.begin(), recentIds_.end(), id);
    return found == recentIds_.end() ? -1 : static_cast<int>(std::distance(recentIds_.begin(), found));
}

void EditorQuickSearch::LoadState() {
    favoriteIds_.clear();
    recentIds_.clear();
    std::ifstream file(statePath_);
    if (!file.is_open()) return;
    try {
        json root;
        file >> root;
        favoriteIds_ = root.value("favorites", std::vector<std::string>{});
        recentIds_ = root.value("recent", std::vector<std::string>{});
    }
    catch (...) {
        favoriteIds_.clear();
        recentIds_.clear();
    }
}

void EditorQuickSearch::SaveState() const {
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path filePath(statePath_);
    fs::create_directories(filePath.parent_path(), error);
    if (error) return;
    std::ofstream file(filePath);
    if (file.is_open()) {
        file << json{
            { "version", 1 },
            { "favorites", favoriteIds_ },
            { "recent", recentIds_ },
        }.dump(2);
    }
}

#endif
