#include "SceneAssetPanel.h"

#include "DebugEditor.h"
#include "SceneController.h"
#include "SceneManager.h"
#include "SceneSerializer.h"

#ifdef USE_IMGUI
#include "EditorCommandRegistry.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"
#endif

#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>

void SceneAssetPanel::Initialize(DebugEditor* editor) {
    editor_ = editor;
}

void SceneAssetPanel::OpenCreateSceneDialog() {
    // 新規Sceneには専用Editorクラスを初期候補とし、誤った実行クラスを選びにくくします。
    sceneAssetStatusMessage_.clear();
    sceneAssetStatusIsError_ = false;

    if (editor_) {
        const std::vector<std::string> runtimeScenes = editor_->GetRegisteredSceneNames();
        const auto editorScene = std::find(runtimeScenes.begin(), runtimeScenes.end(), "SCENE_EDITOR");
        createRuntimeSceneIndex_ = editorScene != runtimeScenes.end()
            ? static_cast<int>(std::distance(runtimeScenes.begin(), editorScene))
            : 0;
    }

    requestCreateScenePopup_ = true;
}

void SceneAssetPanel::DrawList() {
#ifdef USE_IMGUI
    // 保存済みSceneと現在編集中のSceneを対比し、作成・複製・開く操作をまとめます。
    if (!editor_ || !ImGui::CollapsingHeader(
        ICON_FA_SAVE " Scene Asset",
        ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    const std::vector<SceneSerializer::SceneAssetInfo> assets = editor_->GetSceneAssets();
    const std::string currentFilename = editor_->GetCurrentSceneFilenameBuffer();
    const bool sceneTransitioning = editor_->GetSceneManager() && editor_->GetSceneManager()->IsTransitioning();

    const auto findAsset = [&](const std::string& filename) {
        return std::find_if(assets.begin(), assets.end(), [&](const SceneSerializer::SceneAssetInfo& asset) {
            return asset.filename == filename;
        });
    };

    // 選択Assetが消えた場合は編集中のScene、なければ先頭の候補へ選択を戻す。
    if (selectedSceneAssetFilename_.empty() || findAsset(selectedSceneAssetFilename_) == assets.end()) {
        if (findAsset(currentFilename) != assets.end()) {
            selectedSceneAssetFilename_ = currentFilename;
        }
        else if (!assets.empty()) {
            selectedSceneAssetFilename_ = assets.front().filename;
        }
    }

    std::string selectedLabel = "Scene Assetがありません";
    const auto selectedAsset = findAsset(selectedSceneAssetFilename_);

    if (selectedAsset != assets.end()) {
        selectedLabel = selectedAsset->displayName + "  (" + selectedAsset->id + ")";
    }

    const bool selectedAssetIsActive = selectedAsset != assets.end() &&
        editor_->IsCurrentSceneAsset(selectedSceneAssetFilename_);

    // 実行設定を変更しただけでは現在のRuntimeへ反映されないため、再読込の必要性を示す。
    bool selectedRuntimeNeedsReload = false;

    if (selectedAssetIsActive && editor_->GetSceneManager()) {
        const SceneLoadContext& activeContext = editor_->GetSceneManager()->GetActiveSceneLoadContext();
        selectedRuntimeNeedsReload =
            selectedAsset->runtimeScene != activeContext.runtimeScene ||
            selectedAsset->controllerName != activeContext.controllerName ||
            selectedAsset->bgmPath != activeContext.bgmPath ||
            selectedAsset->lightPath != activeContext.lightPath ||
            selectedAsset->cameraPath != activeContext.cameraPath ||
            selectedAsset->skyboxPath != activeContext.skyboxPath;
    }

    ImGui::TextColored(
        ImVec4(0.42f, 0.78f, 1.0f, 1.0f),
        ICON_FA_CIRCLE " Active: %s%s",
        currentFilename.c_str(),
        editor_->HasAnyDirty() ? " *" : "");

    // 一覧上の選択と実行中の Scene を分け、ダブルクリック時だけ読み込みを要求する。
    if (ImGui::BeginCombo(ICON_FA_FOLDER_OPEN " Scene一覧", selectedLabel.c_str())) {
        for (const SceneSerializer::SceneAssetInfo& asset : assets) {
            const bool isSelected = asset.filename == selectedSceneAssetFilename_;
            const bool isActive = editor_->IsCurrentSceneAsset(asset.filename);
            std::string label = isActive ? ICON_FA_CIRCLE " " : "   ";
            label += asset.displayName + "##" + asset.filename;

            if (ImGui::Selectable(label.c_str(), isSelected)) {
                selectedSceneAssetFilename_ = asset.filename;

                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !isActive) {
                    RequestOpen(asset.filename);
                }
            }

            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s\n実行クラス: %s\nController: %s\nObject: %s\nSprite: %s\nダブルクリックで開く",
                    asset.filename.c_str(),
                    asset.runtimeScene.c_str(),
                    asset.controllerName.c_str(),
                    asset.usesSplitFiles ? "分割JSON" : "単一JSON",
                    asset.hasSpriteLayout ? asset.spriteLayoutPath.c_str() : "なし");
            }
        }

        ImGui::EndCombo();
    }

    // JSON AssetとC++の実行Sceneクラスの対応を、この選択行から変更できる。
    if (selectedAsset != assets.end()) {
        const std::vector<std::string> runtimeScenes = editor_->GetRegisteredSceneNames();
        ImGui::BeginDisabled(sceneTransitioning || runtimeScenes.empty());

        if (ImGui::BeginCombo("実行クラス", selectedAsset->runtimeScene.c_str())) {
            for (const std::string& runtimeScene : runtimeScenes) {
                const bool isSelected = runtimeScene == selectedAsset->runtimeScene;

                if (ImGui::Selectable(runtimeScene.c_str(), isSelected) && !isSelected) {
                    std::string errorMessage;
                    if (editor_->SetSceneAssetRuntimeScene(
                        selectedAsset->filename,
                        runtimeScene,
                        errorMessage)) {
                        sceneAssetStatusMessage_ = "実行クラスを変更しました: " + runtimeScene;
                        sceneAssetStatusIsError_ = false;
                    }
                    else {
                        sceneAssetStatusMessage_ = errorMessage;
                        sceneAssetStatusIsError_ = true;
                    }
                }

                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        ImGui::EndDisabled();
        ImGui::TextDisabled("Sceneを開くと、このC++ SceneクラスとAssetのJSONを同時に読み込みます。");

        if (selectedAsset->usesLegacyMetadata) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.72f, 0.2f, 1.0f),
                ICON_FA_EXCLAMATION_TRIANGLE " 旧形式Scene。実行設定の保存でScene Asset形式へ更新します。");
        }

        // 実行設定はModalを開く前に現在値をバッファへ複写し、キャンセルなら保存しない。
        if (ImGui::Button(ICON_FA_COG " 実行設定", ImVec2(122.0f, 0.0f))) {
            const std::vector<std::string>& controllerNames =
                SceneControllerFactory::GetInstance()->GetRegisteredNames();
            const auto controller = std::find(
                controllerNames.begin(),
                controllerNames.end(),
                selectedAsset->controllerName);
            runtimeSettingsControllerIndex_ = controller != controllerNames.end()
                ? static_cast<int>(std::distance(controllerNames.begin(), controller))
                : 0;
            strcpy_s(runtimeBgmPath_, sizeof(runtimeBgmPath_), selectedAsset->bgmPath.c_str());
            strcpy_s(runtimeLightPath_, sizeof(runtimeLightPath_), selectedAsset->lightPath.c_str());
            strcpy_s(runtimeCameraPath_, sizeof(runtimeCameraPath_), selectedAsset->cameraPath.c_str());
            strcpy_s(runtimeSkyboxPath_, sizeof(runtimeSkyboxPath_), selectedAsset->skyboxPath.c_str());
            requestRuntimeSettingsPopup_ = true;
        }

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_CHECK_CIRCLE " 検証", ImVec2(102.0f, 0.0f))) {
            const SceneSerializer::SceneAssetValidationResult validation =
                editor_->ValidateSceneAsset(selectedAsset->filename);
            if (!validation.errors.empty()) {
                sceneAssetStatusMessage_ = "エラー: " + validation.errors.front();
                sceneAssetStatusIsError_ = true;
            }
            else if (!validation.warnings.empty()) {
                sceneAssetStatusMessage_ = "警告: " + validation.warnings.front();
                sceneAssetStatusIsError_ = false;
            }
            else {
                sceneAssetStatusMessage_ = "Scene Assetの検証に成功しました。";
                sceneAssetStatusIsError_ = false;
            }
        }
    }

    // 遷移中の作成・開く・複製・改名・削除は、Scene状態が変わるため無効化する。
    ImGui::BeginDisabled(sceneTransitioning);

    if (ImGui::Button(ICON_FA_PLUS " 新規", ImVec2(84.0f, 0.0f))) {
        OpenCreateSceneDialog();
    }

    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(sceneTransitioning || selectedAsset == assets.end() ||
        (selectedAssetIsActive && !selectedRuntimeNeedsReload));
    if (ImGui::Button(
        selectedRuntimeNeedsReload ? ICON_FA_SYNC " 再読込" : ICON_FA_FOLDER_OPEN " 開く",
        ImVec2(84.0f, 0.0f))) {
        RequestOpen(selectedSceneAssetFilename_);
    }

    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(sceneTransitioning || selectedAsset == assets.end());

    if (ImGui::Button(ICON_FA_COPY " 複製", ImVec2(84.0f, 0.0f))) {
        sceneAssetStatusMessage_.clear();
        sceneAssetStatusIsError_ = false;
        const std::string duplicateId = selectedAsset->id + "_copy";
        strcpy_s(duplicateSceneId_, sizeof(duplicateSceneId_), duplicateId.c_str());
        strcpy_s(duplicateSceneDisplayName_, sizeof(duplicateSceneDisplayName_), selectedAsset->displayName.c_str());
        requestDuplicateScenePopup_ = true;
    }

    ImGui::EndDisabled();

    ImGui::BeginDisabled(sceneTransitioning || selectedAsset == assets.end());

    if (ImGui::Button(ICON_FA_EDIT " 名前変更", ImVec2(122.0f, 0.0f))) {
        sceneAssetStatusMessage_.clear();
        sceneAssetStatusIsError_ = false;
        strcpy_s(renameSceneId_, sizeof(renameSceneId_), selectedAsset->id.c_str());
        strcpy_s(renameSceneDisplayName_, sizeof(renameSceneDisplayName_), selectedAsset->displayName.c_str());
        requestRenameScenePopup_ = true;
    }

    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool canDelete = selectedAsset != assets.end() && !editor_->IsCurrentSceneAsset(selectedSceneAssetFilename_);
    ImGui::BeginDisabled(sceneTransitioning || !canDelete);

    if (ImGui::Button(ICON_FA_TRASH_ALT " 削除", ImVec2(102.0f, 0.0f))) {
        sceneAssetStatusMessage_.clear();
        sceneAssetStatusIsError_ = false;
        requestDeleteScenePopup_ = true;
    }

    ImGui::EndDisabled();

    if (!canDelete &&
        selectedAsset != assets.end() &&
        editor_->IsCurrentSceneAsset(selectedSceneAssetFilename_) &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Active Sceneは削除できません。別のSceneを開いてください。");
    }

    if (!sceneAssetStatusMessage_.empty()) {
        ImGui::TextColored(
            sceneAssetStatusIsError_ ? ImVec4(1.0f, 0.38f, 0.32f, 1.0f) : ImVec4(0.45f, 1.0f, 0.58f, 1.0f),
            "%s",
            sceneAssetStatusMessage_.c_str());
    }

    // 保存単位ごとの操作とActive Scene全体の保存を、Dirty概要の下へ並べる。
    ImGui::SeparatorText("保存");

    if (editor_->HasAnyDirty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.2f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " %s", editor_->GetDirtySummaryText().c_str());
    }
    else {
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), ICON_FA_CHECK_CIRCLE " %s", editor_->GetDirtySummaryText().c_str());
    }

    ImGui::BeginDisabled(sceneTransitioning);
    if (ImGui::Button(ICON_FA_USER " Player"))
        editor_->SaveScene(SaveMode::Player);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SKULL " Enemy"))
        editor_->SaveScene(SaveMode::Enemy);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CUBE " Object"))
        editor_->SaveScene(SaveMode::Object);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_VIDEO " Camera"))
        editor_->SaveScene(SaveMode::Camera);
    if (ImGui::Button(ICON_FA_DOWNLOAD " Active Sceneを保存", ImVec2(-1.0f, 0.0f))) {
        EditorCommandRegistry::GetInstance()->Execute(EditorCommandId::SceneSave);
    }

    ImGui::EndDisabled();
    ImGui::TextDisabled("保存先: Resources/json/3Dobject/%s", currentFilename.c_str());
