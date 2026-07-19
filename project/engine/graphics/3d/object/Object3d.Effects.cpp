#define NOMINMAX
#include "Object3d.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "EffectObject3d.h"
#include "SRVManager.h"
#include "CameraManager.h"
#include "SceneManager.h"
#include "GhostRecorder.h"
#include "CollisionManager.h"
#include <cassert>
#include <algorithm> // min, max
#include <ParticleManager.h>
#include <GPUParticleManager.h>
#include <GPUParticleEmitter.h>
#include <DebugConsole.h>
#include <ProfilerManager.h>
#include <fstream>
#include <filesystem>

namespace {

std::filesystem::path ResolveTerrainCollisionFilePath(const std::string& path) {
    std::filesystem::path filePath(path);
    if (std::filesystem::exists(filePath)) {
        return filePath;
    }

    std::filesystem::path resourcesPath = std::filesystem::path("Resources") / filePath;
    if (std::filesystem::exists(resourcesPath)) {
        return resourcesPath;
    }

    return filePath;
}

bool HasParentInChain(Object3d* parent, const Object3d* child) {
    for (Object3d* current = parent; current != nullptr; current = current->GetParent()) {
        if (current == child) {
            return true;
        }
    }
    return false;
}

void ApplyMatrixToTransform(Transform& transform, const Matrix4x4& matrix) {
    const float epsilon = 0.0001f;

    Vector3 rowX = { matrix.m[0][0], matrix.m[0][1], matrix.m[0][2] };
    Vector3 rowY = { matrix.m[1][0], matrix.m[1][1], matrix.m[1][2] };
    Vector3 rowZ = { matrix.m[2][0], matrix.m[2][1], matrix.m[2][2] };

    transform.scale = {
        std::max(Math::Length(rowX), epsilon),
        std::max(Math::Length(rowY), epsilon),
        std::max(Math::Length(rowZ), epsilon),
    };
    transform.translate = { matrix.m[3][0], matrix.m[3][1], matrix.m[3][2] };

    Matrix4x4 rotateMatrix = Math::MakeIdentity4x4();
    rotateMatrix.m[0][0] = matrix.m[0][0] / transform.scale.x;
    rotateMatrix.m[0][1] = matrix.m[0][1] / transform.scale.x;
    rotateMatrix.m[0][2] = matrix.m[0][2] / transform.scale.x;
    rotateMatrix.m[1][0] = matrix.m[1][0] / transform.scale.y;
    rotateMatrix.m[1][1] = matrix.m[1][1] / transform.scale.y;
    rotateMatrix.m[1][2] = matrix.m[1][2] / transform.scale.y;
    rotateMatrix.m[2][0] = matrix.m[2][0] / transform.scale.z;
    rotateMatrix.m[2][1] = matrix.m[2][1] / transform.scale.z;
    rotateMatrix.m[2][2] = matrix.m[2][2] / transform.scale.z;

    transform.quaternion = Math::MatrixToQuaternion(rotateMatrix);
    transform.rotate = Math::MatrixToEuler(rotateMatrix);
    transform.isQuaternionMaster = true;
}

}

