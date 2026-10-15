#define NOMINMAX
#include "DebugEditor.h"
#include "SceneManager.h"    
#include "BaseScene.h"      
#include "Object3d.h"
#include "imgui.h"
#include <fstream>
#include <string>
#include <vector>
#include "json.hpp"
#include "ImGuizmo.h"
#include "CameraManager.h"
#include "WinApp.h"
#include "Math.h"
#include "DirectXCommon.h"
#include "CollisionConfig.h"
#include "ModelManager.h"      
#include "InputManager.h"    
#include <cmath>
#include <cassert> 
#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include "GhostRecorder.h" 
#include "SceneController.h"
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include "Transform.h"
#include "ParticleManager.h"
#include "EditorManager.h"
#include "PostEffectEditor.h"
#include "SpriteDebugEditor.h"
#include "ParticleEditor.h"
#include "GPUParticleEditor.h"
#include "VFXSequencerEditor.h"
#include "LightEditor.h"      
#include "IconsFontAwesome5.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <DebugConsole.h>
#include <CollisionManager.h>
#include <filesystem> // ファイル操作用
#include <BulletManager.h>
#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.h"
#include <PresetEditor.h>
#include <MeshEffectManager.h>
#include <unordered_map>
#include <utility>
namespace fs = std::filesystem;






// ========================================================================
// 初期化
// ========================================================================

std::vector<SceneSerializer::SceneAssetInfo> DebugEditor::GetSceneAssets() const {
    // ファイル一覧の探索は Serializer に集約し、UI はメタデータを受け取る。
    return serializer_.DiscoverSceneAssets();
}

bool DebugEditor::CreateSceneAsset(
    const std::string& sceneId,
    const std::string& displayName,
    const std::string& runtimeScene,
    SceneSerializer::SceneAssetTemplate sceneTemplate,
    std::string& createdFilename,
    std::string& errorMessage) {
    // Asset の作成とエラー検証は Serializer に任せ、成功時だけ操作ログを残す。
    const bool created = serializer_.CreateSceneAsset(
        sceneId,
        displayName,
        runtimeScene,
        sceneTemplate,
        createdFilename,
        errorMessage);
    if (created) {
        DebugConsole::GetInstance()->AddLog("Created Scene Asset: " + createdFilename);
    }
    return created;
}

bool DebugEditor::DuplicateSceneAsset(
    const std::string& sourceFilename,
    const std::string& newSceneId,
    const std::string& displayName,
    std::string& createdFilename,
    std::string& errorMessage) {
    // 複製元の設定を使って新 ID の Scene Asset を生成する。
    const bool duplicated = serializer_.DuplicateSceneAsset(
        sourceFilename,
        newSceneId,
        displayName,
        createdFilename,
        errorMessage);
    if (duplicated) {
        DebugConsole::GetInstance()->AddLog(
            "Duplicated Scene Asset: " + sourceFilename + " -> " + createdFilename);
    }
    return duplicated;
}

bool DebugEditor::RenameSceneAsset(
    const std::string& sourceFilename,
    const std::string& newSceneId,
    const std::string& displayName,
    std::string& renamedFilename,
    std::string& errorMessage) {
    // 開いている Asset の名前が変わる場合は、SceneManager の保存先パスも追従させる。
    const bool wasCurrentScene = IsCurrentSceneAsset(sourceFilename);
    const bool renamed = serializer_.RenameSceneAsset(
        sourceFilename,
        newSceneId,
        displayName,
        renamedFilename,
        errorMessage);
    if (!renamed) {
        return false;
    }

    if (wasCurrentScene) {
        SetSceneFilename(renamedFilename);
        if (sceneManager_) {
            sceneManager_->SetEditorSceneAssetPaths(
                serializer_.ResolveSceneAssetObjectPath(renamedFilename),
                serializer_.ResolveSceneAssetSpritePath(renamedFilename));
        }
    }
    DebugConsole::GetInstance()->AddLog(
        "Renamed Scene Asset: " + sourceFilename + " -> " + renamedFilename);
    return true;
}

