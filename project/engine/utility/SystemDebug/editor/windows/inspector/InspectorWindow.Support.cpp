#include "engine/utility/SystemDebug/editor/windows/inspector/InspectorWindow.Internal.h"

using namespace InspectorWindowImplementation;

// 補助処理の定義を一か所に保持し、共有状態と呼び出し順を維持します。
namespace InspectorWindowImplementation {
std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::vector<Object3d*> CollectInspectorTargets(DebugEditor* editor, Object3d* primary) {
    std::vector<Object3d*> targets;
    if (!primary) {
        return targets;
    }

    const std::vector<Object3d*>& selectedObjects = editor->GetSelectedObjects();
    if (selectedObjects.empty()) {
        targets.push_back(primary);
        return targets;
    }

    targets.reserve(selectedObjects.size());
    for (Object3d* object : selectedObjects) {
        if (!object) {
            continue;
        }
        if (std::find(targets.begin(), targets.end(), object) != targets.end()) {
            continue;
        }
        targets.push_back(object);
    }

    if (std::find(targets.begin(), targets.end(), primary) == targets.end()) {
        targets.push_back(primary);
    }
    return targets;
}

void MarkInspectorTargetsDirty(DebugEditor* editor, const std::vector<Object3d*>& targets) {
    if (!editor) {
        return;
    }
    for (Object3d* object : targets) {
        if (!object) {
            continue;
        }
        editor->MarkDirtyForObject(object);
    }
}

void RefreshInspectorTargetMatrices(const std::vector<Object3d*>& targets) {
    for (Object3d* object : targets) {
        if (!object) {
            continue;
        }
        object->UpdateLocalMatrix();
        object->UpdateWorldMatrix();
    }
}

float SafeScaleRatio(float after, float before) {
    if (std::fabs(before) < 0.0001f) {
        return 1.0f;
    }
    return after / before;
}

void ApplyPrimaryTransformDeltaToTargets(
    const std::vector<Object3d*>& targets,
    Object3d* primary,
    const Vector3& beforePosition,
    const Vector3& afterPosition,
    const Vector3& beforeRotation,
    const Vector3& afterRotation,
    const Vector3& beforeScale,
    const Vector3& afterScale,
    bool applyPosition,
    bool applyRotation,
    bool applyScale) {

    if (!primary) {
        return;
    }

    const Vector3 positionDelta = {
        afterPosition.x - beforePosition.x,
        afterPosition.y - beforePosition.y,
        afterPosition.z - beforePosition.z,
    };
    const Vector3 rotationDelta = {
        afterRotation.x - beforeRotation.x,
        afterRotation.y - beforeRotation.y,
        afterRotation.z - beforeRotation.z,
    };
    const Vector3 scaleRatio = {
        SafeScaleRatio(afterScale.x, beforeScale.x),
        SafeScaleRatio(afterScale.y, beforeScale.y),
        SafeScaleRatio(afterScale.z, beforeScale.z),
    };

    for (Object3d* object : targets) {
        if (!object || object == primary) {
            continue;
        }

        Transform* transform = object->GetTransform();
        if (!transform) {
            continue;
        }

        if (applyPosition) {
            transform->translate.x += positionDelta.x;
            transform->translate.y += positionDelta.y;
            transform->translate.z += positionDelta.z;
        }
        if (applyRotation) {
            transform->rotate.x += rotationDelta.x;
            transform->rotate.y += rotationDelta.y;
            transform->rotate.z += rotationDelta.z;
            transform->isQuaternionMaster = false;
        }
        if (applyScale) {
            transform->scale.x *= scaleRatio.x;
            transform->scale.y *= scaleRatio.y;
            transform->scale.z *= scaleRatio.z;
        }
    }
}

bool IsSpriteCardObject(const Object3d* object) {
    if (!object) {
        return false;
    }
    const std::string modelName = object->GetModelName();
    const std::string texturePath = object->GetTexturePath();
    const std::string name = object->GetName();
    return modelName == "Primitives/plane" ||
        texturePath.find("Resources/sprite/") == 0 ||
        name.find("SpriteCard") != std::string::npos ||
        name.find("2.5D") != std::string::npos;
}

void ConfigureSpriteCardObject(Object3d* object) {
    if (!object) {
        return;
    }

    object->SetClassName("Model");
    object->SetModel("Primitives/plane");
    if (object->GetName().empty() || object->GetName() == "Object") {
        object->SetName("SpriteCard_2_5D");
    }
    if (object->GetTexturePath().empty()) {
        object->SetTexture("Resources/sprite/common/white.png");
    }
    object->SetMaterialType(0);
    object->SetBlendMode(BlendMode::kNormal);
    object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    object->SetEnableNormalMap(false);
    object->SetNormalMap("");
    object->SetOrmMap("");
    object->SetEnableEnvMap(false);
    object->SetEnableLighting(false);
    object->SetEmissive(1.35f);
    object->SetTextureTiling({ 1.0f, 1.0f });
    object->SetAutoTextureTiling(false);
    object->SetCollisionAttribute(0);
    object->SetCollisionMask(0);

    Object3d::ColliderConfig colliderConfig = object->GetColliderConfig();
    colliderConfig.type = ColliderType::kNone;
    object->SetColliderConfig(colliderConfig);
}

void ApplyRegisteredComponentToTargets(
    DebugEditor* editor,
    const std::vector<Object3d*>& targets,
    const std::string& componentTypeId,
    bool add) {
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    bool changed = false;
    for (Object3d* target : targets) {
        if (!target) {
            continue;
        }
        changed |= add
            ? registry->AddComponent(target, componentTypeId)
            : registry->RemoveComponent(target, componentTypeId);
    }
    if (changed) {
        MarkInspectorTargetsDirty(editor, targets);
        RefreshInspectorTargetMatrices(targets);
    }
}

void ApplyRegisteredComponentPresetToTargets(
    DebugEditor* editor,
    const std::vector<Object3d*>& targets,
    const std::string& componentTypeId,
    const std::string& presetId) {
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    bool changed = false;
    for (Object3d* target : targets) {
        changed |= registry->ApplyComponentPreset(target, componentTypeId, presetId);
    }
    if (changed) {
        MarkInspectorTargetsDirty(editor, targets);
        RefreshInspectorTargetMatrices(targets);
    }
}

void DrawRegisteredComponentRow(
    DebugEditor* editor,
    const std::vector<Object3d*>& targets,
    const EditorComponentDescriptor& component) {
    ImGui::PushID(component.typeId.c_str());
    ImGui::BeginGroup();
    ImGui::TextUnformatted(component.displayName.c_str());
    ImGui::TextDisabled("%s", component.description.c_str());
    ImGui::EndGroup();

    if (component.removable && component.remove) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            ApplyRegisteredComponentToTargets(editor, targets, component.typeId, false);
        }
    }
    else {
        ImGui::SameLine();
        ImGui::TextDisabled("固定");
    }
    ImGui::Separator();
    ImGui::PopID();
}

