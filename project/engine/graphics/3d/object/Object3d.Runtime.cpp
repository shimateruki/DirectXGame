#define NOMINMAX
#include "Object3d.h"
#include "engine/graphics/3d/material/MaterialInstance.h"
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
#include <array>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>

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

std::string Object3d::GeneratePersistentGuid() {
    std::array<unsigned char, 16> bytes{};
    std::random_device random;
    for (unsigned char& value : bytes) {
        value = static_cast<unsigned char>(random());
    }

    // UUID version 4 / RFC 4122 variantのビット配置に合わせます。
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0Fu) | 0x40u);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3Fu) | 0x80u);

    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            stream << '-';
        }
        stream << std::setw(2) << static_cast<unsigned int>(bytes[index]);
    }
    return stream.str();
}

bool Object3d::IsPersistentGuidValid(std::string_view guid) {
    if (guid.size() != 36) {
        return false;
    }
    for (std::size_t index = 0; index < guid.size(); ++index) {
        const bool isSeparator = index == 8 || index == 13 || index == 18 || index == 23;
        if (isSeparator) {
            if (guid[index] != '-') return false;
            continue;
        }
        if (std::isxdigit(static_cast<unsigned char>(guid[index])) == 0) {
            return false;
        }
    }
    return true;
}

const std::string& Object3d::EnsurePersistentGuid() {
    if (!IsPersistentGuidValid(persistentGuid_)) {
        persistentGuid_ = GeneratePersistentGuid();
    }
    return persistentGuid_;
}

bool Object3d::SetPersistentGuid(const std::string& guid) {
    if (!IsPersistentGuidValid(guid)) {
        return false;
    }
    persistentGuid_ = guid;
    return true;
}

void Object3d::RegeneratePersistentGuid() {
    persistentGuid_ = GeneratePersistentGuid();
}

ParticleEmitterComponent* Object3d::EnsureParticleEmitterComponent() {
    if (!particleEmitterComponent_) {
        particleEmitterComponent_.emplace();
    }
    SetComponentPresenceMarker(std::string(kParticleEmitterComponentType), true);
    return &*particleEmitterComponent_;
}

bool Object3d::RemoveParticleEmitterComponent() {
    if (!particleEmitterComponent_) {
        return false;
    }
    particleEmitterComponent_.reset();
    gpuEmitter_.reset();
    SetComponentPresenceMarker(std::string(kParticleEmitterComponentType), false);
    return true;
}

ParticleEmitterComponent* Object3d::GetParticleEmitterComponent() {
    return particleEmitterComponent_ ? &*particleEmitterComponent_ : nullptr;
}

const ParticleEmitterComponent* Object3d::GetParticleEmitterComponent() const {
    return particleEmitterComponent_ ? &*particleEmitterComponent_ : nullptr;
}

void Object3d::SetParticleName(const std::string& name) {
    if (!particleEmitterComponent_ && name.empty()) return;
    EnsureParticleEmitterComponent()->SetCpuParticle(name);
}

const std::string& Object3d::GetParticleName() const {
    static const std::string empty;
    return particleEmitterComponent_ ? particleEmitterComponent_->GetCpuParticle() : empty;
}

void Object3d::SetGPUParticleName(const std::string& name) {
    if (!particleEmitterComponent_ && name.empty()) return;
    EnsureParticleEmitterComponent()->SetGpuParticle(name);
}

const std::string& Object3d::GetGPUParticleName() const {
    static const std::string empty;
    return particleEmitterComponent_ ? particleEmitterComponent_->GetGpuParticle() : empty;
}

MeshEffectComponent* Object3d::EnsureMeshEffectComponent() {
    if (!meshEffectComponent_) {
        meshEffectComponent_.emplace();
    }
    SetComponentPresenceMarker(std::string(kMeshEffectComponentType), true);
    return &*meshEffectComponent_;
}

bool Object3d::RemoveMeshEffectComponent() {
    if (!meshEffectComponent_) {
        return false;
    }
    meshEffectComponent_.reset();
    currentMeshEffect1_.clear();
    currentMeshEffect2_.clear();
    attachedEffects1_.clear();
    attachedEffects2_.clear();
    SetComponentPresenceMarker(std::string(kMeshEffectComponentType), false);
    return true;
}

MeshEffectComponent* Object3d::GetMeshEffectComponent() {
    return meshEffectComponent_ ? &*meshEffectComponent_ : nullptr;
}

const MeshEffectComponent* Object3d::GetMeshEffectComponent() const {
    return meshEffectComponent_ ? &*meshEffectComponent_ : nullptr;
}

void Object3d::SetMeshEffect1Name(const std::string& name) {
    if (!meshEffectComponent_ && name.empty()) return;
    EnsureMeshEffectComponent()->SetPrimaryEffect(name);
}

const std::string& Object3d::GetMeshEffect1Name() const {
    static const std::string empty;
    return meshEffectComponent_ ? meshEffectComponent_->GetPrimaryEffect() : empty;
}