bool DebugEditor::DeleteSceneAsset(const std::string& filename, std::string& errorMessage) {
    // 現在開いている Asset は実行状態とパスが結び付いているため削除させない。
    if (IsCurrentSceneAsset(filename)) {
        errorMessage = "開いているScene Assetは削除できません。別のシーンを開いてから削除してください。";
        return false;
    }
    if (!serializer_.DeleteSceneAsset(filename, errorMessage)) {
        return false;
    }
    DebugConsole::GetInstance()->AddLog("Deleted Scene Asset: " + filename);
    return true;
}

bool DebugEditor::SetSceneAssetRuntimeScene(
    const std::string& filename,
    const std::string& runtimeScene,
    std::string& errorMessage) {
    // 実行クラス名の更新は Serializer に委ね、成功時だけ履歴へ出す。
    if (!serializer_.SetSceneAssetRuntimeScene(filename, runtimeScene, errorMessage)) {
        return false;
    }
    DebugConsole::GetInstance()->AddLog(
        "Scene Asset runtime changed: " + filename + " -> " + runtimeScene);
    return true;
}

bool DebugEditor::SetSceneAssetRuntimeSettings(
    const std::string& filename,
    const std::string& controllerName,
    const std::string& bgmPath,
    const std::string& lightPath,
    const std::string& cameraPath,
    const std::string& skyboxPath,
    std::string& errorMessage) {
    // Controller と環境参照を同じ Serializer の更新経路へ渡す。
    if (!serializer_.SetSceneAssetRuntimeSettings(
        filename,
        controllerName,
        bgmPath,
        lightPath,
        cameraPath,
        skyboxPath,
        errorMessage)) {
        return false;
    }
    DebugConsole::GetInstance()->AddLog("Updated Scene Asset runtime settings: " + filename);
    return true;
}

SceneSerializer::SceneAssetValidationResult DebugEditor::ValidateSceneAsset(
    const std::string& filename) const {
    // ファイル形式に加え、GAMEPLAY 用 Controller の登録状況も検証する。
    SceneSerializer::SceneAssetValidationResult result = serializer_.ValidateSceneAsset(filename);
    const std::vector<SceneSerializer::SceneAssetInfo> assets = serializer_.DiscoverSceneAssets();
    const auto asset = std::find_if(assets.begin(), assets.end(), [&](const SceneSerializer::SceneAssetInfo& candidate) {
        return candidate.filename == filename;
    });
    if (asset != assets.end() && asset->runtimeScene == "GAMEPLAY" &&
        !SceneControllerFactory::GetInstance()->IsRegistered(asset->controllerName)) {
        result.errors.push_back("Scene Controllerが登録されていません: " + asset->controllerName);
    }
    return result;
}

std::vector<std::string> DebugEditor::GetRegisteredSceneNames() const {
    // SceneManager が無い初期化前は空集合を返す。
    return sceneManager_ ? sceneManager_->GetRegisteredSceneNames() : std::vector<std::string>{};
}

