#include "EffectPreviewStage.h"

#include "BaseScene.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "DirectXCommon.h"
#include "IconsFontAwesome5.h"
#include "LightManager.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr const char* kFloorName = "__Editor_EffectPreviewFloor";
constexpr const char* kLegacyBackWallName = "__Editor_EffectPreviewBackWall";
constexpr const char* kLegacyLeftRailName = "__Editor_EffectPreviewLeftRail";
constexpr const char* kLegacyRightRailName = "__Editor_EffectPreviewRightRail";
constexpr const char* kYAxisName = "__Editor_EffectPreviewAxisY";
constexpr const char* kOriginMarkerName = "__Editor_EffectPreviewOrigin";
constexpr const char* kEnvironmentPrefix = "__Editor_EffectPreview";
constexpr int kGridLineCount = 21;
constexpr int kGridCenterIndex = kGridLineCount / 2;
constexpr int kOuterMajorHalfCount = 10;

std::string MakeGridXName(int index) {
    return std::string(kEnvironmentPrefix) + "GridX_" + std::to_string(index);
}

std::string MakeGridZName(int index) {
    return std::string(kEnvironmentPrefix) + "GridZ_" + std::to_string(index);
}

std::string MakeOuterMajorXName(int index) {
    return std::string(kEnvironmentPrefix) + "OuterMajorX_" + std::to_string(index);
}

std::string MakeOuterMajorZName(int index) {
    return std::string(kEnvironmentPrefix) + "OuterMajorZ_" + std::to_string(index);
}

bool IsPreviewEnvironmentName(const std::string& name) {
    return name.rfind(kEnvironmentPrefix, 0) == 0;
}
}

EffectPreviewStage* EffectPreviewStage::GetInstance() {
    static EffectPreviewStage instance;
    return &instance;
}

void EffectPreviewStage::Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon) {
    sceneManager_ = sceneManager;
    dxCommon_ = dxCommon;
}

void EffectPreviewStage::EnableForToolPreview() {
    enabled_ = true;
    isolatedSpace_ = true;
    showFloor_ = true;
    moveCameraOnEnter_ = true;
    stageRadius_ = (std::max)(stageRadius_, 35.0f);
    cameraDistance_ = (std::max)(cameraDistance_, 14.0f);
    cameraHeight_ = (std::max)(cameraHeight_, 5.5f);
    recenterCameraRequested_ = true;
}

