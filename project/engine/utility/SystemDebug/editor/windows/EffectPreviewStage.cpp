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
constexpr const char* kBackWallName = "__Editor_EffectPreviewBackWall";
constexpr const char* kLeftRailName = "__Editor_EffectPreviewLeftRail";
constexpr const char* kRightRailName = "__Editor_EffectPreviewRightRail";
constexpr const char* kEnvironmentPrefix = "__Editor_EffectPreview";
constexpr int kGridLineCount = 7;

std::string MakeGridXName(int index) {
    return std::string(kEnvironmentPrefix) + "GridX_" + std::to_string(index);
}

std::string MakeGridZName(int index) {
    return std::string(kEnvironmentPrefix) + "GridZ_" + std::to_string(index);
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
    floorSize_ = (std::max)(floorSize_, 18.0f);
    cameraDistance_ = (std::max)(cameraDistance_, 18.0f);
    cameraHeight_ = (std::max)(cameraHeight_, 6.5f);
    recenterCameraRequested_ = true;
}

void EffectPreviewStage::Update() {
    CameraEditor::GetInstance()->SetEditorStateSaveBlocker(1u << 0, enabled_ && isolatedSpace_);

    if (enabled_ && !wasEnabled_) {
        CaptureCameraState();
        hasPlacedCamera_ = false;
    }
    if (!enabled_ && wasEnabled_) {
        RestoreCameraState();
        hasPlacedCamera_ = false;
        recenterCameraRequested_ = false;
        const Vector4& sceneClearColor = LightManager::GetInstance()->GetSceneClearColor();
        dxCommon_->SetRenderClearColor(sceneClearColor.x, sceneClearColor.y, sceneClearColor.z, sceneClearColor.w);
    }
    wasEnabled_ = enabled_;

    if (enabled_) {
        ApplyBackgroundColor();
    }

    Object3d* floor = FindFloor();
    Vector3 previewPos = GetPreviewPosition();
    if (enabled_ && showFloor_) {
        if (!floor) {
            CreateFloor();
            floor = FindFloor();
        }

        const float half = floorSize_ * 0.5f;
        const float floorY = previewPos.y - 2.0f;
        const Vector4 gridColor = { 0.32f, 0.45f, 0.52f, 0.45f };
        const Vector4 railColor = { 0.52f, 0.68f, 0.78f, 0.75f };
        const Vector4 wallColor = {
            backgroundColor_.x * 0.75f + 0.03f,
            backgroundColor_.y * 0.75f + 0.04f,
            backgroundColor_.z * 0.75f + 0.05f,
            1.0f
        };

        UpdateEnvironmentObject(kFloorName, { previewPos.x, floorY, previewPos.z }, { floorSize_, 0.05f, floorSize_ }, floorColor_);
        UpdateEnvironmentObject(kBackWallName, { previewPos.x, previewPos.y - 0.35f, previewPos.z + half }, { floorSize_, 3.4f, 0.08f }, wallColor);
        UpdateEnvironmentObject(kLeftRailName, { previewPos.x - half, floorY + 0.12f, previewPos.z }, { 0.07f, 0.12f, floorSize_ }, railColor);
        UpdateEnvironmentObject(kRightRailName, { previewPos.x + half, floorY + 0.12f, previewPos.z }, { 0.07f, 0.12f, floorSize_ }, railColor);

        const float step = floorSize_ / static_cast<float>(kGridLineCount - 1);
        for (int i = 0; i < kGridLineCount; ++i) {
            const float offset = -half + step * static_cast<float>(i);
            UpdateEnvironmentObject(MakeGridXName(i), { previewPos.x, floorY + 0.06f, previewPos.z + offset }, { floorSize_, 0.012f, 0.025f }, gridColor);
            UpdateEnvironmentObject(MakeGridZName(i), { previewPos.x + offset, floorY + 0.065f, previewPos.z }, { 0.025f, 0.012f, floorSize_ }, gridColor);
        }
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
    ImGui::DragFloat3(isolatedSpace_ ? "隔離空間内の原点" : "プレビュー原点", &origin_.x, 0.1f);
    if (isolatedSpace_) {
        ImGui::DragFloat3("隔離空間ベース", &isolatedBase_.x, 100.0f);
    }
    if (moveCameraOnEnter_) {
        ImGui::DragFloat("カメラ距離", &cameraDistance_, 0.1f, 2.0f, 80.0f);
        ImGui::DragFloat("カメラ高さ", &cameraHeight_, 0.1f, -20.0f, 40.0f);
    }

    ImGui::Separator();
    ImGui::ColorEdit4("背景色", &backgroundColor_.x);
    ImGui::Checkbox("床を表示", &showFloor_);
    if (showFloor_) {
        ImGui::DragFloat("床サイズ", &floorSize_, 0.1f, 1.0f, 100.0f);
        ImGui::ColorEdit4("床色", &floorColor_.x);
    }

    if (ImGui::Button(ICON_FA_PLUS_SQUARE " 床を生成", ImVec2(-1, 0))) {
        CreateFloor();
    }
    if (ImGui::Button(ICON_FA_TRASH_ALT " 床を削除", ImVec2(-1, 0))) {
        RemoveFloor();
    }

    ImGui::Separator();
    ImGui::TextWrapped("有効化すると Mesh Effect と GPU Particle のプレビュー発生位置を隔離空間へ移します。カメラは初回だけ移動し、その後は通常のEditorカメラ操作で動かせます。床は保存対象から除外されます。");
#endif
}

Vector3 EffectPreviewStage::GetPreviewPosition() const {
    if (!isolatedSpace_) return origin_;
    return {
        isolatedBase_.x + origin_.x,
        isolatedBase_.y + origin_.y,
        isolatedBase_.z + origin_.z
    };
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

    Vector3 target = GetPreviewPosition();
    Vector3 eye = {
        target.x,
        target.y + cameraHeight_,
        target.z - cameraDistance_
    };

    float pitch = std::atan2(cameraHeight_, cameraDistance_);
    camera->SetFollowTarget(nullptr);
    camera->SetEye(eye);
    camera->SetTarget(target);
    camera->SetRotation({ pitch, 0.0f, 0.0f });
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
    CreateEnvironmentObject(kBackWallName);
    CreateEnvironmentObject(kLeftRailName);
    CreateEnvironmentObject(kRightRailName);
    for (int i = 0; i < kGridLineCount; ++i) {
        CreateEnvironmentObject(MakeGridXName(i));
        CreateEnvironmentObject(MakeGridZName(i));
    }
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
    object->SetModel("Primitives/cube");
    object->SetIsLocked(true);
    object->SetCollisionAttribute(0);
    object->SetCollisionMask(0);
    object->SetMaterialType(0);
    object->SetBlendMode(BlendMode::kNormal);
    object->SetColor(floorColor_);
    object->SetIsVisible(false);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();
    scene->AddObject(std::move(object));
}

void EffectPreviewStage::UpdateEnvironmentObject(const std::string& name, const Vector3& translate, const Vector3& scale, const Vector4& color) {
    Object3d* object = FindEnvironmentObject(name);
    if (!object) return;

    object->SetIsVisible(true);
    object->SetTranslate(translate);
    object->SetScale(scale);
    object->SetColor(color);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();
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
