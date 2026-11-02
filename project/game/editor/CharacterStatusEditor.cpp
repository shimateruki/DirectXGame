#define NOMINMAX
#include "game/editor/CharacterStatusEditor.h"
#include "game/system/rules/GameplayStatusManager.h"
#include "game/editor/EnemyAttackPreviewRegistry.h"
#include "EditorObjectFactoryRegistry.h"
#include "BaseScene.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#include <algorithm>
#include <cctype>
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
    // モデル名と同名のフォルダに入ったファイルは、フォルダ名をAssetキーにする。
    fs::path relative = fs::relative(modelFile, kModelRootDirectory);
    fs::path parent = relative.parent_path();

    if (!parent.empty() && parent.filename() == modelFile.stem()) {
        return parent.generic_string();
    }

    relative.replace_extension("");
    return relative.generic_string();
}

std::string GetModelDisplayName(const std::string& modelName) {
    // ステータス調整画面だけ日本語の通称を付け、未登録モデルは短い元名で示す。
    if (modelName.empty())
        return "未設定";
    const std::string shortName = fs::path(modelName).filename().string();
    if (shortName == "slime_pink")
        return "ピンクスライム";
    if (shortName == "slime_red")
        return "ファイアスライム";
    if (shortName == "slime_yellow")
        return "サンダースライム";
    if (shortName == "slime_black")
        return "ブラックスライム";
    if (shortName == "slime")
        return "スライム";
    if (shortName == "ring_burner")
        return "リングバーナー";
    return shortName;
}