void DrawRegisteredAddComponentMenu(
    DebugEditor* editor,
    const std::vector<Object3d*>& targets,
    Object3d* primary) {
    if (!ImGui::BeginPopup("AddRegisteredComponentMenu")) {
        return;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    ImGui::TextDisabled("追加するComponent");
    ImGui::Separator();

    bool hasAddableComponent = false;
    for (const EditorComponentDescriptor* component : registry->GetApplicableComponentsForObject(primary)) {
        if (!component) {
            continue;
        }
        ImGui::PushID(component->typeId.c_str());
        if (component->add) {
            hasAddableComponent = true;
            const bool present = registry->IsComponentPresent(primary, component->typeId);
            if (ImGui::MenuItem(component->displayName.c_str(), nullptr, false, !present)) {
                ApplyRegisteredComponentToTargets(editor, targets, component->typeId, true);
            }
            if (ImGui::IsItemHovered() && !component->description.empty()) {
                ImGui::SetTooltip("%s", component->description.c_str());
            }
        }
        for (const EditorComponentPresetDescriptor& preset : component->presets) {
            hasAddableComponent = true;
            ImGui::PushID(preset.id.c_str());
            if (ImGui::MenuItem(preset.displayName.c_str())) {
                ApplyRegisteredComponentPresetToTargets(
                    editor,
                    targets,
                    component->typeId,
                    preset.id);
            }
            if (ImGui::IsItemHovered() && !preset.description.empty()) {
                ImGui::SetTooltip("%s", preset.description.c_str());
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    if (!hasAddableComponent) {
        ImGui::TextDisabled("追加可能なComponentはありません。");
    }
    ImGui::EndPopup();
}

bool HasRegisteredComponent(const Object3d* object, const char* componentTypeId) {
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    return registry->IsComponentPresent(object, componentTypeId);
}

std::vector<Object3d*> CollectRegisteredComponentTargets(
    const std::vector<Object3d*>& targets,
    const char* componentTypeId) {
    std::vector<Object3d*> result;
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    for (Object3d* target : targets) {
        if (target && registry->IsComponentPresent(target, componentTypeId)) {
            result.push_back(target);
        }
    }
    return result;
}

bool IsPrefabPropertyOverridden(const Object3d* object, const std::string& propertyPath) {
    if (!object || !object->IsPrefabInstance()) {
        return false;
    }
    for (const auto& entry : PresetManager::GetInstance()->GetPrefabOverrides(object)) {
        if (entry.propertyPath == propertyPath) {
            return true;
        }
    }
    return false;
}

void DrawAutomaticComponentProperties(
    DebugEditor* editor,
    Object3d* primary,
    const std::vector<Object3d*>& targets,
    const EditorComponentDescriptor& component) {
    if (!editor || !primary ||
        component.inspectorMode != EditorComponentInspectorMode::Automatic) {
        return;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    const std::vector<const EditorPropertyDescriptor*> properties =
        registry->GetPropertiesForComponent(component.typeId);
    if (properties.empty()) {
        return;
    }

    std::vector<Object3d*> componentTargets;
    componentTargets.reserve(targets.size());
    for (Object3d* target : targets) {
        if (!target || (component.applicable && !component.applicable(*target)) ||
            (component.present && !component.present(*target))) {
            continue;
        }
        componentTargets.push_back(target);
    }
    if (componentTargets.empty()) {
        return;
    }

    ImGui::Indent();
    if (componentTargets.size() != targets.size()) {
        ImGui::TextDisabled(
            "このComponentを持つ%zu / %zu Objectを編集",
            componentTargets.size(),
            targets.size());
    }
    if (ImGui::BeginTable(
        ("##AutoComponentProperties_" + component.typeId).c_str(),
        2,
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        for (const EditorPropertyDescriptor* property : properties) {
            if (!property || !registry->IsApplicable(primary, property->path)) {
                continue;
            }

            ImGui::PushID(property->path.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool multiEditable = HasEditorPropertyFlag(property->flags, EditorPropertyFlags::MultiEdit);
            const bool mixed = multiEditable && registry->HasMixedValue(componentTargets, property->path);
            const bool prefabOverride =
                HasEditorPropertyFlag(property->flags, EditorPropertyFlags::PrefabOverride) &&
                IsPrefabPropertyOverridden(primary, property->path);
            if (prefabOverride) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.63f, 0.20f, 1.0f),
                    mixed ? "%s * —" : "%s *",
                    property->displayName.c_str());
            }
            else if (mixed) {
                ImGui::TextDisabled("%s —", property->displayName.c_str());
            }
            else {
                ImGui::TextUnformatted(property->displayName.c_str());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextDisabled("%s", property->path.c_str());
                if (prefabOverride) {
                    ImGui::TextUnformatted("Prefab Override");
                }
                ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(1);
            nlohmann::json value = registry->GetValue(primary, property->path);
            const bool readOnly = HasEditorPropertyFlag(property->flags, EditorPropertyFlags::ReadOnly);
            ImGui::BeginDisabled(readOnly);
            EditorPropertyDrawOptions options;
            options.compact = true;
            options.mixed = mixed;
            const bool changed = EditorPropertyDrawer::DrawValue(*property, value, "##Value", options);
            ImGui::EndDisabled();

            if (!multiEditable && componentTargets.size() > 1) {
                ImGui::SameLine();
                ImGui::TextDisabled("代表のみ");
            }

            if (changed && !readOnly) {
                for (Object3d* target : componentTargets) {
                    if (!target || (!multiEditable && target != primary) ||
                        !registry->IsApplicable(target, property->path)) {
                        continue;
                    }
                    registry->SetValue(target, property->path, value);
                }
                MarkInspectorTargetsDirty(editor, componentTargets);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Unindent();
}

void DrawComponentPanel(DebugEditor* editor, Object3d* primary, const std::vector<Object3d*>& targets) {
    if (!primary) {
        return;
    }

    ImGui::SeparatorText("Components");
    ImGui::TextDisabled("Object3Dの既存機能をUnity風のComponentとして追加/削除します。");
    ImGui::TextDisabled("Prefab InstanceではRegistry Propertyの差分をOverrideとして追跡します。");

    if (ImGui::Button("Add Component", ImVec2(-1.0f, 30.0f))) {
        ImGui::OpenPopup("AddRegisteredComponentMenu");
    }
    DrawRegisteredAddComponentMenu(editor, targets, primary);

    ImGui::Spacing();
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    const std::vector<const EditorComponentDescriptor*> registeredComponents =
        registry->GetComponentsForObject(primary);
    bool hasOptionalComponent = false;
    for (const EditorComponentDescriptor* component : registeredComponents) {
        if (!component || !component->showInInspector) {
            continue;
        }
        hasOptionalComponent |= component->removable;
        DrawRegisteredComponentRow(editor, targets, *component);
        DrawAutomaticComponentProperties(editor, primary, targets, *component);
    }

    if (!hasOptionalComponent) {
        ImGui::TextDisabled("追加済みの任意Componentはありません。Add Componentから追加できます。");
    }
}

bool DrawSceneObjectNameCombo(const char* label, BaseScene* scene, Object3d* cameraObject, std::string& objectName) {
    bool changed = false;
    const char* preview = objectName.empty() ? "(未選択)" : objectName.c_str();
    if (!ImGui::BeginCombo(label, preview)) {
        return false;
    }

    if (ImGui::Selectable("(未選択)", objectName.empty())) {
        objectName.clear();
        changed = true;
    }
    if (scene) {
        for (const auto& object : scene->GetObjects()) {
            if (!object || object->IsEditorInternal() || object.get() == cameraObject || object->GetName().empty()) {
                continue;
            }
            const bool selected = objectName == object->GetName();
            if (ImGui::Selectable(object->GetName().c_str(), selected)) {
                objectName = object->GetName();
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
    }
    ImGui::EndCombo();
    return changed;
}

bool HasMixedInspectorValue(const std::vector<Object3d*>& targets, const char* propertyPath) {
    return EditorPropertyRegistry::GetInstance()->HasMixedValue(targets, propertyPath);
}

void DrawMixedValueHint(bool mixed) {
    if (!mixed) {
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("— 複数の値");
}

void PushMixedCheckbox(bool mixed) {
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
}

void PopMixedCheckbox() {
    ImGui::PopItemFlag();
}

void DrawCameraObjectInspector(DebugEditor* editor, BaseScene* scene, Object3d* cameraObject) {
    if (!editor || !cameraObject) {
        return;
    }

    // 旧CinematicCameraを選択した時点で、新しい保存形式へ安全に移行します。
    cameraObject->SetClassName("Camera");
    cameraObject->SetSaveCategory("Camera");
    SceneCameraSettings& settings = cameraObject->GetSceneCameraSettings();
    bool changed = false;

    ImGui::SeparatorText("Camera Object");
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), ICON_FA_VIDEO " Camera専用Inspector");
    ImGui::TextDisabled("このObject自身が編集・保存・プレビュー・Ghost Recorder再生の元データです。");
    if (ImGui::Checkbox("有効", &settings.enabled)) changed = true;

    const char* roleNames[] = { "Main", "Cinematic" };
    int role = static_cast<int>(settings.role);
    if (ImGui::Combo("役割", &role, roleNames, IM_ARRAYSIZE(roleNames))) {
        settings.role = static_cast<SceneCameraRole>(role);
        changed = true;
    }

    ImGui::SeparatorText("Transform");
    Transform* transform = cameraObject->GetTransform();
    if (ImGui::DragFloat3("位置", &transform->translate.x, 0.1f)) changed = true;
    Vector3 rotationDegrees = {
        ToDegrees(transform->rotate.x),
        ToDegrees(transform->rotate.y),
        ToDegrees(transform->rotate.z),
    };
    if (ImGui::DragFloat3("回転 (度)", &rotationDegrees.x, 0.5f)) {
        transform->rotate = {
            ToRadians(rotationDegrees.x),
            ToRadians(rotationDegrees.y),
            ToRadians(rotationDegrees.z),
        };
        transform->isQuaternionMaster = false;
        changed = true;
    }
    if (changed) {
        cameraObject->UpdateLocalMatrix();
        cameraObject->UpdateWorldMatrix();
    }

    ImGui::SeparatorText("Lens / Blend");
    float fovDegrees = ToDegrees(settings.fovY);
    if (ImGui::SliderFloat("FOV (度)", &fovDegrees, 10.0f, 120.0f, "%.1f")) {
        settings.fovY = ToRadians(fovDegrees);
        changed = true;
    }
    if (ImGui::DragFloat("Near Clip", &settings.nearClip, 0.01f, 0.001f, 100.0f, "%.3f")) changed = true;
    if (ImGui::DragFloat("Far Clip", &settings.farClip, 1.0f, 1.0f, 100000.0f, "%.1f")) changed = true;
    settings.nearClip = (std::max)(settings.nearClip, 0.001f);
    settings.farClip = (std::max)(settings.farClip, settings.nearClip + 0.01f);
    if (ImGui::DragFloat("Blend In (秒)", &settings.blendInDuration, 0.02f, 0.0f, 10.0f, "%.2f")) changed = true;
    if (ImGui::DragFloat("Blend Out (秒)", &settings.blendOutDuration, 0.02f, 0.0f, 10.0f, "%.2f")) changed = true;
    const char* easingNames[] = { "Linear", "Ease In", "Ease Out", "Ease In Out", "Smoother Step" };
    int easing = static_cast<int>(settings.easing);
    if (ImGui::Combo("切替Easing", &easing, easingNames, IM_ARRAYSIZE(easingNames))) {
        settings.easing = static_cast<SceneCameraEasing>(easing);
        changed = true;
    }

    ImGui::SeparatorText("Eye / Follow");
    const char* eyeSourceNames[] = { "Camera ObjectのTransform", "Scene Objectを追従" };
    int eyeSource = static_cast<int>(settings.eyeSource);
    if (ImGui::Combo("Eye取得元", &eyeSource, eyeSourceNames, IM_ARRAYSIZE(eyeSourceNames))) {
        settings.eyeSource = static_cast<SceneCameraEyeSource>(eyeSource);
        changed = true;
    }
    if (settings.eyeSource == SceneCameraEyeSource::kSceneObject) {
        if (DrawSceneObjectNameCombo("追従Object##CameraEye", scene, cameraObject, settings.eyeObjectName)) changed = true;
    }
    if (ImGui::DragFloat3("Eyeオフセット", &settings.eyeOffset.x, 0.05f)) changed = true;
    const char* followModeNames[] = { "完全追従", "遅延追従" };
    int eyeFollow = static_cast<int>(settings.eyeFollowMode);
    if (ImGui::Combo("Eye追従方式", &eyeFollow, followModeNames, IM_ARRAYSIZE(followModeNames))) {
        settings.eyeFollowMode = static_cast<SceneCameraFollowMode>(eyeFollow);
        changed = true;
    }
    if (settings.eyeFollowMode == SceneCameraFollowMode::kSmooth &&
        ImGui::DragFloat("Eye追従レスポンス", &settings.eyeFollowResponse, 0.1f, 0.1f, 40.0f, "%.2f")) {
        changed = true;
    }

    ImGui::SeparatorText("Target / Follow");
    const char* targetModeNames[] = { "固定座標", "Scene Objectを注視", "Cameraの前方" };
    int targetMode = static_cast<int>(settings.targetMode);
    if (ImGui::Combo("注視方法", &targetMode, targetModeNames, IM_ARRAYSIZE(targetModeNames))) {
        settings.targetMode = static_cast<SceneCameraTargetMode>(targetMode);
        changed = true;
    }
    if (settings.targetMode == SceneCameraTargetMode::kFixedPoint) {
        if (ImGui::DragFloat3("固定注視点", &settings.fixedTarget.x, 0.1f)) changed = true;
    }
    else if (settings.targetMode == SceneCameraTargetMode::kSceneObject) {
        if (DrawSceneObjectNameCombo("注視Object##CameraTarget", scene, cameraObject, settings.targetObjectName)) changed = true;
    }
    else {
        if (ImGui::DragFloat("前方距離", &settings.forwardDistance, 0.1f, 0.1f, 1000.0f, "%.1f")) changed = true;
    }
    if (settings.targetMode != SceneCameraTargetMode::kFixedPoint) {
        if (ImGui::DragFloat3("注視オフセット", &settings.targetOffset.x, 0.05f)) changed = true;
        int targetFollow = static_cast<int>(settings.targetFollowMode);
        if (ImGui::Combo("Target追従方式", &targetFollow, followModeNames, IM_ARRAYSIZE(followModeNames))) {
            settings.targetFollowMode = static_cast<SceneCameraFollowMode>(targetFollow);
            changed = true;
        }
        if (settings.targetFollowMode == SceneCameraFollowMode::kSmooth &&
            ImGui::DragFloat("Target追従レスポンス", &settings.targetFollowResponse, 0.1f, 0.1f, 40.0f, "%.2f")) {
            changed = true;
        }
    }

    ImGui::SeparatorText("Ghost Recorder");
    const std::string recordPreview = cameraObject->GetRecordPathName().empty() ? "(なし)" : cameraObject->GetRecordPathName();
    if (ImGui::BeginCombo("パスデータ", recordPreview.c_str())) {
        if (ImGui::Selectable("(なし)", cameraObject->GetRecordPathName().empty())) {
            cameraObject->SetRecordPathName("");
            if (cameraObject->recorder_) cameraObject->recorder_->Stop();
            changed = true;
        }
        const fs::path directory = "Resources/json/animation";
        if (fs::exists(directory) && fs::is_directory(directory)) {
            for (const auto& entry : fs::directory_iterator(directory)) {
                if (entry.path().extension() != ".json") continue;
                const std::string pathName = entry.path().stem().string();
                const bool selected = cameraObject->GetRecordPathName() == pathName;
                if (ImGui::Selectable(pathName.c_str(), selected)) {
                    cameraObject->SetRecordPathName(pathName);
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    bool recordLoop = cameraObject->IsRecordLoop();
    if (ImGui::Checkbox("ループ再生##CameraRecord", &recordLoop)) {
        cameraObject->SetRecordLoop(recordLoop);
        changed = true;
    }
    bool recordRelative = cameraObject->IsRecordRelative();
    if (ImGui::Checkbox("相対座標##CameraRecord", &recordRelative)) {
        cameraObject->SetRecordRelative(recordRelative);
        changed = true;
    }

    if (ImGui::Button(ICON_FA_PLAY " Cameraをテスト再生")) {
        CameraEditor::GetInstance()->PlaySceneObjectCamera(CameraManager::GetInstance()->GetMainCamera(), cameraObject);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STOP " 停止##CameraObject")) {
        if (cameraObject->recorder_) cameraObject->recorder_->Stop();
        CameraEditor::GetInstance()->StopSceneObjectCamera(CameraManager::GetInstance()->GetMainCamera());
    }
    if (!cameraObject->GetRecordPathName().empty() && ImGui::Button(ICON_FA_GHOST " Ghost Recorder再生")) {
        if (!cameraObject->recorder_) cameraObject->InitializeRecorder(nullptr);
        if (cameraObject->recorder_) {
            cameraObject->recorder_->Play(
                cameraObject->GetRecordPathName(),
                cameraObject->IsRecordLoop(),
                cameraObject->IsRecordRelative(),
                true);
        }
    }

    if (changed) {
        editor->MarkDirtyForObject(cameraObject);
    }
}

std::vector<Object3d*> CollectSceneObjects(BaseScene* scene) {
    std::vector<Object3d*> objects;
    if (!scene) {
        return objects;
    }
    objects.reserve(scene->GetObjects().size());
    for (const auto& object : scene->GetObjects()) {
        if (object && !object->IsEditorInternal()) {
            objects.push_back(object.get());
        }
    }
    return objects;
}

std::vector<Object3d*> CollectPrefabInstanceObjects(BaseScene* scene, const std::string& instanceId) {
    std::vector<Object3d*> objects;
    if (!scene || instanceId.empty()) {
        return objects;
    }
    for (const auto& object : scene->GetObjects()) {
        if (object && object->IsPrefabInstance() &&
            object->GetPrefabInstanceInfo().instanceId == instanceId) {
            objects.push_back(object.get());
        }
    }
    return objects;
}

void DrawPrefabInstancePanel(DebugEditor* editor, BaseScene* scene, Object3d* object) {
    if (!editor || !scene || !object || !object->IsPrefabInstance()) {
        return;
    }

    PresetManager* manager = PresetManager::GetInstance();
    const auto info = object->GetPrefabInstanceInfo();
    const auto overrides = manager->GetPrefabOverrides(object);
    const auto componentOverrides = manager->GetPrefabComponentOverrides(object);
    const auto variantOverrides = manager->GetPrefabVariantOverrides(object);
    const auto variantComponentOverrides = manager->GetPrefabVariantComponentOverrides(object);
    const auto structureSummary = manager->GetPrefabStructureOverrideSummary(info.prefabName);

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.33f, 0.50f, 1.0f));
    const bool open = ImGui::CollapsingHeader(ICON_FA_CUBES " Prefab Instance", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (!open) {
        return;
    }

    ImGui::Text("Prefab: %s", info.prefabName.empty() ? "(名称不明)" : info.prefabName.c_str());
    const std::string basePrefabName = manager->GetPrefabBaseName(info.prefabName);
    if (!basePrefabName.empty()) {
        ImGui::TextColored(ImVec4(0.70f, 0.55f, 1.0f, 1.0f),
            ICON_FA_CODE_BRANCH " Variant of: %s", basePrefabName.c_str());
    }
    ImGui::TextDisabled("Asset ID: %s", info.assetId.c_str());
    ImGui::TextDisabled("Instance ID: %s", info.instanceId.c_str());
    if (editor->IsPrefabEditMode()) {
        ImGui::TextColored(ImVec4(0.35f, 0.78f, 1.0f, 1.0f),
            ICON_FA_CUBE " Prefab ModeでAssetを直接編集中です。");
        ImGui::TextDisabled("保存・破棄はHierarchy上部から実行します。");
        ImGui::Separator();
        return;
    }
    if (!manager->HasValidPrefabSource(object)) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.25f, 1.0f),
            ICON_FA_EXCLAMATION_TRIANGLE " 元Prefabが見つかりません。UnpackするかPrefab Assetを復元してください。");
    }
    if (info.isRoot) {
        ImGui::TextDisabled("ルートの位置・回転はScene配置値として保持されます。");
        if (ImGui::Button(ICON_FA_CUBE " Prefab Modeで開く")) {
            editor->BeginPrefabEditSession(info.prefabName);
            return;
        }
    }

    const std::vector<Object3d*> sceneObjects = CollectSceneObjects(scene);
    const std::vector<Object3d*> instanceObjects = CollectPrefabInstanceObjects(scene, info.instanceId);

    if (ImGui::Button(ICON_FA_UPLOAD " Apply All", ImVec2(112.0f, 0.0f))) {
        EditorTransactionManager::GetInstance()->BeginGroup("Apply All Prefab Overrides");
        const auto beforeStates = editor->CaptureObjectStates(sceneObjects);
        const int applied = manager->ApplyAllPrefabOverrides(object, sceneObjects);
        editor->RegisterObjectsEdited(beforeStates, "Prefab Apply Propagation");
        EditorTransactionManager::GetInstance()->EndGroup();
        DebugConsole::GetInstance()->AddLog("Prefab Apply All: " + std::to_string(applied) + " overrides");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_UNDO " Revert All", ImVec2(112.0f, 0.0f))) {
        const auto beforeStates = editor->CaptureObjectStates(instanceObjects);
        const int reverted = manager->RevertAllPrefabOverrides(object, sceneObjects);
        editor->RegisterObjectsEdited(beforeStates, "Prefab Revert All");
        DebugConsole::GetInstance()->AddLog("Prefab Revert All: " + std::to_string(reverted) + " overrides");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_UNLINK " Unpack", ImVec2(96.0f, 0.0f))) {
        const auto beforeStates = editor->CaptureObjectStates(instanceObjects);
        const int unpacked = manager->UnpackPrefabInstance(object, sceneObjects);
        editor->RegisterObjectsEdited(beforeStates, "Unpack Prefab Instance");
        DebugConsole::GetInstance()->AddLog("Prefab Unpack: " + std::to_string(unpacked) + " objects");
    }

    ImGui::SeparatorText(("Component Overrides (" + std::to_string(componentOverrides.size()) + ")").c_str());
    if (componentOverrides.empty()) {
        ImGui::TextDisabled("このObjectにComponent構造差分はありません。");
    }
    for (const auto& entry : componentOverrides) {
        ImGui::PushID(("Component_" + entry.componentTypeId).c_str());
        ImGui::Text("%s", entry.displayName.c_str());
        ImGui::SameLine();
        ImGui::TextColored(
            entry.instancePresent ? ImVec4(0.45f, 0.90f, 0.55f, 1.0f) : ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
            entry.instancePresent ? "追加" : "削除");

        if (ImGui::SmallButton(ICON_FA_UPLOAD " Apply")) {
            EditorTransactionManager::GetInstance()->BeginGroup("Apply Prefab Component Override");
            const auto beforeStates = editor->CaptureObjectStates(sceneObjects);
            if (manager->ApplyPrefabComponent(object, entry.componentTypeId, sceneObjects)) {
                editor->RegisterObjectsEdited(beforeStates, "Prefab Component Apply Propagation");
                DebugConsole::GetInstance()->AddLog("Prefab Component Apply: " + entry.componentTypeId);
            }
            EditorTransactionManager::GetInstance()->EndGroup();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_UNDO " Revert")) {
            const auto beforeStates = editor->CaptureObjectStates(instanceObjects);
            if (manager->RevertPrefabComponent(object, entry.componentTypeId)) {
                editor->RegisterObjectsEdited(beforeStates, "Prefab Component Revert");
                DebugConsole::GetInstance()->AddLog("Prefab Component Revert: " + entry.componentTypeId);
            }
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText(("Property Overrides (" + std::to_string(overrides.size()) + ")").c_str());
    if (overrides.empty()) {
        ImGui::TextDisabled("このObjectに適用差分はありません。");
    }
    for (const auto& entry : overrides) {
        ImGui::PushID(entry.propertyPath.c_str());
        ImGui::Text("%s", entry.displayName.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", entry.propertyPath.c_str());

        if (ImGui::SmallButton(ICON_FA_UPLOAD " Apply")) {
            EditorTransactionManager::GetInstance()->BeginGroup("Apply Prefab Override");
            const auto beforeStates = editor->CaptureObjectStates(sceneObjects);
            if (manager->ApplyPrefabProperty(object, entry.propertyPath, sceneObjects)) {
                editor->RegisterObjectsEdited(beforeStates, "Prefab Apply Propagation");
                DebugConsole::GetInstance()->AddLog("Prefab Apply: " + entry.propertyPath);
            }
            EditorTransactionManager::GetInstance()->EndGroup();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_UNDO " Revert")) {
            const auto beforeStates = editor->CaptureObjectStates(instanceObjects);
            if (manager->RevertPrefabProperty(object, entry.propertyPath)) {
                editor->RegisterObjectsEdited(beforeStates, "Prefab Revert Property");
                DebugConsole::GetInstance()->AddLog("Prefab Revert: " + entry.propertyPath);
            }
        }
        ImGui::PopID();
    }

    if (!basePrefabName.empty()) {
        const std::size_t variantOverrideCount = variantOverrides.size() + variantComponentOverrides.size();
        ImGui::SeparatorText(("Variant Source Overrides (" + std::to_string(variantOverrideCount) + ")").c_str());
        ImGui::TextDisabled("InstanceのApplyで作られた、基底Prefabに対するVariant固有差分です。");
        if (structureSummary.HasOverrides()) {
            ImGui::Text("構造差分: Object追加 %d / 削除 %d / 移動 %d / Component %d / その他 %d",
                structureSummary.addedObjects,
                structureSummary.removedObjects,
                structureSummary.reparentedObjects,
                structureSummary.componentOverrides,
                structureSummary.rawNodeOverrides);
        }
        if (variantOverrides.empty() && variantComponentOverrides.empty()) {
            ImGui::TextDisabled("このObjectにはVariant固有差分がありません。");
        }
        for (const auto& entry : variantComponentOverrides) {
            ImGui::PushID(("VariantComponent_" + entry.componentTypeId).c_str());
            ImGui::Text("%s", entry.displayName.c_str());
            ImGui::SameLine();
            ImGui::TextColored(
                entry.variantPresent ? ImVec4(0.45f, 0.90f, 0.55f, 1.0f) : ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                entry.variantPresent ? "追加" : "削除");
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_UNDO " Baseへ戻す")) {
                EditorTransactionManager::GetInstance()->BeginGroup("Revert Prefab Variant Component Override");
                const auto beforeStates = editor->CaptureObjectStates(sceneObjects);
                if (manager->RevertPrefabVariantComponent(object, entry.componentTypeId, sceneObjects)) {
                    editor->RegisterObjectsEdited(beforeStates, "Variant Component Revert Propagation");
                    DebugConsole::GetInstance()->AddLog("Prefab Variant Component Revert: " + entry.componentTypeId);
                }
                EditorTransactionManager::GetInstance()->EndGroup();
            }
            ImGui::PopID();
        }
        for (const auto& entry : variantOverrides) {
            ImGui::PushID(("Variant_" + entry.propertyPath).c_str());
            ImGui::Text("%s", entry.displayName.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", entry.propertyPath.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_UNDO " Baseへ戻す")) {
                EditorTransactionManager::GetInstance()->BeginGroup("Revert Prefab Variant Override");
                const auto beforeStates = editor->CaptureObjectStates(sceneObjects);
                if (manager->RevertPrefabVariantProperty(object, entry.propertyPath, sceneObjects)) {
                    editor->RegisterObjectsEdited(beforeStates, "Variant Revert Propagation");
                    DebugConsole::GetInstance()->AddLog("Prefab Variant Revert: " + entry.propertyPath);
                }
                EditorTransactionManager::GetInstance()->EndGroup();
            }
            ImGui::PopID();
        }
    }
    ImGui::Separator();
}

std::string NormalizeMaterialInstanceSavePath(const char* text) {
    fs::path path = text ? fs::path(text) : fs::path();
    if (path.empty()) return {};
    if (path.extension() != ".json") path.replace_extension(".json");
    if (!path.has_parent_path()) path = fs::path("Resources/json/material_instances") / path;
    return path.lexically_normal().generic_string();
}

void DrawMaterialInstancePanel(
    DebugEditor* editor,
    Object3d* selectedObject,
    const std::vector<Object3d*>& targets) {
    if (!editor || !selectedObject) return;

    ImGui::SeparatorText("Material Instance");
    const std::string& linkedPath = selectedObject->GetMaterialInstancePath();
    ImGui::Text("リンク: %s", linkedPath.empty() ? "未設定" : linkedPath.c_str());
    ImGui::TextDisabled("リンク中はScene読込時にAssetの設定一式を再適用します。");

    static std::vector<std::string> assets;
    static bool assetsLoaded = false;
    if (!assetsLoaded) {
        assets = MaterialInstanceAsset::Discover();
        assetsLoaded = true;
    }
    if (ImGui::BeginCombo("共有Materialを選択", linkedPath.empty() ? "未設定" : linkedPath.c_str())) {
        for (const std::string& asset : assets) {
            const bool selected = asset == linkedPath;
            if (ImGui::Selectable(asset.c_str(), selected)) {
                std::string error;
                bool applied = false;
                for (Object3d* target : targets) {
                    applied |= target && target->ApplyMaterialInstance(asset, &error);
                }
                if (applied) {
                    MarkInspectorTargetsDirty(editor, targets);
                    DebugConsole::GetInstance()->AddLog("Material Instance applied: " + asset);
                } else if (!error.empty()) {
                    DebugConsole::GetInstance()->AddLog("Material Instance error: " + error);
                }
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        if (assets.empty()) ImGui::TextDisabled("保存済みAssetはまだありません。");
        ImGui::EndCombo();
    }

    if (!linkedPath.empty()) {
        if (ImGui::Button(ICON_FA_SYNC " 再読込##MaterialInstance")) {
            std::string error;
            bool applied = false;
            for (Object3d* target : targets) {
                if (!target) continue;
                const std::string path = target->GetMaterialInstancePath().empty()
                    ? linkedPath : target->GetMaterialInstancePath();
                applied |= target->ApplyMaterialInstance(path, &error);
            }
            if (applied) MarkInspectorTargetsDirty(editor, targets);
            if (!error.empty()) DebugConsole::GetInstance()->AddLog("Material Instance error: " + error);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UNLINK " リンク解除##MaterialInstance")) {
            for (Object3d* target : targets) if (target) target->ClearMaterialInstanceLink();
            MarkInspectorTargetsDirty(editor, targets);
        }
    }

    static char savePath[260] = "NewMaterial.json";
    ImGui::SetNextItemWidth(-130.0f);
    ImGui::InputText("##MaterialInstanceSavePath", savePath, sizeof(savePath));
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SAVE " 現在値を保存")) {
        const std::string path = NormalizeMaterialInstanceSavePath(savePath);
        std::string error;
        if (selectedObject->SaveMaterialInstance(path, &error)) {
            for (Object3d* target : targets) {
                if (target && target != selectedObject) target->ApplyMaterialInstance(path, nullptr);
            }
            assets = MaterialInstanceAsset::Discover();
            MarkInspectorTargetsDirty(editor, targets);
            DebugConsole::GetInstance()->AddLog("Material Instance saved: " + path);
        } else {
            DebugConsole::GetInstance()->AddLog("Material Instance error: " + error);
        }
    }
}

void DrawDecalPanel(
    DebugEditor* editor,
    Object3d* selectedObject,
    const std::vector<Object3d*>& targets) {
    if (!editor || !selectedObject || !selectedObject->IsDecal()) return;
    if (!ImGui::CollapsingHeader("Surface Decal", ImGuiTreeNodeFlags_DefaultOpen)) return;

    Object3d::DecalSettings settings = selectedObject->GetDecalSettings();
    bool changed = false;
    changed |= ImGui::DragFloat2("投影サイズ", &settings.size.x, 0.05f, 0.01f, 100.0f);
    changed |= ImGui::DragFloat("面オフセット", &settings.depthOffset, 0.001f, 0.0001f, 0.2f, "%.4f");
    changed |= ImGui::DragFloat("寿命 (0=永続)", &settings.lifetime, 0.05f, 0.0f, 120.0f);
    changed |= ImGui::DragFloat("Fade In", &settings.fadeIn, 0.02f, 0.0f, 10.0f);
    changed |= ImGui::DragFloat("Fade Out", &settings.fadeOut, 0.02f, 0.0f, 10.0f);
    changed |= ImGui::Checkbox("寿命後に削除対象", &settings.transient);

    if (changed) {
        for (Object3d* target : targets) {
            if (!target || !target->IsDecal()) continue;
            const Object3d::DecalSettings oldSettings = target->GetDecalSettings();
            target->SetDecalSettings(settings);
            Transform* transform = target->GetTransform();
            const Matrix4x4 rotation = transform->isQuaternionMaster
                ? Math::MakeRotateQuaternionMatrix(transform->quaternion)
                : Math::MakeRotateMatrix(transform->rotate);
            const Vector3 normal = { -rotation.m[2][0], -rotation.m[2][1], -rotation.m[2][2] };
            transform->translate += normal * (settings.depthOffset - oldSettings.depthOffset);
            transform->scale = { settings.size.x * 0.5f, settings.size.y * 0.5f, 1.0f };
            target->UpdateLocalMatrix();
            target->UpdateWorldMatrix();
        }
        MarkInspectorTargetsDirty(editor, targets);
    }

    if (ImGui::Button(ICON_FA_PLAY " Fadeを最初から確認")) {
        for (Object3d* target : targets) if (target && target->IsDecal()) target->RestartDecalPlayback();
    }
    ImGui::TextDisabled("Game View配置では地面・壁の法線へ自動整列します。回転はTransformで調整できます。");
}
}