void Object3d::SetMeshEffect2Name(const std::string& name) {
    if (!meshEffectComponent_ && name.empty()) return;
    EnsureMeshEffectComponent()->SetSecondaryEffect(name);
}

const std::string& Object3d::GetMeshEffect2Name() const {
    static const std::string empty;
    return meshEffectComponent_ ? meshEffectComponent_->GetSecondaryEffect() : empty;
}

PathMoverComponent* Object3d::EnsurePathMoverComponent() {
    if (!pathMoverComponent_) {
        pathMoverComponent_.emplace();
    }
    SetComponentPresenceMarker(std::string(kPathMoverComponentType), true);
    return &*pathMoverComponent_;
}

bool Object3d::RemovePathMoverComponent() {
    if (!pathMoverComponent_) {
        return false;
    }
    pathMoverComponent_.reset();
    if (recorder_) {
        recorder_->Stop();
    }
    SetComponentPresenceMarker(std::string(kPathMoverComponentType), false);
    return true;
}

PathMoverComponent* Object3d::GetPathMoverComponent() {
    return pathMoverComponent_ ? &*pathMoverComponent_ : nullptr;
}

const PathMoverComponent* Object3d::GetPathMoverComponent() const {
    return pathMoverComponent_ ? &*pathMoverComponent_ : nullptr;
}

void Object3d::SetRecordPathName(const std::string& name) {
    if (!pathMoverComponent_ && name.empty()) return;
    EnsurePathMoverComponent()->SetPathName(name);
}

const std::string& Object3d::GetRecordPathName() const {
    static const std::string empty;
    return pathMoverComponent_ ? pathMoverComponent_->GetPathName() : empty;
}

void Object3d::SetRecordLoop(bool loop) {
    if (!pathMoverComponent_ && !loop) return;
    EnsurePathMoverComponent()->SetLoop(loop);
}

bool Object3d::IsRecordLoop() const {
    return pathMoverComponent_ && pathMoverComponent_->IsLoop();
}

void Object3d::SetRecordRelative(bool relative) {
    if (!pathMoverComponent_ && !relative) return;
    EnsurePathMoverComponent()->SetRelative(relative);
}

bool Object3d::IsRecordRelative() const {
    return pathMoverComponent_ && pathMoverComponent_->IsRelative();
}

GameplayLinkComponent* Object3d::EnsureGameplayLinkComponent() {
    if (!gameplayLinkComponent_) {
        gameplayLinkComponent_.emplace();
    }
    SetComponentPresenceMarker(std::string(kGameplayLinkComponentType), true);
    return &*gameplayLinkComponent_;
}

bool Object3d::RemoveGameplayLinkComponent() {
    if (!gameplayLinkComponent_) {
        return false;
    }
    gameplayLinkComponent_.reset();
    SetComponentPresenceMarker(std::string(kGameplayLinkComponentType), false);
    return true;
}

GameplayLinkComponent* Object3d::GetGameplayLinkComponent() {
    return gameplayLinkComponent_ ? &*gameplayLinkComponent_ : nullptr;
}

const GameplayLinkComponent* Object3d::GetGameplayLinkComponent() const {
    return gameplayLinkComponent_ ? &*gameplayLinkComponent_ : nullptr;
}

void Object3d::ApplyWorldMatrix(const Matrix4x4& worldMatrix) {
    ApplyMatrixToTransform(transform_, worldMatrix);
    UpdateLocalMatrix();
    UpdateWorldMatrix();
}

NavAgentComponent* Object3d::EnsureNavAgentComponent() {
    if (!navAgentComponent_) {
        navAgentComponent_.emplace();
    }
    SetComponentPresenceMarker(std::string(kNavAgentComponentType), true);
    return &*navAgentComponent_;
}

bool Object3d::RemoveNavAgentComponent() {
    if (!navAgentComponent_) return false;
    navAgentComponent_.reset();
    SetComponentPresenceMarker(std::string(kNavAgentComponentType), false);
    return true;
}

NavAgentComponent* Object3d::GetNavAgentComponent() {
    return navAgentComponent_ ? &*navAgentComponent_ : nullptr;
}

const NavAgentComponent* Object3d::GetNavAgentComponent() const {
    return navAgentComponent_ ? &*navAgentComponent_ : nullptr;
}

void Object3d::SetEventID(int id) {
    if (!gameplayLinkComponent_ && id < 0) return;
    EnsureGameplayLinkComponent()->SetEventId(id);
}

int Object3d::GetEventID() const {
    return gameplayLinkComponent_ ? gameplayLinkComponent_->GetEventId() : -1;
}

void Object3d::SetTargetID(int id) {
    if (!gameplayLinkComponent_ && id < 0) return;
    EnsureGameplayLinkComponent()->SetTargetId(id);
}

int Object3d::GetTargetID() const {
    return gameplayLinkComponent_ ? gameplayLinkComponent_->GetTargetId() : -1;
}