bool DebugEditor::OpenSceneAsset(
    const std::string& filename,
    bool discardUnsavedChanges,
    std::string& errorMessage) {
    // Play・Prefab 編集・未保存変更を確認してから Scene Asset を切り替える。
    errorMessage.clear();
    if (!sceneManager_) {
        errorMessage = "SceneManagerが初期化されていません。";
        return false;
    }
    if (sceneManager_->IsPlaying()) {
        errorMessage = "実行中はScene Assetを切り替えられません。停止してから開いてください。";
        return false;
    }
    if (prefabEditMode_) {
        errorMessage = "Prefab Modeを終了してからScene Assetを開いてください。";
        return false;
    }
    if (HasAnyDirty() && !discardUnsavedChanges) {
        errorMessage = "未保存の変更があります。保存するか、破棄して開く操作を選んでください。";
        return false;
    }

    const std::string objectPath = serializer_.ResolveSceneAssetObjectPath(filename);
    const std::string spritePath = serializer_.ResolveSceneAssetSpritePath(filename);
    const std::vector<SceneSerializer::SceneAssetInfo> assets = serializer_.DiscoverSceneAssets();
    const auto asset = std::find_if(assets.begin(), assets.end(), [&](const SceneSerializer::SceneAssetInfo& candidate) {
        return candidate.filename == filename;
    });
    if (asset == assets.end()) {
        errorMessage = "Scene Assetが見つかりません: " + filename;
        return false;
    }
    if (!sceneManager_->IsSceneRegistered(asset->runtimeScene)) {
        errorMessage = "実行クラスがSceneFactoryへ登録されていません: " + asset->runtimeScene;
        return false;
    }
    const SceneSerializer::SceneAssetValidationResult validation = ValidateSceneAsset(filename);
    if (!validation.IsValid()) {
        errorMessage = validation.errors.front();
        for (size_t index = 1; index < validation.errors.size(); ++index) {
            errorMessage += "\n" + validation.errors[index];
        }
        return false;
    }
    for (const std::string& warning : validation.warnings) {
        DebugConsole::GetInstance()->AddLog("Scene validation warning: " + warning);
    }

    // Scene Asset で指定した実行クラスと Object・Sprite・環境データを一つの読込文脈へ渡す。
    SceneLoadContext context;
    context.sceneAssetId = asset->id;
    context.displayName = asset->displayName;
    context.runtimeScene = asset->runtimeScene;
    context.objectLayoutPath = objectPath;
    context.spriteLayoutPath = spritePath;
    context.controllerName = asset->controllerName;
    context.bgmPath = asset->bgmPath;
    context.lightPath = asset->lightPath;
    context.cameraPath = asset->cameraPath;
    context.skyboxPath = asset->skyboxPath;
    if (!sceneManager_->OpenSceneAsset(context)) {
        errorMessage = "シーン遷移中のためScene Assetを開けませんでした。";
        return false;
    }

    // 遷移要求が成立した後、旧 Scene の選択・Undo・Dirty 状態を引き継がない。
    if (ghostRecorder_) {
        ghostRecorder_->ClearTarget();
    }
    ClearObjectSelection();
    EditorManager::GetInstance()->ClearSelection();
    EditorTransactionManager::GetInstance()->Clear();
    pendingSceneAssetOpenAfterSave_.clear();
    ClearDirty(SaveMode::All);
    SetSceneFilename(filename);
    DebugConsole::GetInstance()->AddLog("Opening Scene Asset: " + filename);
    return true;
}

void DebugEditor::SaveThenOpenSceneAsset(const std::string& filename) {
    // 保存確認が確定した後で開くため、遷移先を保留する。
    pendingSceneAssetOpenAfterSave_ = filename;
    SaveScene(SaveMode::All);
}

bool DebugEditor::IsCurrentSceneAsset(const std::string& filename) const {
    // パス表記にかかわらず末尾の JSON 名で現在の Asset と照合する。
    if (!sceneManager_ || !sceneManager_->HasActiveSceneAsset()) {
        return false;
    }
    std::string leafName = filename;
    const size_t slash = leafName.find_last_of("/\\");
    if (slash != std::string::npos) {
        leafName = leafName.substr(slash + 1);
    }
    return leafName == currentSceneFilename_;
}

void DebugEditor::OpenCreateSceneAssetDialog() {
    // 作成 UI の状態管理は Hierarchy に集約する。
    hierarchyWindow_.OpenCreateSceneDialog();
}

void DebugEditor::RequestOpenSceneAssetFromMenu(const std::string& filename) {
    // メニューからの読込も Hierarchy の未保存確認経路を使う。
    hierarchyWindow_.OpenSceneAssetFromMenu(filename);
}