void EffectPreviewStage::Update() {
    CameraEditor::GetInstance()->SetEditorStateSaveBlocker(1u << 0, enabled_ && isolatedSpace_);

    if (enabled_ && !wasEnabled_) {
        CaptureCameraState();
        hasPlacedCamera_ = false;
    }
    if (!enabled_ && wasEnabled_) {
        RestoreStudioLighting();
        RestoreCameraState();
        hasPlacedCamera_ = false;
        recenterCameraRequested_ = false;
        const Vector4& sceneClearColor = LightManager::GetInstance()->GetSceneClearColor();
        dxCommon_->SetRenderClearColor(sceneClearColor.x, sceneClearColor.y, sceneClearColor.z, sceneClearColor.w);
    }
    wasEnabled_ = enabled_;

    if (enabled_) {
        ApplyBackgroundColor();
        if (studioLighting_) {
            ApplyStudioLighting();
        } else {
            RestoreStudioLighting();
        }
    }

    Object3d* floor = FindFloor();
    Vector3 previewPos = GetGroundPosition();
    if (enabled_ && (showFloor_ || showGrid_ || showAxes_)) {
        if (!floor) {
            CreateFloor();
            floor = FindFloor();
        }

        stageRadius_ = (std::max)(stageRadius_, gridSpacing_ * static_cast<float>(kGridCenterIndex) + 2.0f);
        floorSize_ = stageRadius_ * 2.0f;
        const float half = stageRadius_;
        const float floorY = previewPos.y - 0.08f;
        const float gridY = previewPos.y + 0.01f;

        if (showFloor_) {
            UpdateEnvironmentObject(kFloorName, { previewPos.x, floorY, previewPos.z }, { half, 0.05f, half }, floorColor_);
        } else {
            SetEnvironmentObjectVisible(kFloorName, false);
        }

        for (int i = 0; i < kGridLineCount; ++i) {
            if (!showGrid_ && !(showAxes_ && i == kGridCenterIndex)) {
                SetEnvironmentObjectVisible(MakeGridXName(i), false);
                SetEnvironmentObjectVisible(MakeGridZName(i), false);
                continue;
            }
            const float offset = (static_cast<float>(i) - static_cast<float>(kGridCenterIndex)) * gridSpacing_;
            const bool isCenter = i == kGridCenterIndex;
            const bool isMajor = (i - kGridCenterIndex) % 5 == 0;
            const Vector4 xLineColor = isCenter && showAxes_ ? xAxisColor_ : (isMajor ? majorGridColor_ : minorGridColor_);
            const Vector4 zLineColor = isCenter && showAxes_ ? zAxisColor_ : (isMajor ? majorGridColor_ : minorGridColor_);
            const float lineWidth = isCenter && showAxes_ ? 0.040f : (isMajor ? 0.025f : 0.014f);
            UpdateEnvironmentObject(MakeGridXName(i), { previewPos.x, gridY, previewPos.z + offset }, { half, 0.010f, lineWidth }, xLineColor);
            UpdateEnvironmentObject(MakeGridZName(i), { previewPos.x + offset, gridY + 0.006f, previewPos.z }, { lineWidth, 0.010f, half }, zLineColor);
        }

        for (int i = -kOuterMajorHalfCount; i <= kOuterMajorHalfCount; ++i) {
            const int nameIndex = i + kOuterMajorHalfCount;
            const float offset = static_cast<float>(i) * 5.0f * gridSpacing_;
            const bool overlapsFineGrid = std::abs(offset) <= gridSpacing_ * static_cast<float>(kGridCenterIndex) + 0.001f;
            const bool withinStage = std::abs(offset) <= stageRadius_ + 0.001f;
            if (!showGrid_ || overlapsFineGrid || !withinStage) {
                SetEnvironmentObjectVisible(MakeOuterMajorXName(nameIndex), false);
                SetEnvironmentObjectVisible(MakeOuterMajorZName(nameIndex), false);
                continue;
            }
            UpdateEnvironmentObject(MakeOuterMajorXName(nameIndex), { previewPos.x, gridY, previewPos.z + offset }, { half, 0.010f, 0.025f }, majorGridColor_);
            UpdateEnvironmentObject(MakeOuterMajorZName(nameIndex), { previewPos.x + offset, gridY + 0.006f, previewPos.z }, { 0.025f, 0.010f, half }, majorGridColor_);
        }

        if (showAxes_) {
            UpdateEnvironmentObject(kYAxisName, { previewPos.x, previewPos.y + 1.5f, previewPos.z }, { 0.018f, 1.5f, 0.018f }, yAxisColor_);
            UpdateEnvironmentObject(kOriginMarkerName, { previewPos.x, previewPos.y + 0.035f, previewPos.z }, { 0.07f, 0.07f, 0.07f }, { 0.92f, 0.92f, 0.95f, 1.0f });
        } else {
            SetEnvironmentObjectVisible(kYAxisName, false);
            SetEnvironmentObjectVisible(kOriginMarkerName, false);
        }

        // 旧プレビュー箱の壁とレールは互換Objectとして残っていても非表示にします。
        SetEnvironmentObjectVisible(kLegacyBackWallName, false);
        SetEnvironmentObjectVisible(kLegacyLeftRailName, false);
        SetEnvironmentObjectVisible(kLegacyRightRailName, false);
    }
    else if (floor) {
        for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
            if (object && IsPreviewEnvironmentName(object->GetName())) {
                object->SetIsVisible(false);
            }
        }
    }
}