std::vector<Object3d::BuiltInComponentInfo> Object3d::GetBuiltInComponentInfos() const {
    return {
        { kTransformComponentType, "Transform", true, false },
        { kMeshRendererComponentType, "Mesh Renderer", meshRenderer_ != nullptr, false },
        { kColliderComponentType, "Collider", collider_ != nullptr, false },
        { kParticleEmitterComponentType, "Particle Emitter", particleEmitterComponent_.has_value(), true },
        { kMeshEffectComponentType, "Mesh Effect", meshEffectComponent_.has_value(), true },
        { kPathMoverComponentType, "Path Mover", pathMoverComponent_.has_value(), true },
        { kGameplayLinkComponentType, "Gameplay Link", gameplayLinkComponent_.has_value(), true },
        { kNavAgentComponentType, "Nav Agent", navAgentComponent_.has_value(), true },
    };
}

bool Object3d::HasBuiltInComponent(std::string_view typeId) const {
    return FindBuiltInComponent(typeId) != nullptr;
}

void* Object3d::FindBuiltInComponent(std::string_view typeId) {
    return const_cast<void*>(static_cast<const Object3d*>(this)->FindBuiltInComponent(typeId));
}

const void* Object3d::FindBuiltInComponent(std::string_view typeId) const {
    if (typeId == kTransformComponentType) return &transform_;
    if (typeId == kMeshRendererComponentType) return meshRenderer_.get();
    if (typeId == kColliderComponentType) return collider_.get();
    if (typeId == kParticleEmitterComponentType) return GetParticleEmitterComponent();
    if (typeId == kMeshEffectComponentType) return GetMeshEffectComponent();
    if (typeId == kPathMoverComponentType) return GetPathMoverComponent();
    if (typeId == kGameplayLinkComponentType) return GetGameplayLinkComponent();
    if (typeId == kNavAgentComponentType) return GetNavAgentComponent();
    return nullptr;
}


void Object3d::DrawShadow() {
    if (!GetIsRenderVisible() || IsCameraObject()) return;
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
    if (!GetIsRenderVisible() || IsCameraObject()) return;
    if (meshRenderer_) {
        meshRenderer_->DrawShadowOnly();
    }
}

void Object3d::DrawLocalFog(uint32_t depthSrvHandle) {
    if (GetIsRenderVisible() && meshRenderer_) {
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
    // 複製先にはInitialize時に発行した別GUIDを維持します。
    this->opaqueComponents_ = other->opaqueComponents_;
    this->className_ = other->className_;
    this->sceneCameraSettings_ = other->sceneCameraSettings_;
    this->tag_ = other->tag_;
    this->layer_ = other->layer_;
    this->saveCategory_ = other->saveCategory_;
    this->enemyType_ = other->enemyType_;
    this->gimmickType_ = other->gimmickType_;
    this->itemType_ = other->itemType_;
    this->materialInstancePath_ = other->materialInstancePath_;
    this->decalSettings_ = other->decalSettings_;
    this->decalElapsedTime_ = 0.0f;
    this->decalAuthoredAlpha_ = other->decalAuthoredAlpha_;
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
    this->gameplayLinkComponent_ = other->gameplayLinkComponent_;
    this->navAgentComponent_ = other->navAgentComponent_;

    // 5. Stats (Param)
    this->param_ = other->param_;

    // 6. MeshRendererはMaterial Instanceと同じ共通変換を使い、項目追加時のコピー漏れを防ぎます。
    if (meshRenderer_ && other->meshRenderer_) {
        MaterialInstanceAsset::Apply(MaterialInstanceAsset::Capture(*other), *this);
        this->SetLodEnabled(other->IsLodEnabled());
        this->SetLodLevels(other->GetLodLevels());
        this->SetMeshDrawIndex(other->GetMeshDrawIndex());
    }


    // 7. アニメーション
    this->animName_ = other->animName_;
    this->isAnimLoop_ = other->isAnimLoop_;
    if (!other->GetAnimatorControllerPath().empty()) {
        this->SetAnimatorController(other->GetAnimatorControllerPath());
    } else {
        this->ClearAnimatorController();
    }

    // 実体Componentは存在状態を含めてコピーします。再生中Instanceは複製しません。
    this->particleEmitterComponent_ = other->particleEmitterComponent_;
    this->meshEffectComponent_ = other->meshEffectComponent_;
    this->pathMoverComponent_ = other->pathMoverComponent_;
    this->gpuEmitter_.reset();
    this->currentMeshEffect1_.clear();
    this->currentMeshEffect2_.clear();
    this->attachedEffects1_.clear();
    this->attachedEffects2_.clear();

    this->InitializeRecorder(nullptr);
    if (!this->GetRecordPathName().empty() && this->recorder_) {
        bool isCinematic = this->IsCameraObject();
        this->recorder_->Play(
            this->GetRecordPathName(),
            this->IsRecordLoop(),
            this->IsRecordRelative(),
            isCinematic);
    }

    // 9. ローカルフォグ (もし両方にフォグデータがあれば構造体ごとコピー)
    auto myFog = this->GetLocalFogData();
    auto otherFog = const_cast<Object3d*>(other)->GetLocalFogData();
    if (myFog && otherFog) {
        *myFog = *otherFog;
    }
}