#endif
}

void SceneAssetPanel::RequestOpen(const std::string& filename) {
    // 現在のAssetを再指定した場合も、実行クラスの整合を確認して開きます。
    if (!editor_ || filename.empty()) {
        return;
    }

    if (editor_->IsCurrentSceneAsset(filename)) {
        const std::vector<SceneSerializer::SceneAssetInfo> assets = editor_->GetSceneAssets();
        const auto asset = std::find_if(assets.begin(), assets.end(), [&](const SceneSerializer::SceneAssetInfo& candidate) {
            return candidate.filename == filename;
        });
        SceneManager* sceneManager = editor_->GetSceneManager();

        if (asset == assets.end() || !sceneManager) {
            return;
        }

        const SceneLoadContext& activeContext = sceneManager->GetActiveSceneLoadContext();
        const bool settingsMatch =
            asset->runtimeScene == activeContext.runtimeScene &&
            asset->controllerName == activeContext.controllerName &&
            asset->bgmPath == activeContext.bgmPath &&
            asset->lightPath == activeContext.lightPath &&
            asset->cameraPath == activeContext.cameraPath &&
            asset->skyboxPath == activeContext.skyboxPath;
        // 実行設定が同じ現在の Scene を選び直しても、再読み込みや保存確認を起こさない。
        if (settingsMatch)
            return;
    }

    if (editor_->HasAnyDirty()) {
        // 切替前に編集中の変更を扱う必要があるため、ここではファイル名だけ保留する。
        pendingOpenSceneAssetFilename_ = filename;
        requestUnsavedOpenPopup_ = true;
        return;
    }

    std::string errorMessage;

    if (!editor_->OpenSceneAsset(filename, false, errorMessage)) {
        sceneAssetStatusMessage_ = errorMessage;
        sceneAssetStatusIsError_ = true;
    }
    else {
        sceneAssetStatusMessage_ = "Sceneを開いています: " + filename;
        sceneAssetStatusIsError_ = false;
    }
}

