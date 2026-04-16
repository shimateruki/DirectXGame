#include "InspectorWindow.h"
#include "DebugEditor.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3d.h"
#include "imgui.h"
#include "IconsFontAwesome5.h"
#include "EditorManager.h"
#include "ParticleManager.h"
#include "ModelManager.h"
#include "GhostRecorder.h"
#include "DebugConsole.h"
#include "ImGuizmo.h"
#include <filesystem>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static const float PI = (float)M_PI;
static float ToRadians(float degrees) { return degrees * (PI / 180.0f); }
static float ToDegrees(float radians) { return radians * (180.0f / PI); }
namespace fs = std::filesystem;
void InspectorWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
}

void InspectorWindow::Draw() {
#ifdef USE_IMGUI
    if (editor_->GetSceneManager() == nullptr) return;
    BaseScene* currentScene = editor_->GetSceneManager()->GetCurrentScene();
    if (currentScene == nullptr) return;

    std::string currentJsonPath = "Resources/json/3Dobject/" + std::string(editor_->GetCurrentSceneFilenameBuffer());
    Object3d* selectedObject = editor_->GetSelectedObject();

    // ---------------------------------------------------------
    // 2. オブジェクト詳細 (Inspector本体)
    // ---------------------------------------------------------
    if (selectedObject == nullptr) {
        ImGui::TextDisabled(ICON_FA_EXCLAMATION_CIRCLE " オブジェクトが選択されていません");
        ImGui::TextDisabled("Hierarchyから選択してください");
        ImGui::Separator();
        // ★ ゲッターを使って安全にポインタアクセス
        ImGui::Checkbox(ICON_FA_EYE " コライダー枠を描画", editor_->GetDrawCollidersPtr());
    }
    else {
        // --- 名前表示 ---
        char nameBuffer[256];
        std::string currentName = selectedObject->GetName();
        if (currentName.empty()) currentName = "NoName";
        strcpy_s(nameBuffer, currentName.c_str());

        if (ImGui::InputText(ICON_FA_TAG " 名前", nameBuffer, sizeof(nameBuffer))) {
            selectedObject->SetName(std::string(nameBuffer));
        }
        ImGui::Spacing();

        if (editor_->GetIsPathEditMode()) {
            // 編集モード中は目立つ色（オレンジ）にして警告を表示
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.4f, 0.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
            if (ImGui::Button(ICON_FA_CHECK_CIRCLE " パス編集を完了してロックを解除 (Exit Edit Mode)", ImVec2(-1, 45))) {
                editor_->SetIsPathEditMode(false);
                if (selectedObject->recorder_) selectedObject->recorder_->DeselectPin();
            }
            ImGui::PopStyleColor(2);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " 現在パスを編集中です。");
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "    本体の移動・選択・削除はロックされています。");
        }
        else {
            if (ImGui::Button(ICON_FA_PENCIL_ALT " パスの軌跡を編集する (Enter Path Edit Mode)", ImVec2(-1, 35))) {
                editor_->SetIsPathEditMode(true);
            }
        }
        ImGui::Separator();

        // =========================================================================
        // 🚨 ここから下は「パス編集中」はロック（操作不能）にするエリア
        // =========================================================================
        ImGui::BeginDisabled(editor_->GetIsPathEditMode());
        ImGui::Spacing();

        if (ImGui::Button(ICON_FA_COPY " 複製 (Duplicate)")) {
            editor_->DuplicateSelected();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_DOWNLOAD " 単体保存 (JSON更新)")) {
            editor_->SaveSingleObject();
        }
        ImGui::Spacing();

        // --- クラス名表示 ---
        ImGui::TextDisabled(ICON_FA_CUBES " クラス: %s", selectedObject->GetClassName().c_str());
        const char* saveCategories[] = { "Object", "Player", "Enemy" };
        std::string currentCat = selectedObject->GetSaveCategory();
        int catIndex = 0;
        if (currentCat == "Player") catIndex = 1;
        else if (currentCat == "Enemy") catIndex = 2;

        if (ImGui::Combo(ICON_FA_FOLDER " 保存先カテゴリ", &catIndex, saveCategories, IM_ARRAYSIZE(saveCategories))) {
            selectedObject->SetSaveCategory(saveCategories[catIndex]);
        }

        // --- 親の名前表示 ---
        if (selectedObject->GetParent()) {
            ImGui::TextDisabled(ICON_FA_SITEMAP " 親: %s", selectedObject->GetParent()->GetName().c_str());
            if (ImGui::Button(ICON_FA_UNLINK " 親を解除 (Unparent)")) {
                selectedObject->SetParent(nullptr);
            }
        }
        else {
            ImGui::TextDisabled(ICON_FA_SITEMAP " 親: なし");
        }

        // --- Model Asset (InvisibleBoxでない場合のみ表示) ---
        if (selectedObject->GetClassName() != "InvisibleBox") {
            ImGui::Separator();
            ImGui::Text(ICON_FA_CUBE " モデルアセット: %s", selectedObject->GetModelName().c_str());
            ImGui::Button(ICON_FA_BOX_OPEN " [ ここにモデルをドロップして変更 ] ", ImVec2(-1, 30));

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
                    const char* modelName = (const char*)payload->Data;
                    ModelManager::GetInstance()->LoadModel(modelName);
                    selectedObject->SetModel(modelName);
                    DebugConsole::GetInstance()->AddLog("Switched model to: " + std::string(modelName));
                }
                ImGui::EndDragDropTarget();
            }
        }

        // --- 可視性設定 ---
        ImGui::Separator();
        bool isVisible = selectedObject->GetIsVisible();
        if (ImGui::Checkbox(ICON_FA_EYE " 表示 (ゲーム内)", &isVisible)) {
            selectedObject->SetIsVisible(isVisible);
        }

        // --- Transform編集 ---
        ImGui::Separator();
        ImGui::Text(ICON_FA_ARROWS_ALT " トランスフォーム (Transform)");
        Transform* transform = selectedObject->GetTransform();
        bool isTransformChanged = false;

        if (ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 座標 (Pos)", &transform->translate.x, 0.1f)) isTransformChanged = true;

        Vector3 rotDeg = { ToDegrees(transform->rotate.x), ToDegrees(transform->rotate.y), ToDegrees(transform->rotate.z) };
        if (ImGui::DragFloat3(ICON_FA_SYNC " 回転 (Rot)", &rotDeg.x, 1.0f, -360.0f, 360.0f)) {
            transform->rotate = { ToRadians(rotDeg.x), ToRadians(rotDeg.y), ToRadians(rotDeg.z) };
            transform->isQuaternionMaster = false;
            isTransformChanged = true;
        }
        if (ImGui::DragFloat3(ICON_FA_EXPAND_ARROWS_ALT " スケール (Scale)", &transform->scale.x, 0.05f)) isTransformChanged = true;


        // --- コライダー設定 ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader(ICON_FA_SHIELD_ALT " コリジョン設定 (Collision)", ImGuiTreeNodeFlags_DefaultOpen)) {
            Object3d::ColliderConfig colConfig = selectedObject->GetColliderConfig();
            bool isColChanged = false;

            const char* typeNames[] = { "なし (None)", "球 (Sphere)", "箱 (AABB)", "回転箱 (OBB)", "円柱 (Cylinder)" };
            int currentTypeIndex = (int)colConfig.type;
            if (ImGui::Combo(ICON_FA_SHAPES " 形状タイプ", &currentTypeIndex, typeNames, IM_ARRAYSIZE(typeNames))) {
                colConfig.type = (ColliderType)currentTypeIndex;
                if (colConfig.type == ColliderType::kOBB && colConfig.size.x == 0.0f) {
                    colConfig.size = { 1.0f, 1.0f, 1.0f };
                }
                isColChanged = true;
            }

         if (colConfig.type != ColliderType::kNone) {
                if (ImGui::DragFloat3("中心オフセット", &colConfig.center.x, 0.05f)) isColChanged = true;

                // 形状ごとのサイズ変更UIの分岐を整理
                if (colConfig.type == ColliderType::kSphere) {
                    if (ImGui::DragFloat("半径 (Radius)", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) {
                        colConfig.size.y = colConfig.size.z = colConfig.size.x;
                        isColChanged = true;
                    }
                }
                // ★ 円柱の分岐をここに入れる（else if にする）
                else if (colConfig.type == ColliderType::kCylinder) {
                    // if文にして isColChanged = true をつける！
                    if (ImGui::DragFloat("Radius (X)", &colConfig.size.x, 0.1f, 0.0f, 100.0f)) isColChanged = true;
                    if (ImGui::DragFloat("Height (Y)", &colConfig.size.y, 0.1f, 0.0f, 100.0f)) isColChanged = true;

                    // Z軸を強制的に半径(X)と同期させておく
                    colConfig.size.z = colConfig.size.x;
                    ImGui::TextDisabled("※Z軸の値は半径(X)と同期します");
                }
                // 箱(AABB)や回転箱(OBB)の場合
                else {
                    if (ImGui::DragFloat3("サイズ (Size)", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) isColChanged = true;
                }

                // OBBの時の回転UI
                if (colConfig.type == ColliderType::kOBB) {
                    Vector3 rotDegObj = { ToDegrees(colConfig.rotation.x), ToDegrees(colConfig.rotation.y), ToDegrees(colConfig.rotation.z) };
                    if (ImGui::DragFloat3("回転 (Rotation)", &rotDegObj.x, 1.0f, -360.0f, 360.0f)) {
                        colConfig.rotation = { ToRadians(rotDegObj.x), ToRadians(rotDegObj.y), ToRadians(rotDegObj.z) };
                        isColChanged = true;
                    }
                }

                if (isColChanged) {
                    selectedObject->SetColliderConfig(colConfig);
                }
            }
            ImGui::Separator();
            if (ImGui::CollapsingHeader(ICON_FA_PALETTE " グラフィックス (Material)", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool isGraphicsChanged = false;
                const char* matTypes[] = {
                                "通常 (Standard)", "ガラス (Glass)", "氷・宝石 (Ice/Crystal)",
                                "ホログラム (Hologram)", "消滅 (Dissolve)", "マグマ・覚醒 (Emissive)",
                                "トゥーン調 (Cel Shaded)", "ローカルフォグ (Local Fog)",
                };
                int currentMatType = selectedObject->GetMaterialType();
                if (currentMatType < 0) currentMatType = 0;
                if (currentMatType > 7) currentMatType = 0;
                if (ImGui::Combo(ICON_FA_PAINT_BRUSH " 質感 (Material Type)", &currentMatType, matTypes, IM_ARRAYSIZE(matTypes))) {
                    selectedObject->SetMaterialType(currentMatType);
                    isGraphicsChanged = true;
                }

                if (currentMatType == 0) {
                    float metallic = selectedObject->GetMetallic();
                    if (ImGui::SliderFloat("金属度 (Metallic)", &metallic, 0.0f, 1.0f)) {
                        selectedObject->SetMetallic(metallic); isGraphicsChanged = true;
                    }
                    float roughness = selectedObject->GetRoughness();
                    if (ImGui::SliderFloat("粗さ (Roughness)", &roughness, 0.0f, 1.0f)) {
                        selectedObject->SetRoughness(roughness); isGraphicsChanged = true;
                    }
                }

                if (currentMatType == 7) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), ICON_FA_SMOG " --- Local Fog Settings ---");
                    auto* fogData = selectedObject->GetLocalFogData();
                    if (fogData) {
                        ImGui::ColorEdit4("Fog Color (霧の色)", &fogData->fogColor.x);
                        ImGui::DragFloat("Density (濃さ)", &fogData->fogDensity, 0.01f, 0.0f, 10.0f);
                        ImGui::DragFloat("Edge Fade (境界線のボケ)", &fogData->edgeFade, 0.01f, 0.0f, 1.5f);
                        ImGui::DragFloat("Noise Speed (揺らぐ速さ)", &fogData->noiseSpeed, 0.01f, 0.0f, 10.0f);
                        ImGui::DragFloat("Noise Scale (模様の細かさ)", &fogData->noiseScale, 0.01f, 0.0f, 10.0f);
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), ICON_FA_SUN " --- Light Scattering ---");
                        ImGui::DragFloat("Scattering G (光の芯の強さ)", &fogData->scatteringG, 0.01f, 0.0f, 0.99f);
                        ImGui::DragFloat("Light Intensity (光の明るさ)", &fogData->scatteringIntensity, 0.01f, 0.0f, 5.0f);
                    }
                }
                ImGui::Separator();
                bool enableNormal = selectedObject->GetEnableNormalMap();
                if (ImGui::Checkbox(ICON_FA_MAP " 法線マップ (Normal Map) 有効化", &enableNormal)) {
                    selectedObject->SetEnableNormalMap(enableNormal); isGraphicsChanged = true;
                }

                ImGui::Separator();
                bool enableEnv = selectedObject->GetEnableEnvMap();
                if (ImGui::Checkbox(ICON_FA_GLOBE " 環境マップ (IBL) 有効化", &enableEnv)) {
                    selectedObject->SetEnableEnvMap(enableEnv); isGraphicsChanged = true;
                }
                if (enableEnv) {
                    float envIntensity = selectedObject->GetEnvIntensity();
                    if (ImGui::SliderFloat("環境マップ強度", &envIntensity, 0.0f, 5.0f)) {
                        selectedObject->SetEnvIntensity(envIntensity); isGraphicsChanged = true;
                    }
                }
                if (enableNormal) {
                    static std::vector<std::string> texturePaths;
                    static bool isListInitialized = false;

                    if (!isListInitialized) {
                        texturePaths.clear();
                        std::string targetDir = "Resources/sprite/";
                        if (std::filesystem::exists(targetDir)) {
                            for (const auto& entry : std::filesystem::recursive_directory_iterator(targetDir)) {
                                if (entry.is_regular_file()) {
                                    std::string ext = entry.path().extension().string();
                                    if (ext == ".png" || ext == ".jpg" || ext == ".dds") {
                                        std::string pathString = entry.path().string();
                                        std::replace(pathString.begin(), pathString.end(), '\\', '/');
                                        texturePaths.push_back(pathString);
                                    }
                                }
                            }
                        }
                        isListInitialized = true;
                    }

                    std::string currentPath = selectedObject->GetNormalMapPath();
                    const char* previewValue = currentPath.empty() ? "未設定 (クリックで選択)" : currentPath.c_str();

                    if (ImGui::BeginCombo(ICON_FA_IMAGE " ノーマル画像", previewValue)) {
                        for (const std::string& path : texturePaths) {
                            bool isSelected = (currentPath == path);
                            if (ImGui::Selectable(path.c_str(), isSelected)) {
                                selectedObject->SetNormalMap(path); isGraphicsChanged = true;
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::Separator();
                        if (ImGui::Selectable("なし (クリア)", currentPath.empty())) {
                            selectedObject->SetNormalMap(""); isGraphicsChanged = true;
                        }
                        ImGui::EndCombo();
                    }

                    std::string currentOrmPath = selectedObject->GetOrmMapPath();
                    const char* previewOrmValue = currentOrmPath.empty() ? "未設定 (クリックで選択)" : currentOrmPath.c_str();

                    if (ImGui::BeginCombo(ICON_FA_IMAGE " ORMマップ (AO/粗さ/金属)", previewOrmValue)) {
                        for (const std::string& path : texturePaths) {
                            bool isSelected = (currentOrmPath == path);
                            if (ImGui::Selectable(path.c_str(), isSelected)) {
                                selectedObject->SetOrmMap(path); isGraphicsChanged = true;
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::Separator();
                        if (ImGui::Selectable("なし (クリア)", currentOrmPath.empty())) {
                            selectedObject->SetOrmMap(""); isGraphicsChanged = true;
                        }
                        ImGui::EndCombo();
                    }
                    std::string currentTexturePath = selectedObject->GetTexturePath();
                    const char* previewTextureValue = currentTexturePath.empty() ? "デフォルト (モデル固有)" : currentTexturePath.c_str();

                    if (ImGui::BeginCombo(ICON_FA_IMAGE " 基本画像 (Diffuse)", previewTextureValue)) {
                        for (const std::string& path : texturePaths) {
                            bool isSelected = (currentTexturePath == path);
                            if (ImGui::Selectable(path.c_str(), isSelected)) {
                                selectedObject->SetTexture(path); isGraphicsChanged = true;
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::Separator();
                        if (ImGui::Selectable("デフォルトに戻す", currentTexturePath.empty())) {
                            selectedObject->SetTexture(""); isGraphicsChanged = true;
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_SYNC_ALT " 更新")) {
                        isListInitialized = false;
                    }
                }
                ImGui::Separator();
                const char* blendModes[] = { "なし (None)", "通常 (Normal)", "加算 (Add)", "減算 (Subtract)", "乗算 (Multiply)", "スクリーン (Screen)" };
                int currentBlend = static_cast<int>(selectedObject->GetBlendMode());

                if (ImGui::Combo(ICON_FA_ADJUST " 合成 (Blend Mode)", &currentBlend, blendModes, IM_ARRAYSIZE(blendModes))) {
                    selectedObject->SetBlendMode(static_cast<BlendMode>(currentBlend)); isGraphicsChanged = true;
                }

                Vector4 color = selectedObject->GetColor();
                if (ImGui::ColorEdit4(ICON_FA_FILL_DRIP " 色 (Color)", &color.x)) {
                    selectedObject->SetColor(color); isGraphicsChanged = true;
                }

                ImGui::Spacing();
                float emissive = selectedObject->GetEmissive();
                // 1.0 で光なし。HDR空間で限界突破させるため最大値は大きめ(50.0等)にしておく
                if (ImGui::DragFloat(ICON_FA_SUN " 発光強度 (Emissive)", &emissive, 0.1f, 1.0f, 50.0f, "%.1f")) {
                    selectedObject->SetEmissive(emissive);
                    isGraphicsChanged = true;
                }
            }

            ImGui::Separator();
            if (ImGui::CollapsingHeader(ICON_FA_FIRE " パーティクル")) {
                const auto& paramsMap = ParticleManager::GetInstance()->GetParamsMap();
                std::vector<const char*> itemNames;
                int currentItemIndex = 0; int index = 0;
                itemNames.push_back("None");

                std::string currentParticleName = selectedObject->GetParticleName();
                if (currentParticleName.empty()) currentItemIndex = 0;

                for (const auto& [name, param] : paramsMap) {
                    itemNames.push_back(name.c_str());
                    if (name == currentParticleName) currentItemIndex = index + 1;
                    index++;
                }

                if (ImGui::Combo("Effect Name", &currentItemIndex, itemNames.data(), (int)itemNames.size())) {
                    if (currentItemIndex == 0) selectedObject->SetParticleName("");
                    else selectedObject->SetParticleName(itemNames[currentItemIndex]);
                }

                if (!currentParticleName.empty() && paramsMap.find(currentParticleName) == paramsMap.end()) {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Warning: JSON not found!");
                }
            }

            ImGui::Separator();
            uint32_t currentAttr = selectedObject->GetCollisionAttribute();
            DrawAttributeSelector(ICON_FA_TAGS " 自分の属性 (Attribute)", &currentAttr);
            if (currentAttr != selectedObject->GetCollisionAttribute()) selectedObject->SetCollisionAttribute(currentAttr);

            uint32_t currentMask = selectedObject->GetCollisionMask();
            DrawAttributeSelector(ICON_FA_TAGS " 衝突対象 (Mask)", &currentMask);
            if (currentMask != selectedObject->GetCollisionMask()) selectedObject->SetCollisionMask(currentMask);
        }

        // --- Gimmick (ID設定) ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader(ICON_FA_LINK " ギミック設定 (Link IDs)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("イベント連携ID:");
            int tID = selectedObject->GetTargetID();
            if (ImGui::InputInt("送信先ID (Target)", &tID)) selectedObject->SetTargetID(tID);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("トリガーを作動させたい相手のIDを指定してください");

            int eID = selectedObject->GetEventID();
            if (ImGui::InputInt("自分ID (Event)", &eID)) selectedObject->SetEventID(eID);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("ギミック等から起動されるための、自分のIDを指定してください");
        }

        // ==========================================
        // 1. ボーンアニメーション設定
        // ==========================================
        ImGui::Separator();
        ImGui::Text(ICON_FA_BONE " 【ボーンアニメーション】");
        char animNameBuf[64];
        strncpy_s(animNameBuf, selectedObject->animName_.c_str(), sizeof(animNameBuf));
        if (ImGui::InputText("アニメ名##BoneAnim", animNameBuf, sizeof(animNameBuf))) {
            selectedObject->animName_ = animNameBuf;
        }
        ImGui::Checkbox("ループ再生##BoneAnim", &selectedObject->isAnimLoop_);

        // ==========================================
        // 2. パス移動 (GhostRecorder) 設定
        // ==========================================
        ImGui::Separator();
        ImGui::Text(ICON_FA_GHOST " 【パス移動 (GhostRecorder)】");
        std::string currentRecordPreview = selectedObject->recordPathName_.empty() ? "(なし)" : selectedObject->recordPathName_;

        if (ImGui::BeginCombo("パスデータ", currentRecordPreview.c_str())) {
            bool isNoneSelected = selectedObject->recordPathName_.empty();
            if (ImGui::Selectable("(なし)", isNoneSelected)) {
                selectedObject->recordPathName_ = "";
                if (selectedObject->recorder_) selectedObject->recorder_->Stop();
            }
            if (isNoneSelected) ImGui::SetItemDefaultFocus();

            std::string dirPath = "Resources/json/animation/";
            if (fs::exists(dirPath) && fs::is_directory(dirPath)) {
                for (const auto& entry : fs::directory_iterator(dirPath)) {
                    if (entry.path().extension() == ".json") {
                        std::string fileName = entry.path().stem().string();
                        bool isSelected = (selectedObject->recordPathName_ == fileName);

                        if (ImGui::Selectable(fileName.c_str(), isSelected)) {
                            selectedObject->recordPathName_ = fileName;
                            if (selectedObject->recorder_) {
                                bool isCinematic = (selectedObject->GetClassName() == "CinematicCamera");
                                selectedObject->recorder_->Play(selectedObject->recordPathName_, selectedObject->isRecordLoop_, selectedObject->isRecordRelative_, isCinematic);
                            }
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Checkbox("ループ再生##Record", &selectedObject->isRecordLoop_)) {
            if (selectedObject->recorder_ && !selectedObject->recordPathName_.empty()) {
                bool isCinematic = (selectedObject->GetClassName() == "CinematicCamera");
                selectedObject->recorder_->Play(selectedObject->recordPathName_, selectedObject->isRecordLoop_, selectedObject->isRecordRelative_, isCinematic);
            }
        }
        if (ImGui::Checkbox("相対座標モード##Record", &selectedObject->isRecordRelative_)) {
            if (selectedObject->recorder_ && !selectedObject->recordPathName_.empty()) {
                bool isCinematic = (selectedObject->GetClassName() == "CinematicCamera");
                selectedObject->recorder_->Play(selectedObject->recordPathName_, selectedObject->isRecordLoop_, selectedObject->isRecordRelative_, isCinematic);
            }
        }
        if (ImGui::Button("テスト再生##Record")) {
            if (selectedObject->recorder_ && !selectedObject->recordPathName_.empty()) {
                bool isCinematic = (selectedObject->GetClassName() == "CinematicCamera");
                selectedObject->recorder_->Play(selectedObject->recordPathName_, selectedObject->isRecordLoop_, selectedObject->isRecordRelative_, isCinematic);
            }
        }

        // --- Game Data (Stats) ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader(ICON_FA_GAMEPAD " ゲームデータ (Stats)", ImGuiTreeNodeFlags_DefaultOpen)) {
            EventType currentType = selectedObject->GetEventType();
            int currentItemIndex = static_cast<int>(currentType);
            const char* eventNames[] = { "なし", "ダメージ", "ワープ","中間ポイント","ゴール","ステージセレクト" };
            if (ImGui::Combo(ICON_FA_FLAG " イベント種類", &currentItemIndex, eventNames, IM_ARRAYSIZE(eventNames))) {
                selectedObject->SetEventType(static_cast<EventType>(currentItemIndex));
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "--- Object Type Settings ---");
            const char* classItems[] = { "Model", "Spawner", "Player", "Enemy", "InvisibleBox", "Block" };
            std::string currentClass = selectedObject->GetClassName();
            int currentClassIndex = 0;
            for (int i = 0; i < IM_ARRAYSIZE(classItems); i++) {
                if (currentClass == classItems[i]) { currentClassIndex = i; break; }
            }

            if (ImGui::Combo(ICON_FA_CUBES " Class Type", &currentClassIndex, classItems, IM_ARRAYSIZE(classItems))) {
                selectedObject->SetClassName(classItems[currentClassIndex]);
                if (std::string(classItems[currentClassIndex]) == "Spawner") {
                    if (selectedObject->GetName().find("Object") != std::string::npos) selectedObject->SetName("Spawner_New");
                    if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
                }
            }

            if (selectedObject->GetClassName() == "Spawner") DrawSpawnerSettings();

            ImGui::Spacing();
            if (selectedObject->GetClassName() == "Enemy") {
                ImGui::Indent(); DrawEnemyTypeSelector(); ImGui::Unindent();
            }

            if (!selectedObject->param_.has_value()) {
                if (ImGui::Button(ICON_FA_PLUS_CIRCLE " ステータスを追加", ImVec2(-1, 0))) selectedObject->param_.emplace();
            }
            else {
                auto& p = selectedObject->param_.value();
                ImGui::Text("エンティティ・ステータス:");
                ImGui::Indent();
                ImGui::DragFloat(ICON_FA_HEART " HP (体力)", &p.hp, 1.0f, 0.0f, 9999.0f);
                ImGui::DragFloat(ICON_FA_HEARTBEAT " Max HP", &p.maxHp, 1.0f, 1.0f, 9999.0f);
                ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 速度 (Speed)", &p.speed, 0.1f, 0.0f, 100.0f);
                ImGui::DragFloat(ICON_FA_ARROW_DOWN " 重力 (Gravity)", &p.gravity, 0.01f, -10.0f, 10.0f);
                ImGui::DragFloat(ICON_FA_ARROW_UP " ジャンプ力", &p.jumpPower, 0.1f, 0.0f, 100.0f);
                ImGui::Unindent();

                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
                if (ImGui::Button(ICON_FA_TRASH_ALT " ステータスを削除", ImVec2(-1, 0))) selectedObject->param_ = std::nullopt;
                ImGui::PopStyleColor();
            }
        }

        ImGui::EndDisabled();
        ImGui::Separator();

        if (ImGui::Button(ICON_FA_TRASH_ALT " オブジェクト削除", ImVec2(-1, 0))) {
            editor_->DeleteSelected();
            EditorManager::GetInstance()->ClearSelection();
        }

        // --- Gizmo 操作切替 ---
        ImGui::Separator();
        ImGui::Text(ICON_FA_HAND_POINTER " ギズモ操作モード:");
        static ImGuizmo::OPERATION currentOperation = ImGuizmo::TRANSLATE;
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
            if (ImGui::IsKeyPressed(ImGuiKey_T)) currentOperation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) currentOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_S)) currentOperation = ImGuizmo::SCALE;
        }
        if (ImGui::RadioButton("移動", currentOperation == ImGuizmo::TRANSLATE)) currentOperation = ImGuizmo::TRANSLATE; ImGui::SameLine();
        if (ImGui::RadioButton("回転", currentOperation == ImGuizmo::ROTATE)) currentOperation = ImGuizmo::ROTATE; ImGui::SameLine();
        if (ImGui::RadioButton("拡大縮小", currentOperation == ImGuizmo::SCALE)) currentOperation = ImGuizmo::SCALE;
    }
#endif
}

