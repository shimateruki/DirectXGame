#include "engine/utility/SystemDebug/editor/windows/inspector/InspectorWindow.Internal.h"
#include "engine/utility/SystemDebug/editor/windows/ProjectWindow.h"

using namespace InspectorWindowImplementation;

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
    const std::size_t selectedCount = editor_->GetSelectedObjectCount();

    // ---------------------------------------------------------
    // 2. オブジェクト詳細 (Inspector本体)
    // ---------------------------------------------------------
    if (selectedObject == nullptr) {
        ImGui::TextDisabled(ICON_FA_EXCLAMATION_CIRCLE " オブジェクトが選択されていません");
        ImGui::TextDisabled("Hierarchyから選択してください");
        ImGui::Separator();
        // ゲッター経由での描画フラグ制御
        ImGui::Checkbox(ICON_FA_EYE " コライダー枠を描画", editor_->GetDrawCollidersPtr());
        ImGui::Checkbox(ICON_FA_FINGERPRINT " イベントIDを表示", editor_->GetDrawEventIDsPtr());
    }
    else {
        std::vector<Object3d*> inspectorTargets = CollectInspectorTargets(editor_, selectedObject);
        const bool isMultiSelection = inspectorTargets.size() > 1;

        if (isMultiSelection) {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f),
                ICON_FA_CUBES " %zu個選択中 / 共通項目は一括編集されます", inspectorTargets.size());
            ImGui::TextDisabled("名前・親子・モデル分割など、代表Object専用の項目は代表Objectだけを編集します。");
            ImGui::Separator();
        }

        DrawPrefabInstancePanel(editor_, currentScene, selectedObject);

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
            EditorCommandRegistry::GetInstance()->Execute(EditorCommandId::EditDuplicate);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_DOWNLOAD " 単体保存 (JSON更新)")) {
            editor_->SaveSingleObject();
        }
        ImGui::Spacing();

        // --- クラス名表示 ---
        ImGui::TextDisabled(ICON_FA_CUBES " クラス: %s", selectedObject->GetClassName().c_str());

        if (selectedObject->IsCameraObject()) {
            DrawCameraObjectInspector(editor_, currentScene, selectedObject);
            ImGui::EndDisabled();
            return;
        }

        const bool isManagedCharacter = GameplayStatusManager::IsManagedCharacter(selectedObject);

        static Object3d* tagLayerBufferOwner = nullptr;
        static char tagBuffer[128] = {};
        static char layerBuffer[128] = {};
        if (tagLayerBufferOwner != selectedObject) {
            tagLayerBufferOwner = selectedObject;
            strcpy_s(tagBuffer, selectedObject->GetTag().c_str());
            const std::string currentLayer = selectedObject->GetLayer().empty() ? "Default" : selectedObject->GetLayer();
            strcpy_s(layerBuffer, currentLayer.c_str());
        }

        ImGui::SeparatorText("Tag / Layer");
        ImGui::TextDisabled("Tagは役割名、Layerは処理対象の分類として使います。");
        const bool mixedTag = HasMixedInspectorValue(inspectorTargets, "identity.tag");
        if (ImGui::InputText("Tag", tagBuffer, sizeof(tagBuffer))) {
            const std::string newTag = tagBuffer;
            for (Object3d* target : inspectorTargets) {
                if (target) {
                    target->SetTag(newTag);
                }
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        DrawMixedValueHint(mixedTag);

        const char* layerPresets[] = {
            "Default",
            "Player",
            "Enemy",
            "Stage",
            "Trigger",
            "Effect",
            "EditorOnly"
        };
        int layerPresetIndex = 0;
        const std::string selectedLayer = selectedObject->GetLayer().empty() ? "Default" : selectedObject->GetLayer();
        for (int i = 0; i < IM_ARRAYSIZE(layerPresets); ++i) {
            if (selectedLayer == layerPresets[i]) {
                layerPresetIndex = i;
                break;
            }
        }
        const bool mixedLayer = HasMixedInspectorValue(inspectorTargets, "identity.layer");
        if (ImGui::Combo("Layer Preset", &layerPresetIndex, layerPresets, IM_ARRAYSIZE(layerPresets))) {
            strcpy_s(layerBuffer, layerPresets[layerPresetIndex]);
            const std::string newLayer = layerBuffer;
            for (Object3d* target : inspectorTargets) {
                if (target) {
                    target->SetLayer(newLayer);
                }
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        DrawMixedValueHint(mixedLayer);
        if (ImGui::InputText("Layer", layerBuffer, sizeof(layerBuffer))) {
            const std::string newLayer = layerBuffer[0] == '\0' ? "Default" : std::string(layerBuffer);
            if (layerBuffer[0] == '\0') {
                strcpy_s(layerBuffer, "Default");
            }
            for (Object3d* target : inspectorTargets) {
                if (target) {
                    target->SetLayer(newLayer);
                }
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        DrawMixedValueHint(mixedLayer);


        DrawComponentPanel(editor_, selectedObject, inspectorTargets);

        const char* saveCategories[] = { "Object", "Player", "Enemy" };
        std::string currentCat = selectedObject->GetSaveCategory();
        int catIndex = 0;
        if (currentCat == "Player") catIndex = 1;
        else if (currentCat == "Enemy") catIndex = 2;

        const bool mixedSaveCategory = HasMixedInspectorValue(inspectorTargets, "identity.saveCategory");
        if (ImGui::Combo(ICON_FA_FOLDER " 保存先カテゴリ", &catIndex, saveCategories, IM_ARRAYSIZE(saveCategories))) {
            for (Object3d* object : inspectorTargets) {
                if (!object) continue;
                object->SetSaveCategory(saveCategories[catIndex]);
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        DrawMixedValueHint(mixedSaveCategory);

        // --- 親の名前表示 ---
        if (selectedObject->GetParent()) {
            ImGui::TextDisabled(ICON_FA_SITEMAP " 親: %s", selectedObject->GetParent()->GetName().c_str());
            if (ImGui::Button(ICON_FA_UNLINK " 親を解除 (Unparent)")) {
                selectedObject->SetParent(nullptr, true);
            }
        }
        else {
            ImGui::TextDisabled(ICON_FA_SITEMAP " 親: なし");
        }

        // --- Model Asset (InvisibleBoxでない場合のみ表示) ---
        if (selectedObject->GetClassName() != "InvisibleBox") {
            ImGui::Separator();
            ImGui::Text(ICON_FA_CUBE " モデルアセット: %s", selectedObject->GetModelName().c_str());
            if (isManagedCharacter) {
                ImGui::TextDisabled("Player/Enemyのモデルはステータス管理のタイプ共通設定です。");
                if (ImGui::Button(ICON_FA_SLIDERS_H " ステータス管理を開く##ModelStatus", ImVec2(-1, 0)) && editor_->GetStatusTuningWindow()) {
                    EditorManager::GetInstance()->SetSelectedObject(editor_->GetStatusTuningWindow());
                }
            } else {
                ImGui::Button(ICON_FA_BOX_OPEN " [ ここにモデルをドロップして変更 ] ", ImVec2(-1, 30));

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
                        const std::string modelName = ReadEditorAssetDragPath(payload->Data, payload->DataSize);
                        if (!modelName.empty()) {
                            ModelManager::GetInstance()->LoadModel(modelName);
                            selectedObject->SetModel(modelName);
                            DebugConsole::GetInstance()->AddLog("Switched model to: " + modelName);
                        }
                    }

                    // プリセットデータのドロップ受付
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRESET_ASSET")) {
                        const char* presetName = (const char*)payload->Data;
                        PresetManager::GetInstance()->ApplyPresetToObject(presetName, selectedObject);
                        DebugConsole::GetInstance()->AddLog("Applied preset: " + std::string(presetName));
                    }
                    ImGui::EndDragDropTarget();
                }
            }

            Model* currentModel = selectedObject->GetModel();
            const int meshCount = currentModel ? static_cast<int>(currentModel->GetMeshCount()) : 0;
            if (selectedObject->IsMeshDrawFiltered()) {
                ImGui::TextDisabled("描画Mesh: %d", selectedObject->GetMeshDrawIndex());
            }
            if (meshCount > 1 && !selectedObject->IsMeshDrawFiltered()) {
                ImGui::TextDisabled("Mesh数: %d", meshCount);
                if (ImGui::Button(ICON_FA_CUBE " メッシュを子Objectに分割", ImVec2(-1, 28))) {
                    editor_->SplitSelectedModelIntoMeshChildren();
                }
            } else if (meshCount == 1) {
                ImGui::TextDisabled("Mesh数: 1");
            }
        }

        // --- 可視性設定 ---
        ImGui::Separator();
        bool isVisible = selectedObject->GetIsVisible();
        const bool mixedVisible = HasMixedInspectorValue(inspectorTargets, "rendering.visible");
        PushMixedCheckbox(mixedVisible);
        if (ImGui::Checkbox(ICON_FA_EYE " 表示 (ゲーム内)", &isVisible)) {
            for (Object3d* object : inspectorTargets) {
                if (!object) continue;
                object->SetIsVisible(isVisible);
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        PopMixedCheckbox();
        bool isLocked = selectedObject->GetIsLocked();
        const bool mixedLocked = HasMixedInspectorValue(inspectorTargets, "editor.locked");
        PushMixedCheckbox(mixedLocked);
        if (ImGui::Checkbox(ICON_FA_LOCK " ロック (編集保護)", &isLocked)) {
            for (Object3d* object : inspectorTargets) {
                if (!object) continue;
                object->SetIsLocked(isLocked);
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        PopMixedCheckbox();
        bool castShadow = selectedObject->GetCastShadow();
        const bool mixedCastShadow = HasMixedInspectorValue(inspectorTargets, "rendering.castShadow");
        PushMixedCheckbox(mixedCastShadow);
        if (ImGui::Checkbox(ICON_FA_LIGHTBULB " 影を落とす", &castShadow)) {
            for (Object3d* object : inspectorTargets) {
                if (!object) continue;
                object->SetCastShadow(castShadow);
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        PopMixedCheckbox();

        // --- Transform編集 ---
        ImGui::Separator();
        ImGui::Text(ICON_FA_ARROWS_ALT " トランスフォーム (Transform)");
        Transform transformValue = *selectedObject->GetTransform();
        Transform* transform = &transformValue;
        bool isTransformChanged = false;

        const Vector3 beforePos = transform->translate;
        const bool mixedPosition = HasMixedInspectorValue(inspectorTargets, "transform.position");
        if (ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 座標 (Pos)", &transform->translate.x, 0.1f)) {
            ApplyPrimaryTransformDeltaToTargets(
                inspectorTargets,
                selectedObject,
                beforePos,
                transform->translate,
                transform->rotate,
                transform->rotate,
                transform->scale,
                transform->scale,
                true,
                false,
                false);
            isTransformChanged = true;
        }
        DrawMixedValueHint(mixedPosition);

        Vector3 rotDeg = { ToDegrees(transform->rotate.x), ToDegrees(transform->rotate.y), ToDegrees(transform->rotate.z) };
        const Vector3 beforeRot = transform->rotate;
        const bool mixedRotation = HasMixedInspectorValue(inspectorTargets, "transform.rotation");
        if (ImGui::DragFloat3(ICON_FA_SYNC " 回転 (Rot)", &rotDeg.x, 1.0f, -360.0f, 360.0f)) {
            transform->rotate = { ToRadians(rotDeg.x), ToRadians(rotDeg.y), ToRadians(rotDeg.z) };
            transform->isQuaternionMaster = false;
            ApplyPrimaryTransformDeltaToTargets(
                inspectorTargets,
                selectedObject,
                transform->translate,
                transform->translate,
                beforeRot,
                transform->rotate,
                transform->scale,
                transform->scale,
                false,
                true,
                false);
            isTransformChanged = true;
        }
        DrawMixedValueHint(mixedRotation);
        const Vector3 beforeScale = transform->scale;
        if (isManagedCharacter) {
            ImGui::TextDisabled(ICON_FA_EXPAND_ARROWS_ALT " スケール: %.3f, %.3f, %.3f（ステータス管理）",
                transform->scale.x, transform->scale.y, transform->scale.z);
        } else {
            const bool mixedScale = HasMixedInspectorValue(inspectorTargets, "transform.scale");
            if (ImGui::DragFloat3(ICON_FA_EXPAND_ARROWS_ALT " スケール (Scale)", &transform->scale.x, 0.05f)) {
                ApplyPrimaryTransformDeltaToTargets(
                    inspectorTargets,
                    selectedObject,
                    transform->translate,
                    transform->translate,
                    transform->rotate,
                    transform->rotate,
                    beforeScale,
                    transform->scale,
                    false,
                    false,
                    true);
                isTransformChanged = true;
            }
            DrawMixedValueHint(mixedScale);
        }

        if (isTransformChanged) {
            selectedObject->SetLocalTransform(transformValue);
            RefreshInspectorTargetMatrices(inspectorTargets);
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }


        // --- コライダー設定 ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader(ICON_FA_SHIELD_ALT " コリジョン設定 (Collision)", ImGuiTreeNodeFlags_DefaultOpen)) {
            Object3d::ColliderConfig colConfig = selectedObject->GetColliderConfig();
            bool isColChanged = false;

            const char* typeNames[] = { "なし (None)", "球 (Sphere)", "箱 (AABB)", "回転箱 (OBB)", "円柱 (Cylinder)", "リング (Ring)", "地形 (Terrain)" };
            int currentTypeIndex = (int)colConfig.type;
            if (ImGui::Combo(ICON_FA_SHAPES " 形状タイプ", &currentTypeIndex, typeNames, IM_ARRAYSIZE(typeNames))) {
                colConfig.type = (ColliderType)currentTypeIndex;
                if ((colConfig.type == ColliderType::kOBB || colConfig.type == ColliderType::kRing) && colConfig.size.x == 0.0f) {
                    colConfig.size = { 1.0f, 1.0f, 1.0f };
                }
                isColChanged = true;
            }

            if (colConfig.type != ColliderType::kNone) {
                if (ImGui::DragFloat3("中心オフセット", &colConfig.center.x, 0.05f)) isColChanged = true;

                if (colConfig.type == ColliderType::kSphere) {
                    if (ImGui::DragFloat("半径 (Radius)", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) {
                        colConfig.size.y = colConfig.size.z = colConfig.size.x;
                        isColChanged = true;
                    }
                }
                else if (colConfig.type == ColliderType::kRing) {
                    if (ImGui::DragFloat("外径 (Outer Radius)", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) isColChanged = true;
                    if (ImGui::DragFloat("内径 (Inner Radius)", &colConfig.size.z, 0.05f, 0.0f, 100.0f)) isColChanged = true;
                    // 厚み(Y)
                    if (ImGui::DragFloat("厚み (Thickness)", &colConfig.size.y, 0.05f, 0.0f, 10.0f)) isColChanged = true;
                }
                else {
                    if (ImGui::DragFloat3("サイズ (Size)", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) isColChanged = true;
                }

                if (colConfig.type == ColliderType::kOBB || colConfig.type == ColliderType::kRing) {
                    Vector3 rotDegObj = { ToDegrees(colConfig.rotation.x), ToDegrees(colConfig.rotation.y), ToDegrees(colConfig.rotation.z) };
                    if (ImGui::DragFloat3("回転 (Rotation)", &rotDegObj.x, 1.0f, -360.0f, 360.0f)) {
                        colConfig.rotation = { ToRadians(rotDegObj.x), ToRadians(rotDegObj.y), ToRadians(rotDegObj.z) };
                        isColChanged = true;
                    }
                }
                if (isColChanged) {
                    for (Object3d* object : inspectorTargets) {
                        if (!object) continue;
                        object->SetColliderConfig(colConfig);
                    }
                    MarkInspectorTargetsDirty(editor_, inspectorTargets);
                }
            }
            else {
                // なしの時も一応反映
                if (isColChanged) {
                    for (Object3d* object : inspectorTargets) {
                        if (!object) continue;
                        object->SetColliderConfig(colConfig);
                    }
                    MarkInspectorTargetsDirty(editor_, inspectorTargets);
                }
            }
            ImGui::Separator();
            if (ImGui::CollapsingHeader(ICON_FA_PALETTE " グラフィックス (Material)", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool isGraphicsChanged = false;
                DrawMaterialInstancePanel(editor_, selectedObject, inspectorTargets);
                DrawDecalPanel(editor_, selectedObject, inspectorTargets);
                const char* matTypes[] = {
                                         "通常 (Standard)", "ガラス (Glass)", "氷・宝石 (Ice/Crystal)",
                                         "ホログラム (Hologram)", "消滅 (Dissolve)", "旧マグマ (Emissive)",
                                         "トゥーン調 (Cel Shaded)", "ローカルフォグ (Local Fog)",
                                         "水 (Water)", "新マグマ (Magma)", "分厚い氷 (Ice)", "炎 (Fire)",
                                         "レーザー (Laser)", "スライムジェル (Slime Gel)",
                                         "地面衝撃波 (Shockwave)", "水/マグマ接触 (Liquid Contact)",
                                         "ダメージ亀裂 (Damage Crack)", "上昇気流 (Updraft)",
                                         "スタン拘束 (Stun Bind)", "王冠解放 (Crown Unlock)",
                                         "毒胞子 (Poison Spore)", "雲 (Cloud)",
                                         "ゲートポータル (Gate Portal)",
                                         "アニメ調地形 (Stylized Terrain)",
                                         "ダッシュパネル (Dash Panel)",
                                         "スライム補正 (Slime Soft)",
                                         "風弾 (Wind Orb)",
                                         "プリズム結晶 (Prism Crystal)",
                                         "デカール (Surface Decal)",
                                         "草地 PBR (Meadow)", "草 PBR (Foliage)", "樹木 PBR (Tree)",
                                         "土の道付き草地 PBR (Meadow Path)",
                                         "入口の土側面 PBR (Island Soil)",
                                         "庭の景物 PBR (Garden Prop)",
                                         "火山岩 PBR (Basalt)"
                };
                int currentMatType = selectedObject->GetMaterialType();
                if (currentMatType < 0) currentMatType = 0;
                if (currentMatType > 35) currentMatType = 0;
                if (ImGui::Combo(ICON_FA_PAINT_BRUSH " 質感 (Material Type)", &currentMatType, matTypes, IM_ARRAYSIZE(matTypes))) {
                    for (Object3d* object : inspectorTargets) {
                        if (!object) continue;
                        object->SetMaterialType(currentMatType);
                    }
                    MarkInspectorTargetsDirty(editor_, inspectorTargets);
                    isGraphicsChanged = true;
                }

                if (currentMatType == 32) {
                    float pathWidth = selectedObject->GetEnvIntensity();
                    if (ImGui::SliderFloat("土の道の半幅（44m幅の地面用）", &pathWidth, 1.0f, 8.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (object) object->SetEnvIntensity(pathWidth);
                        }
                        MarkInspectorTargetsDirty(editor_, inspectorTargets);
                        isGraphicsChanged = true;
                    }
                }
                if (currentMatType == 30 || currentMatType == 31) {
                    float wind = selectedObject->GetEnvIntensity();
                    if (ImGui::SliderFloat("草木の風の強さ", &wind, 0.0f, 2.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (object) object->SetEnvIntensity(wind);
                        }
                        MarkInspectorTargetsDirty(editor_, inspectorTargets);
                        isGraphicsChanged = true;
                    }
                    ImGui::TextWrapped("根元を固定して葉先だけ揺らします。色・スケールで草の色と丈を調整できます。");
                }
                if (currentMatType == 0 || (currentMatType >= 29 && currentMatType <= 35)) {
                    float metallic = selectedObject->GetMetallic();
                    if (ImGui::SliderFloat("金属度 (Metallic)", &metallic, 0.0f, 1.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (!object) continue;
                            object->SetMetallic(metallic);
                        }
                        MarkInspectorTargetsDirty(editor_, inspectorTargets);
                        isGraphicsChanged = true;
                    }
                    float roughness = selectedObject->GetRoughness();
                    if (ImGui::SliderFloat("粗さ (Roughness)", &roughness, 0.0f, 1.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (!object) continue;
                            object->SetRoughness(roughness);
                        }
                        MarkInspectorTargetsDirty(editor_, inspectorTargets);
                        isGraphicsChanged = true;
                    }
                }
                else if (currentMatType == 23) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.45f, 1.0f), ICON_FA_TREE " --- Stylized Terrain Settings ---");
                    Vector4 terrainColor = selectedObject->GetColor();
                    if (ImGui::ColorEdit4("差し色 (Accent Color)", &terrainColor.x)) {
                        for (Object3d* object : inspectorTargets) {
                            if (!object) continue;
                            object->SetColor(terrainColor);
                        }
                        MarkInspectorTargetsDirty(editor_, inspectorTargets);
                        isGraphicsChanged = true;
                    }
                    float textureBlend = selectedObject->GetRoughness();
                    if (ImGui::SliderFloat("テクスチャ反映量 (Texture Blend)", &textureBlend, 0.0f, 1.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (object) object->SetRoughness(textureBlend);
                        }
                        MarkInspectorTargetsDirty(editor_, inspectorTargets);
                        isGraphicsChanged = true;
                    }
                    float paintStrength = selectedObject->GetMetallic();
                    if (ImGui::SliderFloat("塗りの濃さ / 色ムラ (Paint Strength)", &paintStrength, 0.0f, 1.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (object) object->SetMetallic(paintStrength);
                        }
                        MarkInspectorTargetsDirty(editor_, inspectorTargets);
                        isGraphicsChanged = true;
                    }
                    float reliefStrength = selectedObject->GetEnvIntensity();
                    if (ImGui::SliderFloat("立体感 / 凹凸強度 (Relief)", &reliefStrength, 0.0f, 2.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (object) object->SetEnvIntensity(reliefStrength);
                        }
                        MarkInspectorTargetsDirty(editor_, inspectorTargets);
                        isGraphicsChanged = true;
                    }
                    ImGui::TextDisabled("側面の陰影・色面の差・法線マップ強度をまとめて調整します");
                }
                else if (currentMatType == 24) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.35f, 0.95f, 1.0f, 1.0f), ICON_FA_FORWARD " --- Dash Panel Settings ---");
                    float flowSpeed = selectedObject->GetRoughness();
                    if (ImGui::SliderFloat("流れる速さ (Flow Speed)", &flowSpeed, 0.0f, 1.0f)) {
                        selectedObject->SetRoughness(flowSpeed); isGraphicsChanged = true;
                    }
                    float lineDensity = selectedObject->GetMetallic();
                    if (ImGui::SliderFloat("ライン密度 (Line Density)", &lineDensity, 0.0f, 1.0f)) {
                        selectedObject->SetMetallic(lineDensity); isGraphicsChanged = true;
                    }
                }
                else if (currentMatType == 27) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.72f, 0.92f, 1.0f, 1.0f), ICON_FA_GEM " --- Prism Crystal Settings ---");
                    float facetSoftness = selectedObject->GetRoughness();
                    if (ImGui::SliderFloat("面反射の柔らかさ (Facet Softness)", &facetSoftness, 0.02f, 0.70f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (object) object->SetRoughness(facetSoftness);
                        }
                        isGraphicsChanged = true;
                    }
                    float dispersionStrength = selectedObject->GetMetallic();
                    if (ImGui::SliderFloat("分光の強さ (Dispersion)", &dispersionStrength, 0.0f, 1.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (object) object->SetMetallic(dispersionStrength);
                        }
                        isGraphicsChanged = true;
                    }
                    float environmentIntensity = selectedObject->GetEnvIntensity();
                    if (ImGui::SliderFloat("環境反射の強さ (Environment)", &environmentIntensity, 0.0f, 3.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (!object) continue;
                            object->SetEnableEnvMap(true);
                            object->SetEnvIntensity(environmentIntensity);
                        }
                        isGraphicsChanged = true;
                    }
                }

                ImGui::Separator();
                ImGui::Text(ICON_FA_EXPAND_ARROWS_ALT " PBRテクスチャ繰り返し");
                Vector2 textureTiling = selectedObject->GetTextureTiling();
                if (ImGui::DragFloat2("繰り返し倍率 (Tiling)", &textureTiling.x, 0.05f, 0.01f, 100.0f, "%.2f")) {
                    selectedObject->SetTextureTiling(textureTiling);
                    isGraphicsChanged = true;
                }
                bool autoTextureTiling = selectedObject->GetAutoTextureTiling();
                if (ImGui::Checkbox("スケールに合わせて自動繰り返し", &autoTextureTiling)) {
                    selectedObject->SetAutoTextureTiling(autoTextureTiling);
                    isGraphicsChanged = true;
                }
                ImGui::TextDisabled("大きい床や壁でPBRテクスチャが伸びる時に使います");

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
                DrawEffectMaterialSettings(selectedObject, currentMatType, isGraphicsChanged);
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

                bool enableLighting = selectedObject->GetEnableLighting();
                if (ImGui::Checkbox(ICON_FA_LIGHTBULB " ライト影響を受ける", &enableLighting)) {
                    selectedObject->SetEnableLighting(enableLighting);
                    isGraphicsChanged = true;
                }

                bool castShadow = selectedObject->GetCastShadow();
                if (ImGui::Checkbox("影を落とす (Cast Shadow)", &castShadow)) {
                    selectedObject->SetCastShadow(castShadow);
                    isGraphicsChanged = true;
                }
                if (!castShadow) {
                    ImGui::TextDisabled("影マップ描画だけを無効化します。ライト影響は別設定です。");
                }

                static InspectorTextureCatalog textureCatalog;
                textureCatalog.EnsureLoaded();
                const auto& albedoPaths = textureCatalog.GetAlbedoPaths();
                const auto& normalPaths = textureCatalog.GetNormalPaths();
                const auto& armPaths = textureCatalog.GetOrmPaths();
                const auto& spriteTexturePaths = textureCatalog.GetSpritePaths();
                const auto& pbrPresets = textureCatalog.GetPbrPresets();

                ImGui::Separator();
                std::string currentTexturePath = selectedObject->GetTexturePath();
                std::string currentNormalPath = selectedObject->GetNormalMapPath();
                std::string currentOrmPathForPreset = selectedObject->GetOrmMapPath();

                std::string currentPresetName = "未設定 (3枚セットを選択)";
                for (const InspectorTextureCatalog::PbrPreset& preset : pbrPresets) {
                    if (preset.albedoPath == currentTexturePath &&
                        preset.normalPath == currentNormalPath &&
                        preset.ormPath == currentOrmPathForPreset) {
                        currentPresetName = preset.name;
                        break;
                    }
                }

                // 文字一覧と球ギャラリーで同じ適用処理を使い、複数選択にも同じ結果を渡す。
                const auto applyPbrPreset = [&](const InspectorTextureCatalog::PbrPreset& preset) {
                    for (Object3d* target : inspectorTargets) {
                        if (!target) continue;
                        target->SetTexture(preset.albedoPath);
                        target->SetNormalMap(preset.normalPath);
                        target->SetOrmMap(preset.ormPath);
                        target->SetEnableNormalMap(true);
                    }
                    MarkInspectorTargetsDirty(editor_, inspectorTargets);
                    DebugConsole::GetInstance()->AddLog("PBR preset applied: " + preset.name);
                    isGraphicsChanged = true;
                };

                if (ImGui::BeginCombo(ICON_FA_IMAGE " PBRプリセット (Albedo/Normal/ORM)", currentPresetName.c_str())) {
                    if (pbrPresets.empty()) {
                        ImGui::TextDisabled("同名の3枚セットが見つかりません。");
                    }
                    for (const InspectorTextureCatalog::PbrPreset& preset : pbrPresets) {
                        const bool isSelected =
                            preset.albedoPath == currentTexturePath &&
                            preset.normalPath == currentNormalPath &&
                            preset.ormPath == currentOrmPathForPreset;
                        if (ImGui::Selectable(preset.name.c_str(), isSelected)) {
                            applyPbrPreset(preset);
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Button(ICON_FA_IMAGES " 球プレビューから選ぶ")) {
                    ImGui::OpenPopup("PBR球プレビュー");
                }
                ImGui::SetNextWindowSize(ImVec2(510.0f, 430.0f), ImGuiCond_Appearing);
                if (ImGui::BeginPopup("PBR球プレビュー")) {
                    ProjectWindow* projectWindow = editor_->GetProjectWindow();
                    if (ImGui::Button(ICON_FA_SYNC " 素材を再読込")) {
                        textureCatalog.Refresh();
                        if (projectWindow) projectWindow->InvalidatePbrThumbnails();
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("同じ照明で球に貼った見た目を比較します");
                    ImGui::Separator();
                    if (pbrPresets.empty()) ImGui::TextDisabled("Albedo・Normal・ORMの3枚セットがありません。");
                    const float cardWidth = 112.0f;
                    const int columns = (std::max)(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cardWidth));
                    if (ImGui::BeginTable("PbrSphereGrid", columns)) {
                        for (const auto& preset : pbrPresets) {
                            ImGui::TableNextColumn();
                            ImGui::PushID(preset.name.c_str());
                            const uint64_t thumbnail = projectWindow && ImGui::IsRectVisible(ImVec2(96.0f, 96.0f))
                                ? projectWindow->GetPbrThumbnailGpuPtr(preset.name,
                                    preset.albedoPath, preset.normalPath, preset.ormPath) : 0;
                            const bool selected = preset.albedoPath == currentTexturePath &&
                                preset.normalPath == currentNormalPath && preset.ormPath == currentOrmPathForPreset;
                            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.38f, 0.60f, 1.0f));
                            const bool clicked = thumbnail
                                ? ImGui::ImageButton("##sphere", (ImTextureID)(uintptr_t)thumbnail, ImVec2(96.0f, 96.0f))
                                : ImGui::Button("撮影中...", ImVec2(96.0f, 96.0f));
                            if (selected) ImGui::PopStyleColor();
                            if (clicked) {
                                applyPbrPreset(preset);
                                ImGui::CloseCurrentPopup();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::BeginTooltip();
                                ImGui::TextUnformatted(preset.name.c_str());
                                ImGui::TextDisabled("Albedo: %s", preset.albedoPath.c_str());
                                ImGui::TextDisabled("Normal: %s", preset.normalPath.c_str());
                                ImGui::TextDisabled("ORM: %s", preset.ormPath.c_str());
                                ImGui::EndTooltip();
                            }
                            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 100.0f);
                            ImGui::TextWrapped("%s", preset.name.c_str());
                            ImGui::PopTextWrapPos();
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndPopup();
                }
                if (!pbrPresets.empty()) {
                    ImGui::TextDisabled("同じベース名の3枚をまとめて適用します。");
                }

                const char* previewTextureValue = currentTexturePath.empty() ? "デフォルト (モデル固有)" : currentTexturePath.c_str();

                if (ImGui::BeginCombo(ICON_FA_IMAGE " 基本画像 (Diffuse / Sprite)", previewTextureValue)) {
                    if (!spriteTexturePaths.empty()) {
                        ImGui::TextDisabled("Sprite");
                        for (const std::string& path : spriteTexturePaths) {
                            bool isSelected = (currentTexturePath == path);
                            if (ImGui::Selectable(path.c_str(), isSelected)) {
                                selectedObject->SetTexture(path); isGraphicsChanged = true;
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::Separator();
                    }

                    if (!albedoPaths.empty()) {
                        ImGui::TextDisabled("PBR Albedo");
                        for (const std::string& path : albedoPaths) {
                            bool isSelected = (currentTexturePath == path);
                            if (ImGui::Selectable(path.c_str(), isSelected)) {
                                selectedObject->SetTexture(path); isGraphicsChanged = true;
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::Separator();
                    }

                    if (ImGui::Selectable("デフォルトに戻す", currentTexturePath.empty())) {
                        selectedObject->SetTexture(""); isGraphicsChanged = true;
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE_FILE")) {
                        const char* spritePath = static_cast<const char*>(payload->Data);
                        if (spritePath) {
                            selectedObject->SetTexture("Resources/sprite/" + std::string(spritePath));
                            isGraphicsChanged = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_SYNC_ALT " 更新##TextureList")) {
                    textureCatalog.Refresh();
                }

                if (IsSpriteCardObject(selectedObject)) {
                    ImGui::Separator();
                    if (ImGui::CollapsingHeader(ICON_FA_IMAGE " 2.5Dスプライト板", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextDisabled("3D空間にSprite画像を置くための板です。画像は上の基本画像から変更できます。");
                        if (ImGui::Button(ICON_FA_MAGIC " 2.5D用に整える", ImVec2(-1, 28))) {
                            ConfigureSpriteCardObject(selectedObject);
                            currentMatType = 0;
                            isGraphicsChanged = true;
                        }

                        const Transform* cardTransform = selectedObject->GetTransform();
                        Vector2 cardSize = { cardTransform->scale.x, cardTransform->scale.y };
                        if (ImGui::DragFloat2("表示サイズ (幅 / 高さ)", &cardSize.x, 0.02f, 0.01f, 100.0f, "%.2f")) {
                            selectedObject->SetScale({ (std::max)(0.01f, cardSize.x), (std::max)(0.01f, cardSize.y), 1.0f });
                            isTransformChanged = true;
                        }

                        if (ImGui::Button(ICON_FA_UNDO " 正面向きに戻す")) {
                            selectedObject->SetRotation({ 0.0f, 0.0f, 0.0f });
                            isTransformChanged = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(ICON_FA_SHIELD_ALT " 当たり判定なし")) {
                            Object3d::ColliderConfig colliderConfig = selectedObject->GetColliderConfig();
                            colliderConfig.type = ColliderType::kNone;
                            selectedObject->SetColliderConfig(colliderConfig);
                        }
                    }
                }

                if (enableNormal) {
                    std::string currentPath = selectedObject->GetNormalMapPath();
                    const char* previewValue = currentPath.empty() ? "未設定 (クリックで選択)" : currentPath.c_str();

                    if (ImGui::BeginCombo(ICON_FA_IMAGE " ノーマル画像", previewValue)) {
                        for (const std::string& path : normalPaths) {
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
                        for (const std::string& path : armPaths) {
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
                }
                ImGui::Separator();
                const char* blendModes[] = { "なし (None)", "通常 (Normal)", "加算 (Add)", "減算 (Subtract)", "乗算 (Multiply)", "スクリーン (Screen)" };
                int currentBlend = static_cast<int>(selectedObject->GetBlendMode());

                if (ImGui::Combo(ICON_FA_ADJUST " 合成 (Blend Mode)", &currentBlend, blendModes, IM_ARRAYSIZE(blendModes))) {
                    selectedObject->SetBlendMode(static_cast<BlendMode>(currentBlend)); isGraphicsChanged = true;
                }

                if (currentMatType != 23) {
                    Vector4 color = selectedObject->GetColor();
                    if (ImGui::ColorEdit4(ICON_FA_FILL_DRIP " 色 (Color)", &color.x)) {
                        selectedObject->SetColor(color); isGraphicsChanged = true;
                    }
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
            if (HasRegisteredComponent(selectedObject, "LOD") &&
                ImGui::CollapsingHeader(ICON_FA_COMPRESS_ARROWS_ALT " LOD / 軽量モデル")) {
                MeshRenderer* renderer = selectedObject->GetMeshRenderer();
                if (!renderer || selectedObject->GetModelName().empty()) {
                    ImGui::TextDisabled("モデルが設定されていません。");
                }
                else {
                    bool lodEnabled = selectedObject->IsLodEnabled();
                    if (ImGui::Checkbox("距離LODを使う", &lodEnabled)) {
                        selectedObject->SetLodEnabled(lodEnabled);
                    }

                    const int activeLodLevel = selectedObject->GetActiveLodLevel();
                    const float lodDistance = selectedObject->GetLodCameraDistance();
                    const std::string activeLodModel = selectedObject->GetActiveLodModelName();
                    ImGui::TextColored(
                        activeLodLevel == 0 ? ImVec4(0.75f, 0.9f, 1.0f, 1.0f) : ImVec4(0.45f, 1.0f, 0.55f, 1.0f),
                        "描画中: LOD%d / 距離 %.1f",
                        activeLodLevel,
                        lodDistance);
                    ImGui::TextDisabled("使用モデル: %s", activeLodModel.c_str());

                    if (ImGui::Button(ICON_FA_SYNC " LOD設定を再読込")) {
                        if (selectedObject->ReloadLodManifest()) {
                            DebugConsole::GetInstance()->AddLog("LOD manifest reloaded: " + selectedObject->GetModelName());
                        }
                        else {
                            DebugConsole::GetInstance()->AddLog("LOD manifest not found: " + selectedObject->GetModelName());
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_TRASH " LODを解除")) {
                        selectedObject->ClearLodLevels();
                    }

                    const auto& lodLevels = selectedObject->GetLodLevels();
                    if (lodLevels.empty()) {
                        ImGui::TextWrapped("LOD設定がありません。モデル最適化ツールでLODを生成するか、*_lod.jsonを配置してください。");
                    }
                    else if (ImGui::BeginTable("ObjectLodLevels", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                        ImGui::TableSetupColumn("LOD", ImGuiTableColumnFlags_WidthFixed, 48.0f);
                        ImGui::TableSetupColumn("切替距離", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                        ImGui::TableSetupColumn("モデル", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();

                        for (const auto& lod : lodLevels) {
                            ImGui::TableNextRow();
                            if (lod.level == activeLodLevel) {
                                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(75, 160, 95, 95));
                            }
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("LOD%d", lod.level);

                            ImGui::TableSetColumnIndex(1);
                            float distance = lod.distance;
                            const std::string distanceLabel = "##lod_distance_" + std::to_string(lod.level);
                            if (ImGui::DragFloat(distanceLabel.c_str(), &distance, 0.5f, 0.0f, 5000.0f, "%.1f")) {
                                selectedObject->SetLodLevelDistance(lod.level, distance);
                            }

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextWrapped("%s", lod.modelName.c_str());
                        }
                        ImGui::EndTable();
                    }
                }
            }

            ImGui::Separator();
            if (HasRegisteredComponent(selectedObject, "ParticleEmitter") &&
                ImGui::CollapsingHeader(ICON_FA_FIRE " パーティクル")) {
                // --- CPU Particle (Old) ---
                const auto& cpuParamsMap = ParticleManager::GetInstance()->GetParamsMap();
                std::vector<const char*> cpuItemNames;
                int currentCpuIndex = 0; int cpuIdx = 0;
                cpuItemNames.push_back("None (CPU)");

                std::string currentCpuName = selectedObject->GetParticleName();
                if (currentCpuName.empty()) currentCpuIndex = 0;

                for (const auto& [name, param] : cpuParamsMap) {
                    cpuItemNames.push_back(name.c_str());
                    if (name == currentCpuName) currentCpuIndex = cpuIdx + 1;
                    cpuIdx++;
                }

                if (ImGui::Combo("CPU Effect", &currentCpuIndex, cpuItemNames.data(), (int)cpuItemNames.size())) {
                    if (currentCpuIndex == 0) selectedObject->SetParticleName("");
                    else selectedObject->SetParticleName(cpuItemNames[currentCpuIndex]);
                }

                ImGui::Separator();

                // --- GPU Particle (New) ---
                const auto& gpuPresets = GPUParticleManager::GetInstance()->GetPresets();
                std::vector<const char*> gpuItemNames;
                int currentGpuIndex = 0; int gpuIdx = 0;
                gpuItemNames.push_back("None (GPU)");

                std::string currentGpuName = selectedObject->GetGPUParticleName();
                if (currentGpuName.empty()) currentGpuIndex = 0;

                for (const auto& [name, config] : gpuPresets) {
                    gpuItemNames.push_back(name.c_str());
                    if (name == currentGpuName) currentGpuIndex = gpuIdx + 1;
                    gpuIdx++;
                }

                if (ImGui::Combo("GPU Effect", &currentGpuIndex, gpuItemNames.data(), (int)gpuItemNames.size())) {
                    if (currentGpuIndex == 0) selectedObject->SetGPUParticleName("");
                    else selectedObject->SetGPUParticleName(gpuItemNames[currentGpuIndex]);
                }

                if (!currentGpuName.empty() && gpuPresets.find(currentGpuName) == gpuPresets.end()) {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Warning: GPU JSON not found!");
                }
            }

            ImGui::Separator();
            if (HasRegisteredComponent(selectedObject, "MeshEffect") &&
                ImGui::CollapsingHeader(ICON_FA_MAGIC " メッシュエフェクト (Mesh Effect)")) {
                std::vector<std::string> effectPaths;
                std::vector<std::string> effectDisplayNames;
                
                effectPaths.push_back(""); 
                effectDisplayNames.push_back("None");

                std::string effectDir = "Resources/json/effect";
                if (fs::exists(effectDir) && fs::is_directory(effectDir)) {
                    for (const auto& entry : fs::directory_iterator(effectDir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".json") {
                            effectPaths.push_back(entry.path().generic_string());
                            effectDisplayNames.push_back(entry.path().filename().generic_string());
                        }
                    }
                }

                std::vector<const char*> itemNames;
                for (const auto& displayName : effectDisplayNames) {
                    itemNames.push_back(displayName.c_str());
                }

                // --- スロット1 ---
                std::string currentEff1 = selectedObject->GetMeshEffect1Name();
                int selectIdx1 = 0;
                for (size_t i = 0; i < effectPaths.size(); ++i) {
                    if (effectPaths[i] == currentEff1) {
                        selectIdx1 = static_cast<int>(i);
                        break;
                    }
                }

                if (ImGui::Combo("Slot 1 (Floor etc.)", &selectIdx1, itemNames.data(), (int)itemNames.size())) {
                    selectedObject->SetMeshEffect1Name(effectPaths[selectIdx1]);
                }

                // --- スロット2 ---
                std::string currentEff2 = selectedObject->GetMeshEffect2Name();
                int selectIdx2 = 0;
                for (size_t i = 0; i < effectPaths.size(); ++i) {
                    if (effectPaths[i] == currentEff2) {
                        selectIdx2 = static_cast<int>(i);
                        break;
                    }
                }

                if (ImGui::Combo("Slot 2 (Pillar etc.)", &selectIdx2, itemNames.data(), (int)itemNames.size())) {
                    selectedObject->SetMeshEffect2Name(effectPaths[selectIdx2]);
                }
            }

            ImGui::Separator();
            uint32_t currentAttr = selectedObject->GetCollisionAttribute();
            DrawAttributeSelector(ICON_FA_TAGS " 自分の属性 (Attribute)", &currentAttr);
            if (currentAttr != selectedObject->GetCollisionAttribute()) {
                selectedObject->SetCollisionAttribute(currentAttr);
                MarkInspectorTargetsDirty(editor_, inspectorTargets);
            }

            uint32_t currentMask = selectedObject->GetCollisionMask();
            DrawAttributeSelector(ICON_FA_TAGS " 衝突対象 (Mask)", &currentMask);
            if (currentMask != selectedObject->GetCollisionMask()) {
                selectedObject->SetCollisionMask(currentMask);
                MarkInspectorTargetsDirty(editor_, inspectorTargets);
            }
        }

        if (HasRegisteredComponent(selectedObject, "Animator")) {
            // ==========================================
            // 1. ボーンアニメーション設定
            // ==========================================
            ImGui::Separator();
            ImGui::Text(ICON_FA_BONE " 【ボーンアニメーション】");
            const std::vector<Object3d*> animatorTargets =
                CollectRegisteredComponentTargets(inspectorTargets, "Animator");

        std::string controllerPath = selectedObject->GetAnimatorControllerPath();
        bool hasMixedController = false;
        for (Object3d* target : animatorTargets) {
            if (target && target->GetAnimatorControllerPath() != controllerPath) {
                hasMixedController = true;
                break;
            }
        }
        std::string controllerPreview = "(なし)";
        if (hasMixedController) {
            controllerPreview = "(複数値)";
        } else if (!controllerPath.empty()) {
            controllerPreview = fs::path(controllerPath).stem().string();
        }
        if (ImGui::BeginCombo("Animator Controller##BoneAnim", controllerPreview.c_str())) {
            const bool noneSelected = !hasMixedController && controllerPath.empty();
            if (ImGui::Selectable("(なし)", noneSelected)) {
                const auto beforeStates = editor_->CaptureObjectStates(animatorTargets);
                for (Object3d* target : animatorTargets) {
                    if (target) {
                        target->ClearAnimatorController();
                    }
                }
                editor_->RegisterObjectsEdited(beforeStates, "Animator Controller Clear");
            }

            std::vector<std::string> controllerFiles;
            const fs::path controllerDirectory = "Resources/json/animator";
            if (fs::exists(controllerDirectory) && fs::is_directory(controllerDirectory)) {
                for (const auto& entry : fs::directory_iterator(controllerDirectory)) {
                    if (entry.is_regular_file() && ToLowerAscii(entry.path().extension().string()) == ".json") {
                        controllerFiles.push_back(entry.path().stem().string());
                    }
                }
            }
            std::sort(controllerFiles.begin(), controllerFiles.end());
            for (const std::string& fileName : controllerFiles) {
                const bool isSelected = !hasMixedController && fs::path(controllerPath).stem().string() == fileName;
                if (ImGui::Selectable(fileName.c_str(), isSelected)) {
                    const auto beforeStates = editor_->CaptureObjectStates(animatorTargets);
                    bool assigned = false;
                    for (Object3d* target : animatorTargets) {
                        if (target && target->SetAnimatorController(fileName)) {
                            assigned = true;
                        }
                    }
                    if (assigned) {
                        editor_->RegisterObjectsEdited(beforeStates, "Animator Controller Assign");
                    } else {
                        DebugConsole::GetInstance()->AddLog("Animator Controllerの読み込みに失敗しました: " + fileName);
                    }
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (selectedObject->HasAnimatorController()) {
            const std::string currentState = selectedObject->GetAnimatorCurrentStateName();
            ImGui::TextDisabled("現在State: %s", currentState.empty() ? "(未再生)" : currentState.c_str());
        }
        if (ImGui::Button("Animator Controller Editorを開く##BoneAnim")) {
            editor_->GetAnimatorControllerEditor()->SetPreviewTarget(selectedObject);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetAnimatorControllerEditor());
        }
        ImGui::TextDisabled("Controller未設定時は下の旧単発クリップ設定を使用します。現行制作ではControllerを推奨します。");

        char animNameBuf[64];
        strncpy_s(animNameBuf, selectedObject->animName_.c_str(), sizeof(animNameBuf));
        if (ImGui::InputText("アニメ名##BoneAnim", animNameBuf, sizeof(animNameBuf))) {
            selectedObject->animName_ = animNameBuf;
        }
            ImGui::Checkbox("ループ再生##BoneAnim", &selectedObject->isAnimLoop_);
        }

        if (HasRegisteredComponent(selectedObject, "PathMover")) {
            DrawPathMoverSection(selectedObject);
        }

        DrawGameplayDataSection(selectedObject);
        ImGui::EndDisabled();
        ImGui::Separator();

        if (ImGui::Button(ICON_FA_TRASH_ALT " オブジェクト削除", ImVec2(-1, 0))) {
            EditorCommandRegistry::GetInstance()->Execute(EditorCommandId::EditDelete);
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