void EffectPreviewStage::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_MAGIC " Effect Preview Stage");
    ImGui::Separator();

    ImGui::Checkbox("ステージを有効化", &enabled_);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLAY " 1回再生")) {
        ++playRequestSerial_;
    }

    ImGui::Checkbox("ループ再生", &loopPreview_);
    ImGui::Checkbox("隔離空間で確認", &isolatedSpace_);
    ImGui::Checkbox("有効化時にカメラを移動", &moveCameraOnEnter_);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CROSSHAIRS " カメラをプレビューへ移動")) {
        RequestCameraRecenter();
    }
    ImGui::DragFloat("再生速度", &playbackSpeed_, 0.05f, 0.0f, 5.0f);
    ImGui::DragFloat3(isolatedSpace_ ? "隔離空間内の床原点" : "プレビュー床原点", &origin_.x, 0.1f);
    if (isolatedSpace_) {
        ImGui::DragFloat3("隔離空間ベース", &isolatedBase_.x, 100.0f);
    }
    ImGui::DragFloat("プレビュー中心高さ", &previewCenterHeight_, 0.05f, 0.0f, 10.0f, "%.2f m");
    ImGui::TextDisabled("中心原点のEffectは標準1m。接地型Effectは0mへ下げて確認できます。");
    if (moveCameraOnEnter_) {
        ImGui::DragFloat("カメラ距離", &cameraDistance_, 0.1f, 2.0f, 80.0f);
        ImGui::DragFloat("カメラ高さ", &cameraHeight_, 0.1f, -20.0f, 40.0f);
        ImGui::DragFloat("カメラ方位角", &cameraAzimuthDegrees_, 0.5f, -180.0f, 180.0f, "%.1f deg");
        ImGui::DragFloat("注視点の高さ", &cameraTargetHeight_, 0.05f, 0.0f, 20.0f);
    }

    ImGui::SeparatorText("Studio Viewport");
    const char* presetNames[] = { "Studio Dark", "Studio Light", "Emission Black" };
    int selectedPreset = studioPresetIndex_;
    if (ImGui::Combo("表示プリセット", &selectedPreset, presetNames, IM_ARRAYSIZE(presetNames))) {
        ApplyStudioPreset(selectedPreset);
    }
    ImGui::TextDisabled("Dark:標準 / Light:黒煙・暗色 / Black:発光・Bloom・Alpha確認");

    ImGui::Checkbox("床", &showFloor_);
    ImGui::SameLine();
    ImGui::Checkbox("1mグリッド", &showGrid_);
    ImGui::SameLine();
    ImGui::Checkbox("XYZ軸", &showAxes_);
    if (showFloor_ || showGrid_) {
        if (ImGui::DragFloat("グリッド間隔", &gridSpacing_, 0.05f, 0.1f, 6.0f, "%.2f m")) {
            gridSpacing_ = std::clamp(gridSpacing_, 0.1f, 6.0f);
        }
        const float minimumRadius = gridSpacing_ * static_cast<float>(kGridCenterIndex) + 2.0f;
        if (ImGui::DragFloat("空間半径", &stageRadius_, 0.5f, minimumRadius, 80.0f, "%.1f m")) {
            stageRadius_ = std::clamp(stageRadius_, minimumRadius, 80.0f);
        }
        ImGui::TextDisabled("中央は細かいGrid、外周は5区画ごとのMajor Line。標準直径70mです。");
    }

    ImGui::Checkbox("Studio Lighting", &studioLighting_);
    if (studioLighting_ && ImGui::TreeNode("Studio Lighting設定")) {
        ImGui::ColorEdit3("Key Light色", &studioLightColor_.x);
        ImGui::DragFloat3("Key Light方向", &studioLightDirection_.x, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat("Key Light強度", &studioLightIntensity_, 0.05f, 0.0f, 8.0f);
        ImGui::DragFloat("Ambient強度", &studioAmbientIntensity_, 0.02f, 0.0f, 2.0f);
        ImGui::TextDisabled("Preview中だけSceneのDirectional Light・Fog・Skyboxを一時的に置き換えます。");
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("表示色の微調整")) {
        ImGui::ColorEdit4("背景色", &backgroundColor_.x);
        ImGui::ColorEdit4("床色", &floorColor_.x);
        ImGui::ColorEdit4("Minor Grid", &minorGridColor_.x);
        ImGui::ColorEdit4("Major Grid", &majorGridColor_.x);
        ImGui::TreePop();
    }

    if (ImGui::Button(ICON_FA_UNDO " Studio標準へ戻す", ImVec2(-1, 0))) {
        ApplyStudioPreset(0);
        showFloor_ = true;
        showGrid_ = true;
        showAxes_ = true;
        studioLighting_ = true;
        gridSpacing_ = 1.0f;
        stageRadius_ = 35.0f;
        previewCenterHeight_ = 1.0f;
        cameraDistance_ = 14.0f;
        cameraHeight_ = 5.5f;
        cameraAzimuthDegrees_ = 35.0f;
        cameraTargetHeight_ = 1.5f;
        recenterCameraRequested_ = true;
    }
    if (ImGui::Button(ICON_FA_TRASH_ALT " Preview環境を削除", ImVec2(-1, 0))) {
        RemoveFloor();
    }

    ImGui::Separator();
    ImGui::TextWrapped("BlenderのStudio Viewportに近い中立的な検証空間です。Mesh EffectとGPU Particleの発生位置を隔離し、床・グリッド・軸・背景・照明を揃えて比較します。Preview環境はScene保存対象から除外され、終了時にCameraとLightingを復元します。");
#endif
}