// -------------------------------------------------------------
// UIヘルパー関数群
// -------------------------------------------------------------
void InspectorWindow::DrawSpawnerSettings() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    ImGui::Separator();
    ImGui::Indent();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "[ Spawner Config ]");

    if (!selectedObject->param_.has_value()) {
        selectedObject->param_.emplace();
    }
    auto& p = selectedObject->param_.value();

    static char typeBuf[64] = "";
    if (typeBuf[0] == '\0') {
        strcpy_s(typeBuf, sizeof(typeBuf), p.enemyType.c_str());
    }

    const char* enemyTypes[] = { "Slime", };
    int currentTypeIndex = -1;
    for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
        if (p.enemyType == enemyTypes[i]) currentTypeIndex = i;
    }

    if (ImGui::Combo("Spawn Type", &currentTypeIndex, enemyTypes, IM_ARRAYSIZE(enemyTypes))) {
        p.enemyType = enemyTypes[currentTypeIndex];
        strcpy_s(typeBuf, sizeof(typeBuf), p.enemyType.c_str());
    }

    ImGui::DragFloat("Interval (sec)", &p.interval, 0.1f, 0.1f, 60.0f, "%.1f s");
    ImGui::InputInt("Max Count", &p.maxCount);

    ImGui::Unindent();
