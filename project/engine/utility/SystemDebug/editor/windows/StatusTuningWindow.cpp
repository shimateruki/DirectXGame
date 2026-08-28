#define NOMINMAX
#include "StatusTuningWindow.h"

#include "BaseScene.h"
#include "DebugEditor.h"
#include "IconsFontAwesome5.h"
#include "SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace {
namespace fs = std::filesystem;

constexpr const char* kModelRootDirectory = "Resources/3DModel";

std::string ToLowerAscii(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

bool IsSupportedModelFile(const fs::path& path) {
    const std::string extension = ToLowerAscii(path.extension().string());
    return extension == ".obj" || extension == ".gltf" || extension == ".glb";
}

std::string MakeModelKeyFromFile(const fs::path& modelFile) {
    fs::path relative = fs::relative(modelFile, kModelRootDirectory);
    fs::path parent = relative.parent_path();
    if (!parent.empty() && parent.filename() == modelFile.stem()) {
        return parent.generic_string();
    }
    relative.replace_extension("");
    return relative.generic_string();
}

std::string GetModelDisplayName(const std::string& modelName) {
    if (modelName.empty()) return "未設定";
    const std::string shortName = fs::path(modelName).filename().string();
    if (shortName == "slime_pink") return "ピンクスライム";
    if (shortName == "slime_red") return "ファイアスライム";
    if (shortName == "slime_yellow") return "サンダースライム";
    if (shortName == "slime_black") return "ブラックスライム";
    if (shortName == "slime") return "スライム";
    if (shortName == "bat") return "コウモリ";
    if (shortName == "eye") return "アイ";
    if (shortName == "ring_burner") return "リングバーナー";
    return shortName;
}
}

void StatusTuningWindow::Initialize(DebugEditor* editor, SceneManager* sceneManager) {
    editor_ = editor;
    sceneManager_ = sceneManager;
    GameplayStatusManager::GetInstance()->Initialize();
    RefreshCharacterModelList();
}