Vector3 EffectPreviewStage::GetGroundPosition() const {
    if (!isolatedSpace_) return origin_;
    return {
        isolatedBase_.x + origin_.x,
        isolatedBase_.y + origin_.y,
        isolatedBase_.z + origin_.z
    };
}

Vector3 EffectPreviewStage::GetPreviewPosition() const {
    Vector3 position = GetGroundPosition();
    position.y += previewCenterHeight_;
    return position;
}

void EffectPreviewStage::ApplyCameraOverride() {
    if (!enabled_) return;

    CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);

    if ((moveCameraOnEnter_ && !hasPlacedCamera_) || recenterCameraRequested_) {
        PlaceCameraAtPreview();
        hasPlacedCamera_ = true;
        recenterCameraRequested_ = false;
    }
}

void EffectPreviewStage::PlaceCameraAtPreview() {
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) return;

    Vector3 target = GetGroundPosition();
    target.y += cameraTargetHeight_;
    constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
    const float azimuth = cameraAzimuthDegrees_ * kDegreesToRadians;
    Vector3 eye = {
        target.x + std::sin(azimuth) * cameraDistance_,
        target.y + cameraHeight_,
        target.z - std::cos(azimuth) * cameraDistance_
    };

    float pitch = std::atan2(cameraHeight_, cameraDistance_);
    camera->SetFollowTarget(nullptr);
    camera->SetEye(eye);
    camera->SetTarget(target);
    camera->SetRotation({ pitch, -azimuth, 0.0f });
    camera->Update();
    CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
}

void EffectPreviewStage::CreateFloor() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* scene = sceneManager_->GetCurrentScene();
    if (!scene->GetObject3dCommon()) return;

    ModelManager::GetInstance()->LoadModel("Primitives/cube");

    const bool wasMissing = FindFloor() == nullptr;
    CreateEnvironmentObject(kFloorName);
    for (int i = 0; i < kGridLineCount; ++i) {
        CreateEnvironmentObject(MakeGridXName(i));
        CreateEnvironmentObject(MakeGridZName(i));
    }
    for (int i = 0; i <= kOuterMajorHalfCount * 2; ++i) {
        CreateEnvironmentObject(MakeOuterMajorXName(i));
        CreateEnvironmentObject(MakeOuterMajorZName(i));
    }
    CreateEnvironmentObject(kYAxisName);
    CreateEnvironmentObject(kOriginMarkerName);
    if (wasMissing) {
        DebugConsole::GetInstance()->AddLog("Effect Preview Stage environment created.");
    }
}