#endif
}

void InspectorWindow::DrawEnemyTypeSelector() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    const char* enemyTypes[] = { "Slime","Bomb", "BossCore" };
    std::string currentType = selectedObject->GetEnemyType();

    int currentIndex = -1;
    for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
        if (currentType == enemyTypes[i]) {
            currentIndex = i;
            break;
        }
    }

    const char* previewValue = (currentIndex >= 0) ? enemyTypes[currentIndex] : "(未設定)";

    if (ImGui::BeginCombo("敵の種族 (Enemy Type)", previewValue)) {
        for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
            bool isSelected = (currentIndex == i);
            if (ImGui::Selectable(enemyTypes[i], isSelected)) {
                selectedObject->SetEnemyType(enemyTypes[i]);
                selectedObject->SetName("Enemy_" + std::string(enemyTypes[i]));
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ロード時に生成される敵クラスを指定します。\nEmptyの場合はただの箱になります。");
#endif
}

void InspectorWindow::DrawAttributeSelector(const char* label, uint32_t* attribute) {
#ifdef USE_IMGUI
    if (ImGui::TreeNode(label)) {
        int flags = static_cast<int>(*attribute);
        ImGui::CheckboxFlags("プレイヤー (Player)", &flags, 1 << 0);
        ImGui::CheckboxFlags("敵 (Enemy)", &flags, 1 << 1);
        ImGui::CheckboxFlags("床・地形 (Ground)", &flags, 1 << 2);
        ImGui::CheckboxFlags("弾 (Bullet)", &flags, 1 << 3);
        ImGui::CheckboxFlags("トリガー (Trigger)", &flags, 1 << 4);
        ImGui::CheckboxFlags("プレイヤー攻撃 (PlayerAttack)", &flags, 1 << 6);
        ImGui::CheckboxFlags("敵攻撃 (EnemyAttack)", &flags, 1 << 7);
        *attribute = static_cast<uint32_t>(flags);
        ImGui::TreePop();
    }
#endif
}