void StatusTuningWindow::DrawImGui() {
#ifdef USE_IMGUI
    auto* manager = GameplayStatusManager::GetInstance();
    manager->Initialize();

    ImGui::TextColored(ImVec4(0.62f, 0.95f, 1.0f, 1.0f), ICON_FA_SLIDERS_H " ステータス管理");
    ImGui::TextWrapped("PlayerとEnemy Typeの共通値をここだけで管理します。個体別ステータスは使用しません。");
    ImGui::TextDisabled("数値変更中も現在シーンへ反映され、ドラッグ操作を離すと設定JSONへ自動保存されます。");
    ImGui::Separator();

    if (ImGui::Button(ICON_FA_SYNC " 設定JSONを再読込")) {
        if (manager->Reload()) {
            const int applied = ApplyAllStatusLive();
            statusText_ = "設定を再読込して現在シーンへ反映しました: " + std::to_string(applied) + " 件";
        } else {
            statusText_ = "設定JSONの再読込に失敗しました。現在値は維持されています。";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_UNDO " 標準値へ戻す")) {
        ImGui::OpenPopup("ResetGameplayStatusConfirm");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CUBE " モデル一覧更新")) {
        RefreshCharacterModelList();
        statusText_ = "モデル一覧を更新しました。";
    }

    if (ImGui::BeginPopupModal("ResetGameplayStatusConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Playerと全Enemy Typeの設定をゲーム標準値へ戻します。よろしいですか？");
        if (ImGui::Button("戻す", ImVec2(120.0f, 0.0f))) {
            manager->ResetToDefaults();
            const int applied = ApplyAllStatusLive();
            if (manager->Save()) {
                statusText_ = "標準値へ戻して保存しました: " + std::to_string(applied) + " 件";
            } else {
                statusText_ = "標準値は反映しましたが、設定JSONの保存に失敗しました。";
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::TextWrapped("%s", statusText_.c_str());
    ImGui::TextDisabled("保存先: %s", GameplayStatusManager::GetSettingsPath());
    DrawCurrentSceneSummary();
    ImGui::Separator();

    if (ImGui::CollapsingHeader(ICON_FA_USER " プレイヤー", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (DrawStatusEditor(manager->GetMutablePlayerStatus(), "PlayerStatus")) {
            manager->MarkDirty();
            const int applied = ApplyPlayerStatusLive();
            statusText_ = "プレイヤーへリアルタイム反映中: " + std::to_string(applied) + " 件";
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_SLIDERS_H " 敵タイプ別", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawEnemyStatusRows();
    }

    // ドラッグ中はメモリへ即時反映し、操作を離したフレームで1回だけ保存します。
    if (manager->IsDirty() && !ImGui::IsAnyItemActive()) {
        if (manager->Save()) {
            statusText_ = "変更内容をリアルタイム反映し、設定JSONへ自動保存しました。";
        } else {
            statusText_ = "リアルタイム反映は完了しましたが、設定JSONの保存に失敗しました。";
        }
    }
#else
    (void)editor_;
    (void)sceneManager_;
#endif
}

bool StatusTuningWindow::DrawStatusEditor(GameplayStatusManager::CharacterStatus& status, const char* id) {
#ifdef USE_IMGUI
    ImGui::PushID(id);
    bool changed = DrawModelPicker("モデル", "StatusModel", status.modelName);

    changed |= ImGui::DragFloat("最大HP", &status.maxHp, 1.0f, 1.0f, 9999.0f);
    status.maxHp = (std::max)(status.maxHp, 1.0f);
    ImGui::TextDisabled("現在HPは実行状態です。新規出現時は最大HPで開始します。");

    changed |= ImGui::DragFloat("攻撃力倍率", &status.attackPower, 0.05f, 0.0f, 100.0f);
    changed |= ImGui::DragFloat("移動速度", &status.speed, 0.1f, 0.0f, 100.0f);
    changed |= ImGui::DragFloat("重力", &status.gravity, 0.1f, -100.0f, 200.0f);
    changed |= ImGui::DragFloat("最大落下速度", &status.maxFallSpeed, 0.1f, 0.0f, 500.0f);
    changed |= ImGui::DragFloat("ジャンプ力", &status.jumpPower, 0.1f, 0.0f, 100.0f);
    changed |= ImGui::DragFloat("感知範囲", &status.detectionRange, 0.5f, 0.0f, 500.0f);
    changed |= ImGui::DragFloat3("タイプ共通スケール", &status.scale.x, 0.005f, 0.001f, 20.0f, "%.3f");

    if (ImGui::TreeNodeEx("Character Motor", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("標準OFF時は従来の移動結果を維持します。タイプごとに段階導入できます。");
        changed |= ImGui::Checkbox("高速移動をSweep判定", &status.motorContinuousCollision);
        ImGui::BeginDisabled(!status.motorContinuousCollision);
        changed |= ImGui::Checkbox("接地面へ吸着", &status.motorSnapToGround);
        changed |= ImGui::DragFloat("最大登坂角度", &status.motorMaxSlopeDegrees, 0.1f, 0.0f, 89.9f, "%.1f deg");
        changed |= ImGui::DragFloat("自動段差", &status.motorStepHeight, 0.01f, 0.0f, 5.0f, "%.2f m");
        changed |= ImGui::DragFloat("接地探索距離", &status.motorGroundProbeDistance, 0.005f, 0.0f, 2.0f, "%.3f m");
        changed |= ImGui::DragFloat("Skin幅", &status.motorSkinWidth, 0.001f, 0.0f, 0.25f, "%.3f m");
        ImGui::EndDisabled();
        ImGui::TreePop();
    }

    status.attackPower = (std::max)(status.attackPower, 0.0f);
    status.speed = (std::max)(status.speed, 0.0f);
    status.maxFallSpeed = (std::max)(status.maxFallSpeed, 0.0f);
    status.jumpPower = (std::max)(status.jumpPower, 0.0f);
    status.detectionRange = (std::max)(status.detectionRange, 0.0f);
    status.scale.x = (std::max)(status.scale.x, 0.001f);
    status.scale.y = (std::max)(status.scale.y, 0.001f);
    status.scale.z = (std::max)(status.scale.z, 0.001f);
    status.motorMaxSlopeDegrees = std::clamp(status.motorMaxSlopeDegrees, 0.0f, 89.9f);
    status.motorStepHeight = (std::max)(status.motorStepHeight, 0.0f);
    status.motorGroundProbeDistance = (std::max)(status.motorGroundProbeDistance, 0.0f);
    status.motorSkinWidth = std::clamp(status.motorSkinWidth, 0.0f, 0.25f);

    changed |= ImGui::Checkbox("変身に制限時間を使う", &status.morphLimited);
    ImGui::BeginDisabled(!status.morphLimited);
    changed |= ImGui::DragFloat("変身時間（秒）", &status.morphDuration, 0.1f, 0.1f, 999.0f, "%.1f");
    ImGui::EndDisabled();
    status.morphDuration = (std::max)(status.morphDuration, 0.1f);

    ImGui::PopID();
    return changed;
#else
    (void)status;
    (void)id;
    return false;
#endif
}

bool StatusTuningWindow::DrawModelPicker(const char* label, const char* id, std::string& modelName) {
#ifdef USE_IMGUI
    ImGui::PushID(id);
    bool changed = false;
    const std::string preview = GetModelDisplayName(modelName) + "  /  " + modelName;
    if (ImGui::BeginCombo(label, preview.c_str())) {
        for (const std::string& candidate : characterModelNames_) {
            const bool selected = candidate == modelName;
            const std::string itemLabel = GetModelDisplayName(candidate) + "  /  " + candidate;
            if (ImGui::Selectable(itemLabel.c_str(), selected)) {
                modelName = candidate;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
#else
    (void)label;
    (void)id;
    (void)modelName;
    return false;
#endif
}

void StatusTuningWindow::DrawCurrentSceneSummary() {
#ifdef USE_IMGUI
    BaseScene* scene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;
    if (!scene) {
        ImGui::TextDisabled("現在シーンがありません。");
        return;
    }

    int playerCount = 0;
    int enemyCount = 0;
    for (const auto& object : scene->GetObjects()) {
        if (!object) continue;
        if (object->GetClassName() == "Player") ++playerCount;
        else if (object->GetClassName() == "Enemy") ++enemyCount;
    }
    ImGui::Text("現在シーンの反映対象: プレイヤー %d / 敵 %d", playerCount, enemyCount);
#endif
}

void StatusTuningWindow::DrawEnemyStatusRows() {
#ifdef USE_IMGUI
    auto* manager = GameplayStatusManager::GetInstance();
    ImGui::TextDisabled("変更したタイプだけが現在シーンへ即時反映されます。");
    ImGui::BeginChild("EnemyStatusList", ImVec2(0.0f, 600.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (const EnemyTypeInfo& info : enemyTypes_) {
        auto* status = manager->FindMutableEnemyStatus(info.type);
        if (!status) continue;

        ImGui::PushID(info.type);
        const std::string header = std::string(info.label) + "  /  " + info.type;
        if (ImGui::CollapsingHeader(header.c_str())) {
            if (DrawStatusEditor(*status, "EnemyStatus")) {
                manager->MarkDirty();
                const int applied = ApplyEnemyStatusLive(info.type);
                statusText_ = std::string(info.label) + " へリアルタイム反映中: " + std::to_string(applied) + " 件";
            }
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
#endif
}

void StatusTuningWindow::RefreshCharacterModelList() {
    characterModelNames_.clear();
    try {
        const fs::path root(kModelRootDirectory);
        if (!fs::exists(root) || !fs::is_directory(root)) {
            return;
        }

        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file() || !IsSupportedModelFile(entry.path())) {
                continue;
            }
            const std::string modelKey = MakeModelKeyFromFile(entry.path());
            if (!modelKey.empty() && std::find(characterModelNames_.begin(), characterModelNames_.end(), modelKey) == characterModelNames_.end()) {
                characterModelNames_.push_back(modelKey);
            }
        }
        std::sort(characterModelNames_.begin(), characterModelNames_.end());
    } catch (...) {
        characterModelNames_.clear();
    }
}

int StatusTuningWindow::ApplyPlayerStatusLive() {
    BaseScene* scene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;
    if (!scene) return 0;

    int applied = 0;
    auto* manager = GameplayStatusManager::GetInstance();
    for (const auto& object : scene->GetObjects()) {
        if (!object || object->GetClassName() != "Player") continue;
        if (manager->ApplyPlayerStatus(object.get(), false)) {
            object->UpdateLocalMatrix();
            object->UpdateWorldMatrix();
            ++applied;
        }
    }
    return applied;
}

int StatusTuningWindow::ApplyEnemyStatusLive(const std::string& enemyType) {
    BaseScene* scene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;
    if (!scene) return 0;

    int applied = 0;
    auto* manager = GameplayStatusManager::GetInstance();
    for (const auto& object : scene->GetObjects()) {
        if (!object || object->GetClassName() != "Enemy" || object->GetEnemyType() != enemyType) continue;
        if (manager->ApplyEnemyStatus(object.get(), false)) {
            object->UpdateLocalMatrix();
            object->UpdateWorldMatrix();
            ++applied;
        }
    }
    return applied;
}

int StatusTuningWindow::ApplyAllStatusLive() {
    int applied = ApplyPlayerStatusLive();
    for (const EnemyTypeInfo& info : enemyTypes_) {
        applied += ApplyEnemyStatusLive(info.type);
    }
    return applied;
}