void EffectPreviewStage::RemoveFloor() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    std::vector<Object3d*> targets;
    for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (object && IsPreviewEnvironmentName(object->GetName())) {
            targets.push_back(object.get());
        }
    }

    for (Object3d* target : targets) {
        sceneManager_->GetCurrentScene()->RequestRemoveObject(target);
    }
    if (!targets.empty()) {
        DebugConsole::GetInstance()->AddLog("Effect Preview Stage environment removed.");
    }
}

Object3d* EffectPreviewStage::FindFloor() const {
    return FindEnvironmentObject(kFloorName);
}

Object3d* EffectPreviewStage::FindEnvironmentObject(const std::string& name) const {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;

    for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (object && object->GetName() == name) {
            return object.get();
        }
    }
    return nullptr;
}

void EffectPreviewStage::CreateEnvironmentObject(const std::string& name) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* scene = sceneManager_->GetCurrentScene();
    if (!scene->GetObject3dCommon() || FindEnvironmentObject(name)) return;

    auto object = std::make_unique<Object3d>();
    object->Initialize(scene->GetObject3dCommon());
    object->SetName(name);
    object->SetClassName("EditorOnly_EffectPreviewStage");
    object->SetSaveCategory("Object");
    object->SetEditorInternal(true);
    object->SetModel("Primitives/cube");
    object->SetIsLocked(true);
    object->SetCollisionAttribute(0);
    object->SetCollisionMask(0);
    object->SetMaterialType(0);
    object->SetBlendMode(BlendMode::kNormal);
    object->SetTexture("Resources/sprite/common/white.png");
    object->SetEnableLighting(false);
    object->SetCastShadow(false);
    object->SetMetallic(0.0f);
    object->SetRoughness(1.0f);
    object->SetEmissive(1.0f);
    object->SetColor(floorColor_);
    object->SetIsVisible(false);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();
    scene->AddObject(std::move(object));
}

void EffectPreviewStage::UpdateEnvironmentObject(const std::string& name, const Vector3& translate, const Vector3& scale, const Vector4& color) {
    Object3d* object = FindEnvironmentObject(name);
    if (!object) return;

    object->SetEditorInternal(true);
    object->SetIsVisible(true);
    object->SetTranslate(translate);
    object->SetScale(scale);
    object->SetColor(color);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();
}

void EffectPreviewStage::SetEnvironmentObjectVisible(const std::string& name, bool visible) {
    if (Object3d* object = FindEnvironmentObject(name)) {
        object->SetIsVisible(visible);
    }
}

void EffectPreviewStage::ApplyStudioPreset(int presetIndex) {
    studioPresetIndex_ = std::clamp(presetIndex, 0, 2);
    switch (studioPresetIndex_) {
    case 1:
        backgroundColor_ = { 0.42f, 0.45f, 0.50f, 1.0f };
        floorColor_ = { 0.54f, 0.56f, 0.60f, 1.0f };
        minorGridColor_ = { 0.35f, 0.37f, 0.41f, 1.0f };
        majorGridColor_ = { 0.25f, 0.27f, 0.31f, 1.0f };
        break;
    case 2:
        backgroundColor_ = { 0.005f, 0.006f, 0.008f, 1.0f };
        floorColor_ = { 0.025f, 0.028f, 0.035f, 1.0f };
        minorGridColor_ = { 0.08f, 0.09f, 0.11f, 1.0f };
        majorGridColor_ = { 0.16f, 0.18f, 0.22f, 1.0f };
        break;
    case 0:
    default:
        backgroundColor_ = { 0.095f, 0.105f, 0.125f, 1.0f };
        floorColor_ = { 0.20f, 0.215f, 0.24f, 1.0f };
        minorGridColor_ = { 0.30f, 0.32f, 0.35f, 1.0f };
        majorGridColor_ = { 0.46f, 0.49f, 0.54f, 1.0f };
        break;
    }
}