void DebugEditor::SaveScene(SaveMode mode) {
    // Prefab 編集時は Scene JSON へ書かず、専用セッションを保存する。
    if (prefabEditMode_) {
        SavePrefabEditSession();
        return;
    }
#ifdef USE_IMGUI
    // ImGui 有効時は保存対象をプレビューに積み、ユーザーの確定まで書き込まない。
    auto targets = serializer_.BuildSceneSaveTargets(currentSceneFilename_, mode);
    if (targets.empty()) {
        DebugConsole::GetInstance()->AddLog("Save Preview: 保存対象がありません。");
        return;
    }

    sceneSavePreview_.Build(targets, MakeSavePreviewTitle(mode));
    pendingSaveMode_ = mode;
    pendingSaveIsSingleObject_ = false;
    sceneSavePreview_.Open();
#else
    serializer_.SaveScene(currentSceneFilename_, mode);
#endif
}

void DebugEditor::SaveSingleObject() {
    // 選択 Object だけを保存対象にし、通常の Scene 全体保存とは区別する。
    if (prefabEditMode_) {
        SavePrefabEditSession();
        return;
    }
    if (!selectedObject_) return;

#ifdef USE_IMGUI
    auto targets = serializer_.BuildSingleObjectSaveTargets(selectedObject_, std::string(currentSceneFilename_));
    if (targets.empty()) {
        DebugConsole::GetInstance()->AddLog("Save Preview: 単体保存の対象を作成できませんでした。");
        return;
    }

    sceneSavePreview_.Build(targets, "単体保存: " + selectedObject_->GetName());
    pendingSaveMode_ = selectedObject_->IsCameraObject() ? SaveMode::Camera : SaveMode::Object;
    pendingSaveIsSingleObject_ = true;
    sceneSavePreview_.Open();
#else
    serializer_.UpdateObjectInSceneJSON(selectedObject_, std::string(currentSceneFilename_));
#endif
}

void DebugEditor::DrawSavePreview() {
#ifdef USE_IMGUI
    // 確定時だけ書き込み、保存待ちの Scene 切り替えは Dirty が消えた後に行う。
    SceneSavePreview::Action action = sceneSavePreview_.Draw();
    if (action == SceneSavePreview::Action::None) {
        return;
    }

    if (action == SceneSavePreview::Action::Confirm) {
        std::string savedFiles = serializer_.SaveTargets(sceneSavePreview_.GetTargets());
        if (!pendingSaveIsSingleObject_) {
            ClearDirty(pendingSaveMode_);
        }
        std::string notificationName = sceneSavePreview_.GetTitle();
        if (!savedFiles.empty()) {
            notificationName += " (" + savedFiles + ")";
        }
        DebugConsole::GetInstance()->AddLog("Save Preview: 保存を確定しました。");
        TriggerSaveNotification(notificationName);

        if (!pendingSceneAssetOpenAfterSave_.empty() && !HasAnyDirty()) {
            const std::string filenameToOpen = pendingSceneAssetOpenAfterSave_;
            pendingSceneAssetOpenAfterSave_.clear();
            std::string openError;
            if (!OpenSceneAsset(filenameToOpen, false, openError)) {
                DebugConsole::GetInstance()->AddLog("Scene Asset Open Error: " + openError);
            }
        }
    }
    else {
        DebugConsole::GetInstance()->AddLog("Save Preview: 保存をキャンセルしました。");
        pendingSceneAssetOpenAfterSave_.clear();
    }

    sceneSavePreview_.Close();
    pendingSaveIsSingleObject_ = false;
#endif
}

std::string DebugEditor::MakeSavePreviewTitle(SaveMode mode) const {
    // 保存対象のカテゴリを確認画面の見出しに含め、対象 JSON を判別しやすくする。
    std::string baseName = currentSceneFilename_;
    size_t extPos = baseName.find(".json");
    if (extPos != std::string::npos) baseName = baseName.substr(0, extPos);

    switch (mode) {
    case SaveMode::Player:
        return "シーン保存: " + baseName + " / Player";
    case SaveMode::Enemy:
        return "シーン保存: " + baseName + " / Enemy";
    case SaveMode::Object:
        return "シーン保存: " + baseName + " / Object";
    case SaveMode::Camera:
        return "シーン保存: " + baseName + " / Camera";
    case SaveMode::All:
    default:
        return "シーン全体保存: " + baseName;
    }
}