std::vector<std::string> characterModelNames;
std::string statusText;
bool modelListReady = false;
bool DrawModelPicker(const char* label, const char* id, std::string& modelName);
void RefreshCharacterModelList();
bool DrawStatusEditor(GameplayStatusManager::CharacterStatus& status, const char* id) {
#ifdef USE_IMGUI
    // 新規出現時に使う定義値だけ編集し、現在HPの実行状態とは分けます。
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

bool DrawModelPicker(const char* label, const char* id, std::string& modelName) {
#ifdef USE_IMGUI
    // 保存にはモデルの参照名を使い、一覧では表示名も併記します。
    ImGui::PushID(id);
    bool changed = false;
    const std::string preview = GetModelDisplayName(modelName) + "  /  " + modelName;

    if (ImGui::BeginCombo(label, preview.c_str())) {
        for (const std::string& candidate : characterModelNames) {
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

void RefreshCharacterModelList() {
    // 従来と同じモデルルートを走査し、対応形式を重複のない一覧へまとめます。
    modelListReady = true;
    characterModelNames.clear();
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

            if (!modelKey.empty() && std::find(characterModelNames.begin(), characterModelNames.end(), modelKey) == characterModelNames.end()) {
                characterModelNames.push_back(modelKey);
            }
        }

        std::sort(characterModelNames.begin(), characterModelNames.end());
    } catch (...) {
        characterModelNames.clear();
    }
}


void SavePending() {
#ifdef USE_IMGUI
    auto* manager = GameplayStatusManager::GetInstance();
    // 他の欄・ツールへ移っても、ドラッグが終わった次の更新で保存します。
    if (manager->IsDirty() && ImGui::GetCurrentContext() && !ImGui::IsAnyItemActive())
        statusText = manager->SaveIfDirty() ? "基本ステータスを保存しました。" : "保存に失敗しました。変更はメモリに保持しています。";
#endif
}

bool Draw(BaseScene* scene, const std::string& type) {
#ifdef USE_IMGUI
    auto* manager = GameplayStatusManager::GetInstance();
    manager->Initialize();
    bool changed = false;
    if (ImGui::CollapsingHeader("基本ステータス（種類共通）", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped(type.empty()
            ? "全コピー形態で共通のPlayer基本値です。コピー固有の攻撃は攻撃設定で調整します。"
            : "同じ種類の敵すべてに使う設定です。配置した1体だけの変更ではありません。");
        ImGui::TextDisabled("操作を離すと自動保存。表示中のプレビューは先頭へ戻して反映します。");
        auto* status = type.empty() ? &manager->GetMutablePlayerStatus() : manager->FindMutableEnemyStatus(type);
        if (status) {
            if (!modelListReady) RefreshCharacterModelList();
            bool statusChanged = DrawStatusEditor(*status, "SelectedCharacterStatus");
            if (type.empty() && ImGui::TreeNodeEx("通常移動の加速・減速", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& movement = manager->GetMutablePlayerLocomotion();
                statusChanged |= ImGui::DragFloat("最高速まで（秒）", &movement.accelerationTime, 0.01f, 0.0f, 3.0f, "%.2f");
                statusChanged |= ImGui::DragFloat("停止まで（秒）", &movement.brakingTime, 0.01f, 0.0f, 3.0f, "%.2f");
                statusChanged |= ImGui::DragFloat("空中の追従倍率", &movement.airControl, 0.01f, 0.1f, 1.0f, "%.2f");
                ImGui::TextDisabled("最高速度は上の移動速度を使用。0秒なら即時に切り替わります。");
                movement.Normalize();
                ImGui::TreePop();
            }
            if (statusChanged) {
                manager->MarkDirty();
                const int count = CharacterStatusEditor::Apply(scene, type);
                statusText = "配置済みの対象へ反映: " + std::to_string(count) + " 件";
                changed = true;
            }
        } else ImGui::TextDisabled("この種類の基本ステータスは登録されていません。");

        if (ImGui::Button("モデル一覧更新")) RefreshCharacterModelList();
        if (ImGui::TreeNode("共通設定ファイルの操作")) {
            ImGui::TextWrapped("再読込・標準値への復元は、Playerとすべての敵タイプが対象です。");
            if (ImGui::Button("全種類を再読込")) {
                changed = manager->Reload();
                if (changed) CharacterStatusEditor::Apply(scene, "*");
                statusText = changed ? "全種類の設定を再読込しました。" : "再読込に失敗しました。現在値を保持します。";
            }
            ImGui::SameLine();
            if (ImGui::Button("全種類を標準値へ")) ImGui::OpenPopup("ResetAllStatus");
            if (ImGui::BeginPopupModal("ResetAllStatus", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("Playerと全敵タイプの設定を標準値へ戻して保存します。");
                if (ImGui::Button("戻して保存")) {
                    manager->ResetToDefaults();
                    CharacterStatusEditor::Apply(scene, "*");
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::TreePop();
        }
        SavePending();
        ImGui::TextWrapped("%s", statusText.c_str());
        ImGui::TextWrapped("保存先: %s", GameplayStatusManager::GetSettingsPath());
    }
    return changed;
#else
    (void)scene; (void)type;
    return false;
#endif
}
} // namespace

int CharacterStatusEditor::Apply(BaseScene* scene, const std::string& type) {
    if (!scene) return 0;
    auto* manager = GameplayStatusManager::GetInstance();
    int count = 0;
    for (const auto& object : scene->GetObjects()) {
        // ダミーの固定HPやコピーの実行中姿勢は、プレビューの再生成側に任せます。
        if (!object || object->IsEditorInternal() || object->IsReplayRemoved()) continue;
        std::string enemyType = object->GetEnemyType();
        if (enemyType.empty() && object->GetParameters()) enemyType = object->GetParameters()->enemyType;
        const bool matches = type == "*" || (type.empty() ? object->GetClassName() == "Player"
            : object->GetClassName() == "Enemy" && enemyType == type);
        if (matches && manager->ApplyManagedStatus(object.get(), false)) {
            object->UpdateLocalMatrix();
            object->UpdateWorldMatrix();
            ++count;
        }
    }
    return count;
}

void CharacterStatusEditor::Register() {
    EnemyAttackPreviewRegistry::RegisterStatusEditor({
        [] {
            auto* manager = GameplayStatusManager::GetInstance();
            manager->Initialize();
            std::vector<EnemyAttackPreviewRegistry::StatusTarget> targets;
            // 攻撃確認がある種類を従来の順に並べ、基本値だけ持つ種類を末尾へ補います。
            for (const auto& entry : EnemyAttackPreviewRegistry::GetEntries())
                targets.push_back({entry.type, entry.displayName});
            for (const auto& type : manager->GetEnemyTypes()) {
                if (std::any_of(targets.begin(), targets.end(), [&](const auto& t) { return t.type == type; })) continue;
                std::string label = type == "Bomb" ? "ボム" : type == "RingBurner" ? "リングバーナー" : type;
                for (const auto& entry : EditorObjectFactoryRegistry::GetTypes(EditorObjectFactoryRegistry::Kind::Enemy))
                    if (entry.id == type) label = entry.displayName;
                targets.push_back({type, label + "（基本ステータス）"});
            }
            return targets;
        }, Draw, SavePending
    });
}
