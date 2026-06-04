#include "EffectPreviewStage.h"

#include "BaseScene.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "DirectXCommon.h"
#include "IconsFontAwesome5.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "imgui.h"

#include <cmath>
#include <memory>

namespace {
constexpr const char* kFloorName = "__Editor_EffectPreviewFloor";
constexpr float kDefaultClearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };
}

EffectPreviewStage* EffectPreviewStage::GetInstance() {
    static EffectPreviewStage instance;
    return &instance;
}

void EffectPreviewStage::Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon) {
    sceneManager_ = sceneManager;
    dxCommon_ = dxCommon;
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
    }
    wasEnabled_ = enabled_;

    ApplyBackgroundColor();

    Object3d* floor = FindFloor();
    Vector3 previewPos = GetPreviewPosition();
    if (enabled_ && showFloor_) {
        if (!floor) {
            CreateFloor();
            floor = FindFloor();
        }
        if (floor) {
            floor->SetIsVisible(true);
            floor->SetTranslate({ previewPos.x, previewPos.y - 2.0f, previewPos.z });
            floor->SetScale({ floorSize_, 0.05f, floorSize_ });
            floor->SetColor(floorColor_);
            floor->UpdateLocalMatrix();
            floor->UpdateWorldMatrix();
        }
    }
    else if (floor) {
        floor->SetIsVisible(false);
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
    if (FindFloor()) return;

    ModelManager::GetInstance()->LoadModel("Primitives/cube");

    auto floor = std::make_unique<Object3d>();
    floor->Initialize(scene->GetObject3dCommon());
    floor->SetName(kFloorName);
    floor->SetClassName("EditorOnly");
    floor->SetSaveCategory("Object");
    floor->SetModel("Primitives/cube");
    floor->SetIsLocked(true);
    floor->SetCollisionAttribute(0);
    floor->SetCollisionMask(0);
    floor->SetMaterialType(0);
    floor->SetBlendMode(BlendMode::kNone);
    floor->SetColor(floorColor_);
    Vector3 previewPos = GetPreviewPosition();
    floor->SetTranslate({ previewPos.x, previewPos.y - 2.0f, previewPos.z });
    floor->SetScale({ floorSize_, 0.05f, floorSize_ });
    floor->UpdateLocalMatrix();
    floor->UpdateWorldMatrix();

    scene->AddObject(std::move(floor));
    DebugConsole::GetInstance()->AddLog("Effect Preview Stage floor created.");
}

void EffectPreviewStage::RemoveFloor() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    if (Object3d* floor = FindFloor()) {
        sceneManager_->GetCurrentScene()->RequestRemoveObject(floor);
        DebugConsole::GetInstance()->AddLog("Effect Preview Stage floor removed.");
    }
}

Object3d* EffectPreviewStage::FindFloor() const {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;

    for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (object && object->GetName() == kFloorName) {
            return object.get();
        }
    }
    return nullptr;
}

void EffectPreviewStage::ApplyBackgroundColor() {
    if (!dxCommon_) return;

    if (enabled_) {
        dxCommon_->SetRenderClearColor(backgroundColor_.x, backgroundColor_.y, backgroundColor_.z, backgroundColor_.w);
    }
    else {
        dxCommon_->SetRenderClearColor(kDefaultClearColor[0], kDefaultClearColor[1], kDefaultClearColor[2], kDefaultClearColor[3]);
    }
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