// ========================================================================
// Object3d 付属エフェクト処理
// ------------------------------------------------------------------------
// Object3dに追従するMeshEffectを生成、更新、描画する処理を担当する。
// 歪み系エフェクトは描画順が重要なので、通常Drawとは分けて管理する。
// ========================================================================
void Object3d::UpdateAttachedEffects(float deltaTime) {
    // =================================================================
    // ★ 追加: エフェクト用のアンカーを更新（エディタと同じ挙動を再現）
    // 親のスケールやX,Z回転を無視し、座標と大元のY軸回転だけを反映する
    // =================================================================
    effectAnchor_.translate = GetWorldPosition();
    effectAnchor_.scale = { 1.0f, 1.0f, 1.0f };

    // ルート（大元）のY軸回転を取得
    float rootRotY = transform_.rotate.y;
    Object3d* rootObj = this;
    while (rootObj && rootObj->GetParent()) {
        rootObj = rootObj->GetParent();
    }
    if (rootObj) {
        rootRotY = rootObj->GetRotation().y;
    }
    effectAnchor_.rotate = { 0.0f, rootRotY, 0.0f };
    effectAnchor_.UpdateMatrix();

    // ========================================================
    // --- スロット1 ---
    // ========================================================
    const std::string& primaryEffect = GetMeshEffect1Name();
    const std::string& secondaryEffect = GetMeshEffect2Name();
    if (!primaryEffect.empty()) {
        if (attachedEffects1_.empty() || currentMeshEffect1_ != primaryEffect) {
            attachedEffects1_.clear();
            currentMeshEffect1_ = "";

            std::ifstream file(primaryEffect);
            if (file.is_open()) {
                json j; file >> j; file.close();

                int volumeMode = j.contains("VolumeMode") ? (int)j["VolumeMode"] : 0;
                int numSpawns = (volumeMode == 2) ? 3 : (volumeMode == 1 ? 2 : 1);

                for (int i = 0; i < numSpawns; ++i) {
                    auto effect = std::make_unique<EffectObject3d>();
                    effect->Initialize(common_);
                    effect->SetName(primaryEffect + "_" + std::to_string(i));

                    if (effect->LoadFromJson(primaryEffect)) {
                        Vector3 localRot = effect->GetRotation();
                        Vector3 localPos = effect->GetTranslate();

                        if (volumeMode == 1 && i == 1) {
                            localRot.x += 1.570796f; // 90度クロス
                        }
                        else if (volumeMode == 2) {
                            float gap = 0.02f;
                            Vector3 localZ;
                            localZ.x = sinf(localRot.y) * cosf(localRot.x);
                            localZ.y = -sinf(localRot.x);
                            localZ.z = cosf(localRot.y) * cosf(localRot.x);

                            if (i == 1) { localPos.x += localZ.x * gap; localPos.y += localZ.y * gap; localPos.z += localZ.z * gap; }
                            if (i == 2) { localPos.x -= localZ.x * gap; localPos.y -= localZ.y * gap; localPos.z -= localZ.z * gap; }
                        }

                        effect->SetTranslate(localPos);
                        effect->SetRotation(localRot);

                        // ★修正: 親の階層には繋がず、専用のアンカーを親にする！
                        effect->GetTransform()->parent = &effectAnchor_;

                        attachedEffects1_.push_back(std::move(effect));
                    }
                }
                currentMeshEffect1_ = primaryEffect;
            }
        }
    }
    else {
        attachedEffects1_.clear();
        currentMeshEffect1_ = "";
    }

    // ========================================================
    // --- スロット2 ---
    // ========================================================
    if (!secondaryEffect.empty()) {
        if (attachedEffects2_.empty() || currentMeshEffect2_ != secondaryEffect) {
            attachedEffects2_.clear();
            currentMeshEffect2_ = "";

            std::ifstream file(secondaryEffect);
            if (file.is_open()) {
                json j; file >> j; file.close();

                int volumeMode = j.contains("VolumeMode") ? (int)j["VolumeMode"] : 0;
                int numSpawns = (volumeMode == 2) ? 3 : (volumeMode == 1 ? 2 : 1);

                for (int i = 0; i < numSpawns; ++i) {
                    auto effect = std::make_unique<EffectObject3d>();
                    effect->Initialize(common_);
                    effect->SetName(secondaryEffect + "_" + std::to_string(i));

                    if (effect->LoadFromJson(secondaryEffect)) {
                        Vector3 localRot = effect->GetRotation();
                        Vector3 localPos = effect->GetTranslate();

                        if (volumeMode == 1 && i == 1) {
                            localRot.x += 1.570796f;
                        }
                        else if (volumeMode == 2) {
                            float gap = 0.02f;
                            Vector3 localZ;
                            localZ.x = sinf(localRot.y) * cosf(localRot.x);
                            localZ.y = -sinf(localRot.x);
                            localZ.z = cosf(localRot.y) * cosf(localRot.x);

                            if (i == 1) { localPos.x += localZ.x * gap; localPos.y += localZ.y * gap; localPos.z += localZ.z * gap; }
                            if (i == 2) { localPos.x -= localZ.x * gap; localPos.y -= localZ.y * gap; localPos.z -= localZ.z * gap; }
                        }

                        effect->SetTranslate(localPos);
                        effect->SetRotation(localRot);

                        // ★修正: 親の階層には繋がず、専用のアンカーを親にする！
                        effect->GetTransform()->parent = &effectAnchor_;

                        attachedEffects2_.push_back(std::move(effect));
                    }
                }
                currentMeshEffect2_ = secondaryEffect;
            }
        }
    }
    else {
        attachedEffects2_.clear();
        currentMeshEffect2_ = "";
    }

    // 配列内のすべてのエフェクトを更新
    for (auto& effect : attachedEffects1_) {
        effect->Update(deltaTime);
    }
    for (auto& effect : attachedEffects2_) {
        effect->Update(deltaTime);
    }
}
void Object3d::DrawAttachedEffects(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (!isVisible_) return;

    // 配列内のすべてのエフェクトを描画
    for (auto& effect : attachedEffects1_) {
        effect->Draw(pointLightResource, spotLightResource);
    }
    for (auto& effect : attachedEffects2_) {
        effect->Draw(pointLightResource, spotLightResource);
    }
}
