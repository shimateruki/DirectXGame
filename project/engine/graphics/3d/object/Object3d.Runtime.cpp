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
// Object3d 実行時補助処理
// ------------------------------------------------------------------------
// GhostRecorder連携、Clone、Shadow/LocalFog描画、CopyFromを担当する。
// 生成済みオブジェクトの複製やランタイム補助はここにまとめる。
// ========================================================================
void Object3d::InitializeRecorder(SceneManager* sceneManager) {
    if (recorder_) {
        delete recorder_;
    }
    recorder_ = new GhostRecorder();
    recorder_->Initialize(sceneManager);
    recorder_->SetTarget(this);
}


std::unique_ptr<Object3d> Object3d::Clone() const {
    auto newObj = std::make_unique<Object3d>();
    assert(common_ != nullptr);
    newObj->Initialize(common_);
    newObj->CopyFrom(this);
    return newObj;
}


void Object3d::DrawShadow() {
    if (IsCameraObject()) return;
    if (meshRenderer_) {
        meshRenderer_->DrawShadow();
    }
}
void Object3d::SetShadowCommonState() {
    if (meshRenderer_) {
        meshRenderer_->SetShadowCommonState();
    }
}
void Object3d::DrawShadowOnly() {
    if (IsCameraObject()) return;
    if (meshRenderer_) {
        meshRenderer_->DrawShadowOnly();
    }
}

void Object3d::DrawLocalFog(uint32_t depthSrvHandle) {
    if (meshRenderer_) {
        // メッシュレンダラーに描画を丸投げ！
        meshRenderer_->DrawLocalFog(depthSrvHandle);
    }
}

MeshRenderer::LocalFogData* Object3d::GetLocalFogData() {
    return meshRenderer_ ? meshRenderer_->GetLocalFogData() : nullptr;
}

void Object3d::CopyFrom(const Object3d* other) {
    if (!other) return;

    // 1. 基本設定・識別子
    this->name_ = other->name_;
    this->className_ = other->className_;
    this->sceneCameraSettings_ = other->sceneCameraSettings_;
    this->tag_ = other->tag_;
    this->layer_ = other->layer_;
    this->saveCategory_ = other->saveCategory_;
    this->enemyType_ = other->enemyType_;
    this->gimmickType_ = other->gimmickType_;
    this->itemType_ = other->itemType_;
    this->isVisible_ = other->isVisible_;
    this->isLocked_ = other->isLocked_;
    this->castShadow_ = other->castShadow_;
    this->prefabInstanceInfo_ = other->prefabInstanceInfo_;
    if (!other->GetModelName().empty()) {
        this->SetModel(other->GetModelName());
    }

    // 2. Transform構造体 (位置・回転・クォータニオン・スケールを完全コピー)
    this->transform_ = other->transform_;

    // 3. Collider ＆ 衝突属性
    if (collider_ && other->collider_) {
        this->SetColliderConfig(other->GetColliderConfig());
        this->SetCollisionAttribute(other->GetCollisionAttribute());
        this->SetCollisionMask(other->GetCollisionMask());
        if (const TerrainCollisionData* terrain = other->GetTerrainCollisionData()) {
            this->SetTerrainCollisionData(*terrain, other->GetTerrainCollisionPath());
        } else {
            terrainCollisionPath_.clear();
            collider_->ClearTerrainData();
        }
    }

    // 4. イベント関連
    this->eventType_ = other->eventType_;
    this->SetTargetID(other->GetTargetID());
    this->SetEventID(other->GetEventID());

    // 5. Stats (Param)
    this->param_ = other->param_;

    // 6. MeshRenderer (グラフィックス・マテリアル・PBR設定)
    if (meshRenderer_ && other->meshRenderer_) {
        this->SetColor(other->GetColor());
        this->SetBlendMode(other->GetBlendMode());
        this->SetMaterialType(other->GetMaterialType());

        // ★追加: 金属度と粗さ
        this->SetMetallic(other->GetMetallic());
        this->SetRoughness(other->GetRoughness());

        // テクスチャ・マップ群
        this->SetEnableNormalMap(other->GetEnableNormalMap());
        this->SetNormalMap(other->GetNormalMapPath());
        this->SetOrmMap(other->GetOrmMapPath());
        this->SetTexture(other->GetTexturePath());

        // 環境マップ
        this->SetEnableEnvMap(other->GetEnableEnvMap());
        this->SetEnvIntensity(other->GetEnvIntensity());
        this->SetEmissive(other->GetEmissive());

        // LOD設定
        this->SetLodEnabled(other->IsLodEnabled());
        this->SetLodLevels(other->GetLodLevels());
        this->SetMeshDrawIndex(other->GetMeshDrawIndex());
    }

    // 7. アニメーション
    this->animName_ = other->animName_;
    this->isAnimLoop_ = other->isAnimLoop_;

    // パーティクル
    this->particleName_ = other->particleName_;
    this->gpuParticleName_ = other->gpuParticleName_;

    // メッシュエフェクト
    this->meshEffectName1_ = other->meshEffectName1_;
    this->meshEffectName2_ = other->meshEffectName2_;

    // 8. レコーダー (Ghost)
    this->recordPathName_ = other->recordPathName_;
    this->isRecordLoop_ = other->isRecordLoop_;
    this->isRecordRelative_ = other->isRecordRelative_;

    this->InitializeRecorder(nullptr);
    if (!this->recordPathName_.empty() && this->recorder_) {
        bool isCinematic = this->IsCameraObject();
        this->recorder_->Play(this->recordPathName_, this->isRecordLoop_, this->isRecordRelative_, isCinematic);
    }

    // 9. ローカルフォグ (もし両方にフォグデータがあれば構造体ごとコピー)
    auto myFog = this->GetLocalFogData();
    auto otherFog = const_cast<Object3d*>(other)->GetLocalFogData();
    if (myFog && otherFog) {
        *myFog = *otherFog;
    }
}