void EffectPreviewStage::ApplyStudioLighting() {
    LightManager* lightManager = LightManager::GetInstance();
    if (!lightManager) return;

    DirectionalLight& light = lightManager->GetDirectionalLight();
    if (!studioLightingApplied_) {
        storedDirectionalColor_ = light.color;
        storedDirectionalDirection_ = light.direction;
        storedDirectionalIntensity_ = light.intensity;
        storedAmbientColor_ = light.ambientColor;
        storedEnableFog_ = light.enableFog;
        storedVolumetricIntensity_ = light.volumetricIntensity;
        storedSkyboxEnabled_ = lightManager->IsSkyboxEnabled();
        hasCapturedLighting_ = true;
        studioLightingApplied_ = true;
    }

    const float directionLength = std::sqrt(
        studioLightDirection_.x * studioLightDirection_.x +
        studioLightDirection_.y * studioLightDirection_.y +
        studioLightDirection_.z * studioLightDirection_.z);
    if (directionLength > 0.0001f) {
        light.direction = {
            studioLightDirection_.x / directionLength,
            studioLightDirection_.y / directionLength,
            studioLightDirection_.z / directionLength
        };
    }
    light.color = studioLightColor_;
    light.intensity = studioLightIntensity_;
    light.ambientColor = {
        studioAmbientIntensity_ * 0.92f,
        studioAmbientIntensity_ * 0.96f,
        studioAmbientIntensity_
    };
    light.enableFog = 0;
    light.volumetricIntensity = 0.0f;
    lightManager->SetSkyboxEnabled(false);
}

void EffectPreviewStage::RestoreStudioLighting() {
    if (!studioLightingApplied_ || !hasCapturedLighting_) return;

    LightManager* lightManager = LightManager::GetInstance();
    if (lightManager) {
        DirectionalLight& light = lightManager->GetDirectionalLight();
        light.color = storedDirectionalColor_;
        light.direction = storedDirectionalDirection_;
        light.intensity = storedDirectionalIntensity_;
        light.ambientColor = storedAmbientColor_;
        light.enableFog = storedEnableFog_;
        light.volumetricIntensity = storedVolumetricIntensity_;
        lightManager->SetSkyboxEnabled(storedSkyboxEnabled_);
    }
    studioLightingApplied_ = false;
    hasCapturedLighting_ = false;
}

void EffectPreviewStage::ApplyBackgroundColor() {
    if (!dxCommon_) return;
    dxCommon_->SetRenderClearColor(backgroundColor_.x, backgroundColor_.y, backgroundColor_.z, backgroundColor_.w);
}

void EffectPreviewStage::CaptureCameraState() {
    if (hasCapturedCamera_) return;

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) return;

    storedEye_ = camera->GetEye();
    storedTarget_ = camera->GetTargetPoint();
    storedRotation_ = camera->GetRotation();
    storedCameraMode_ = static_cast<int>(CameraEditor::GetInstance()->GetMode());
    hasCapturedCamera_ = true;
}

void EffectPreviewStage::RestoreCameraState() {
    if (!hasCapturedCamera_) return;

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        camera->SetFollowTarget(nullptr);
        camera->SetEye(storedEye_);
        camera->SetTarget(storedTarget_);
        camera->SetRotation(storedRotation_);
        camera->Update();
    }

    CameraEditor::GetInstance()->SetMode(static_cast<CameraEditor::Mode>(storedCameraMode_));
    hasCapturedCamera_ = false;
}