void SceneAssetPanel::DrawDialogs() {
#ifdef USE_IMGUI
    // 作成・変更・削除の確認をモーダルに集約し、確定前のSceneを保護します。
    if (!editor_) {
        return;
    }

    // メニューやボタンで立てた要求フラグを、一フレームだけOpenPopupへ渡す。
    if (requestCreateScenePopup_) {
        ImGui::OpenPopup("新規Scene Asset###CreateSceneAsset");
        requestCreateScenePopup_ = false;
    }

    if (requestDuplicateScenePopup_) {
        ImGui::OpenPopup("Scene Assetを複製###DuplicateSceneAsset");
        requestDuplicateScenePopup_ = false;
    }

    if (requestRenameScenePopup_) {
        ImGui::OpenPopup("Scene Assetの名前変更###RenameSceneAsset");
        requestRenameScenePopup_ = false;
    }

    if (requestDeleteScenePopup_) {
        ImGui::OpenPopup("Scene Assetを削除###DeleteSceneAsset");
        requestDeleteScenePopup_ = false;
    }

    if (requestUnsavedOpenPopup_) {
        ImGui::OpenPopup("未保存のScene変更###OpenSceneAssetUnsaved");
        requestUnsavedOpenPopup_ = false;
    }

    if (requestRuntimeSettingsPopup_) {
        ImGui::OpenPopup("Scene Asset実行設定###SceneAssetRuntimeSettings");
        requestRuntimeSettingsPopup_ = false;
    }

    // 実行設定はControllerと各参照ファイルをまとめて保存する。
    ImGui::SetNextWindowSize(ImVec2(620.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(
        "Scene Asset実行設定###SceneAssetRuntimeSettings",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::vector<std::string>& controllerNames =
            SceneControllerFactory::GetInstance()->GetRegisteredNames();
        if (!controllerNames.empty()) {
            // 一覧が変わっても古い選択位置で範囲外を参照しないよう、表示前に補正する。
            runtimeSettingsControllerIndex_ = std::clamp(
                runtimeSettingsControllerIndex_,
                0,
                static_cast<int>(controllerNames.size()) - 1);
            if (ImGui::BeginCombo(
                "Scene Controller",
                controllerNames[runtimeSettingsControllerIndex_].c_str())) {
                for (int index = 0; index < static_cast<int>(controllerNames.size()); ++index) {
                    const bool isSelected = index == runtimeSettingsControllerIndex_;

                    if (ImGui::Selectable(controllerNames[index].c_str(), isSelected)) {
                        runtimeSettingsControllerIndex_ = index;
                    }

                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }
        }

        ImGui::InputText("BGM", runtimeBgmPath_, sizeof(runtimeBgmPath_));
        ImGui::InputText("Light JSON", runtimeLightPath_, sizeof(runtimeLightPath_));
        ImGui::InputText("Camera JSON", runtimeCameraPath_, sizeof(runtimeCameraPath_));
        ImGui::InputText("Skybox", runtimeSkyboxPath_, sizeof(runtimeSkyboxPath_));
        ImGui::TextDisabled("空欄は選択したC++ Sceneクラスの既定値を使用します。");
        ImGui::TextDisabled("Camera JSONはResources/json/camera以下のファイル名を指定します。");
        ImGui::Separator();

        ImGui::BeginDisabled(controllerNames.empty());

        if (ImGui::Button(ICON_FA_SAVE " 保存", ImVec2(120.0f, 0.0f))) {
            std::string errorMessage;
            if (editor_->SetSceneAssetRuntimeSettings(
                selectedSceneAssetFilename_,
                controllerNames[runtimeSettingsControllerIndex_],
                runtimeBgmPath_,
                runtimeLightPath_,
                runtimeCameraPath_,
                runtimeSkyboxPath_,
                errorMessage)) {
                sceneAssetStatusMessage_ = "Scene Assetの実行設定を保存しました。";
                sceneAssetStatusIsError_ = false;
                ImGui::CloseCurrentPopup();
            }
            else {
                sceneAssetStatusMessage_ = errorMessage;
                sceneAssetStatusIsError_ = true;
            }
        }

        ImGui::EndDisabled();
        ImGui::SameLine();

        if (ImGui::Button("キャンセル", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // 新規Assetは空Sceneか現在Sceneの複製を選び、登録済みの実行クラスへ紐付ける。
    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("新規Scene Asset###CreateSceneAsset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Scene IDはファイル名に使用します。表示名には日本語を使用できます。");
        ImGui::InputText("Scene ID", createSceneId_, sizeof(createSceneId_));
        ImGui::InputText("表示名", createSceneDisplayName_, sizeof(createSceneDisplayName_));
        const char* templates[] = { "空のScene", "現在のSceneを複製" };
        ImGui::Combo("テンプレート", &createSceneTemplate_, templates, IM_ARRAYSIZE(templates));
        const std::vector<std::string> runtimeScenes = editor_->GetRegisteredSceneNames();

        if (!runtimeScenes.empty()) {
            createRuntimeSceneIndex_ = std::clamp(
                createRuntimeSceneIndex_,
                0,
                static_cast<int>(runtimeScenes.size()) - 1);
            if (ImGui::BeginCombo("実行クラス", runtimeScenes[createRuntimeSceneIndex_].c_str())) {
                for (int index = 0; index < static_cast<int>(runtimeScenes.size()); ++index) {
                    const bool isSelected = index == createRuntimeSceneIndex_;

                    if (ImGui::Selectable(runtimeScenes[index].c_str(), isSelected)) {
                        createRuntimeSceneIndex_ = index;
                    }

                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndCombo();
            }
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.32f, 1.0f), "登録済みの実行クラスがありません。");
        }

        if (createSceneTemplate_ == 1) {
            ImGui::TextDisabled("現在のメモリ上のObjectと関連Spriteレイアウトを複製します。");
        }

        if (sceneAssetStatusIsError_ && !sceneAssetStatusMessage_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.32f, 1.0f), "%s", sceneAssetStatusMessage_.c_str());
        }

        ImGui::Separator();

        // 作成のみと作成後に開く操作で同じ検証・生成経路を使用する。
        const auto createAsset = [&](bool openAfterCreate) {
            if (runtimeScenes.empty()) {
                sceneAssetStatusMessage_ = "SceneFactoryに実行クラスが登録されていません。";
                sceneAssetStatusIsError_ = true;
                return false;
            }

            std::string createdFilename;
            std::string errorMessage;
            const SceneSerializer::SceneAssetTemplate sceneTemplate = createSceneTemplate_ == 0
                ? SceneSerializer::SceneAssetTemplate::Empty
                : SceneSerializer::SceneAssetTemplate::CurrentScene;
            if (!editor_->CreateSceneAsset(
                createSceneId_,
                createSceneDisplayName_,
                runtimeScenes[createRuntimeSceneIndex_],
                sceneTemplate,
                createdFilename,
                errorMessage)) {
                sceneAssetStatusMessage_ = errorMessage;
                sceneAssetStatusIsError_ = true;
                return false;
            }

            selectedSceneAssetFilename_ = createdFilename;
            sceneAssetStatusMessage_ = "Scene Assetを作成しました: " + createdFilename;
            sceneAssetStatusIsError_ = false;

            if (openAfterCreate) {
                RequestOpen(createdFilename);
            }

            return true;
        };

        if (ImGui::Button(ICON_FA_FOLDER_OPEN " 作成して開く", ImVec2(150.0f, 0.0f))) {
            if (createAsset(true)) {
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_PLUS " 作成のみ", ImVec2(120.0f, 0.0f))) {
            if (createAsset(false)) {
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("キャンセル", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // 複製・改名は成功後に選択中Assetを新しいファイル名へ切り替える。
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Scene Assetを複製###DuplicateSceneAsset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("複製元: %s", selectedSceneAssetFilename_.c_str());
        ImGui::InputText("新しいScene ID", duplicateSceneId_, sizeof(duplicateSceneId_));
        ImGui::InputText("新しい表示名", duplicateSceneDisplayName_, sizeof(duplicateSceneDisplayName_));

        if (sceneAssetStatusIsError_ && !sceneAssetStatusMessage_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.32f, 1.0f), "%s", sceneAssetStatusMessage_.c_str());
        }

        ImGui::Separator();

        if (ImGui::Button(ICON_FA_COPY " 複製", ImVec2(120.0f, 0.0f))) {
            std::string createdFilename;
            std::string errorMessage;
            if (editor_->DuplicateSceneAsset(
                selectedSceneAssetFilename_,
                duplicateSceneId_,
                duplicateSceneDisplayName_,
                createdFilename,
                errorMessage)) {
                selectedSceneAssetFilename_ = createdFilename;
                sceneAssetStatusMessage_ = "Scene Assetを複製しました: " + createdFilename;
                sceneAssetStatusIsError_ = false;
                ImGui::CloseCurrentPopup();
            }
            else {
                sceneAssetStatusMessage_ = errorMessage;
                sceneAssetStatusIsError_ = true;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("キャンセル", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Scene Assetの名前変更###RenameSceneAsset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string sourceFilename = selectedSceneAssetFilename_;
        ImGui::InputText("Scene ID", renameSceneId_, sizeof(renameSceneId_));
        ImGui::InputText("表示名", renameSceneDisplayName_, sizeof(renameSceneDisplayName_));
        ImGui::TextDisabled("Active Sceneを変更した場合は、以降の保存先も新しい名前へ切り替わります。");

        if (sceneAssetStatusIsError_ && !sceneAssetStatusMessage_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.32f, 1.0f), "%s", sceneAssetStatusMessage_.c_str());
        }

        ImGui::Separator();

        if (ImGui::Button(ICON_FA_EDIT " 名前変更", ImVec2(130.0f, 0.0f))) {
            std::string renamedFilename;
            std::string errorMessage;
            if (editor_->RenameSceneAsset(
                sourceFilename,
                renameSceneId_,
                renameSceneDisplayName_,
                renamedFilename,
                errorMessage)) {
                selectedSceneAssetFilename_ = renamedFilename;
                sceneAssetStatusMessage_ = "Scene Assetの名前を変更しました: " + renamedFilename;
                sceneAssetStatusIsError_ = false;
                ImGui::CloseCurrentPopup();
            }
            else {
                sceneAssetStatusMessage_ = errorMessage;
                sceneAssetStatusIsError_ = true;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("キャンセル", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // 削除は関連JSONも対象となるため、対象名を示した専用Modalで確定する。
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Scene Assetを削除###DeleteSceneAsset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " この操作は元に戻せません。");
        ImGui::TextWrapped("%s と関連する分割JSON、Spriteレイアウトを削除します。", selectedSceneAssetFilename_.c_str());

        if (sceneAssetStatusIsError_ && !sceneAssetStatusMessage_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.32f, 1.0f), "%s", sceneAssetStatusMessage_.c_str());
        }

        ImGui::Separator();

        if (ImGui::Button(ICON_FA_TRASH_ALT " 削除する", ImVec2(130.0f, 0.0f))) {
            const std::string deletedFilename = selectedSceneAssetFilename_;
            std::string errorMessage;

            if (editor_->DeleteSceneAsset(deletedFilename, errorMessage)) {
                selectedSceneAssetFilename_.clear();
                sceneAssetStatusMessage_ = "Scene Assetを削除しました: " + deletedFilename;
                sceneAssetStatusIsError_ = false;
                ImGui::CloseCurrentPopup();
            }
            else {
                sceneAssetStatusMessage_ = errorMessage;
                sceneAssetStatusIsError_ = true;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("キャンセル", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // 未保存Sceneを開き直すときは保存・破棄・取消の三択をここで確定する。
    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("未保存のScene変更###OpenSceneAssetUnsaved", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.28f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " 未保存の変更があります。");
        ImGui::TextWrapped("%s を開く前に、現在のSceneを保存しますか？", pendingOpenSceneAssetFilename_.c_str());
        ImGui::TextDisabled("%s", editor_->GetDirtySummaryText().c_str());
        ImGui::Separator();

        // 保存確認と後続の Scene 切り替えは DebugEditor に渡し、このダイアログでは保留要求を解除する。
        if (ImGui::Button(ICON_FA_SAVE " 保存して開く", ImVec2(150.0f, 0.0f))) {
            editor_->SaveThenOpenSceneAsset(pendingOpenSceneAssetFilename_);
            pendingOpenSceneAssetFilename_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_FOLDER_OPEN " 保存せず開く", ImVec2(150.0f, 0.0f))) {
            const std::string filename = pendingOpenSceneAssetFilename_;
            pendingOpenSceneAssetFilename_.clear();
            std::string errorMessage;

            if (!editor_->OpenSceneAsset(filename, true, errorMessage)) {
                sceneAssetStatusMessage_ = errorMessage;
                sceneAssetStatusIsError_ = true;
            }

            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("キャンセル", ImVec2(100.0f, 0.0f))) {
            pendingOpenSceneAssetFilename_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
#endif
}
