#define NOMINMAX
#include "Object3d.h"
#include "Object3dTransformUtils.h"
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
#include <typeinfo>

namespace engine {

namespace {

std::filesystem::path ResolveTerrainCollisionFilePath(const std::string& path) {
    // 保存済みのパスを優先し、相対パスならResources配下も探して旧データを読めるようにする。
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

// 親候補の祖先に対象が含まれるか調べ、階層の循環を防ぎます。
bool HasParentInChain(Object3d* parent, const Object3d* child) {
    // 親子付け時の循環参照を防ぐため、祖先を根までたどる。
    for (Object3d* current = parent; current != nullptr; current = current->GetParent()) {
        if (current == child) {
            return true;
        }
    }
    return false;
}

}

// ========================================================================
// Object3d 実行時補助処理
// ------------------------------------------------------------------------
// GhostRecorder連携、Clone、Shadow/LocalFog描画、CopyFromを担当する。
// 生成済みオブジェクトの複製やランタイム補助はここにまとめる。
// ========================================================================
void Object3d::InitializeRecorder(SceneManager* sceneManager) {
    // 古い記録器の接続を先に解放し、このオブジェクトを新しい再生対象へ結び直す。
    recorder_.reset();
    recorder_ = std::make_unique<GhostRecorder>();
    recorder_->Initialize(sceneManager);
    recorder_->SetTarget(this);
}


std::unique_ptr<Object3d> Object3d::Clone() const {
    // 派生型の切り捨てを避けるため、基底型そのものだけをここで複製する。
    if (typeid(*this) != typeid(Object3d)) {
        return nullptr;
    }
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
    // 読込データの識別子は区切り位置と16進文字を確認し、不正なら採用しない。
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
    // 保存時に識別子が欠けていれば新規発行し、以後は同じ値を返す。
    if (!IsPersistentGuidValid(persistentGuid_)) {
        persistentGuid_ = GeneratePersistentGuid();
    }
    return persistentGuid_;
}

bool Object3d::SetPersistentGuid(const std::string& guid) {
    // 不正な識別子で既存のシーン参照を壊さないよう、検証後にだけ更新する。
    if (!IsPersistentGuidValid(guid)) {
        return false;
    }
    persistentGuid_ = guid;
    return true;
}

void Object3d::RegeneratePersistentGuid() {
    // 複製や別物としての登録で元オブジェクトと識別子を共有しないようにする。
    persistentGuid_ = GeneratePersistentGuid();
}

ParticleEmitterComponent* Object3d::EnsureParticleEmitterComponent() {
    // 編集で初めて粒子を設定したときだけComponentを作り、存在情報も保存対象にする。
    if (!particleEmitterComponent_) {
        particleEmitterComponent_.emplace();
    }
    SetComponentPresenceMarker(std::string(kParticleEmitterComponentType), true);
    return &*particleEmitterComponent_;
}

bool Object3d::RemoveParticleEmitterComponent() {
    // 設定と実行中のGPU放出器を共に消し、保存用の存在情報も解除する。
    if (!particleEmitterComponent_) {
        return false;
    }
    particleEmitterComponent_.reset();
    gpuEmitter_.reset();
    SetComponentPresenceMarker(std::string(kParticleEmitterComponentType), false);
    return true;
}

ParticleEmitterComponent* Object3d::GetParticleEmitterComponent() {
    // 未追加なら作成せずnullptrを返し、参照だけでシーンを変更しない。
    return particleEmitterComponent_ ? &*particleEmitterComponent_ : nullptr;
}

const ParticleEmitterComponent* Object3d::GetParticleEmitterComponent() const {
    // 読み取り側でも未追加Componentは生成しない。
    return particleEmitterComponent_ ? &*particleEmitterComponent_ : nullptr;
}

void Object3d::SetParticleName(const std::string& name) {
    // 空文字で未追加の場合はComponentを増やさず、実際の設定時だけ生成する。
    if (!particleEmitterComponent_ && name.empty()) return;
    EnsureParticleEmitterComponent()->SetCpuParticle(name);
}

const std::string& Object3d::GetParticleName() const {
    // 未追加時には参照寿命が安定した空文字列を返す。
    static const std::string empty;
    return particleEmitterComponent_ ? particleEmitterComponent_->GetCpuParticle() : empty;
}

void Object3d::SetGPUParticleName(const std::string& name) {
    // GPUプリセットが指定された場合だけ粒子Componentを実体化する。
    if (!particleEmitterComponent_ && name.empty()) return;
    EnsureParticleEmitterComponent()->SetGpuParticle(name);
}

const std::string& Object3d::GetGPUParticleName() const {
    // GPU設定が無い場合も空文字列の参照を安全に返す。
    static const std::string empty;
    return particleEmitterComponent_ ? particleEmitterComponent_->GetGpuParticle() : empty;
}

MeshEffectComponent* Object3d::EnsureMeshEffectComponent() {
    // メッシュ効果を初めて設定するときだけComponentを作り、保存上の存在を記録する。
    if (!meshEffectComponent_) {
        meshEffectComponent_.emplace();
    }
    SetComponentPresenceMarker(std::string(kMeshEffectComponentType), true);
    return &*meshEffectComponent_;
}

bool Object3d::RemoveMeshEffectComponent() {
    // 設定だけでなく再生中の効果参照も消し、削除後に描画が残らないようにする。
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
    // 編集画面からの参照ではComponentを勝手に追加しない。
    return meshEffectComponent_ ? &*meshEffectComponent_ : nullptr;
}

const MeshEffectComponent* Object3d::GetMeshEffectComponent() const {
    // 未設定ならnullptrを返し、読み取り操作を副作用なしに保つ。
    return meshEffectComponent_ ? &*meshEffectComponent_ : nullptr;
}

void Object3d::SetMeshEffect1Name(const std::string& name) {
    // 主効果を指定するときだけComponentを確保し、空設定では余分な項目を増やさない。
    if (!meshEffectComponent_ && name.empty()) return;
    EnsureMeshEffectComponent()->SetPrimaryEffect(name);
}

const std::string& Object3d::GetMeshEffect1Name() const {
    // 主効果が無い場合は静的な空文字列を返し、参照先の寿命を保つ。
    static const std::string empty;
    return meshEffectComponent_ ? meshEffectComponent_->GetPrimaryEffect() : empty;
}

void Object3d::SetMeshEffect2Name(const std::string& name) {
    // 副効果を設定した場合だけComponentを作成する。
    if (!meshEffectComponent_ && name.empty()) return;
    EnsureMeshEffectComponent()->SetSecondaryEffect(name);
}

const std::string& Object3d::GetMeshEffect2Name() const {
    // 副効果が未設定なら寿命の安定した空文字列を返す。
    static const std::string empty;
    return meshEffectComponent_ ? meshEffectComponent_->GetSecondaryEffect() : empty;
}

PathMoverComponent* Object3d::EnsurePathMoverComponent() {
    // 移動経路を設定するときだけComponentを作り、シーン保存へ反映する。
    if (!pathMoverComponent_) {
        pathMoverComponent_.emplace();
    }
    SetComponentPresenceMarker(std::string(kPathMoverComponentType), true);
    return &*pathMoverComponent_;
}

bool Object3d::RemovePathMoverComponent() {
    // 経路設定を消す際はGhostRecorderの再生も止め、古い動きを残さない。
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
    // 参照だけでは経路Componentを作らず、未設定をnullptrで知らせる。
    return pathMoverComponent_ ? &*pathMoverComponent_ : nullptr;
}

const PathMoverComponent* Object3d::GetPathMoverComponent() const {
    // 読み取り時の未設定はnullptrとして扱う。
    return pathMoverComponent_ ? &*pathMoverComponent_ : nullptr;
}

void Object3d::SetRecordPathName(const std::string& name) {
    // 実際に経路名を指定した場合だけ移動Componentを作成する。
    if (!pathMoverComponent_ && name.empty()) return;
    EnsurePathMoverComponent()->SetPathName(name);
}

const std::string& Object3d::GetRecordPathName() const {
    // 経路Componentが無い場合でも安全な空文字列参照を返す。
    static const std::string empty;
    return pathMoverComponent_ ? pathMoverComponent_->GetPathName() : empty;
}

void Object3d::SetRecordLoop(bool loop) {
    // 未追加のオブジェクトへfalseを設定しても、空の経路Componentは作らない。
    if (!pathMoverComponent_ && !loop) return;
    EnsurePathMoverComponent()->SetLoop(loop);
}

bool Object3d::IsRecordLoop() const {
    // 経路そのものが無い場合はループ設定も無効として返す。
    return pathMoverComponent_ && pathMoverComponent_->IsLoop();
}

void Object3d::SetRecordRelative(bool relative) {
    // 未追加状態のfalseは省き、相対経路を使うときだけComponentを作る。
    if (!pathMoverComponent_ && !relative) return;
    EnsurePathMoverComponent()->SetRelative(relative);
}

bool Object3d::IsRecordRelative() const {
    // 経路Componentが無い場合は絶対位置扱いにする。
    return pathMoverComponent_ && pathMoverComponent_->IsRelative();
}

GameplayLinkComponent* Object3d::EnsureGameplayLinkComponent() {
    // イベント接続を設定するときだけComponentを作り、シーン保存に存在を示す。
    if (!gameplayLinkComponent_) {
        gameplayLinkComponent_.emplace();
    }
    SetComponentPresenceMarker(std::string(kGameplayLinkComponentType), true);
    return &*gameplayLinkComponent_;
}

bool Object3d::RemoveGameplayLinkComponent() {
    // 接続情報をまとめて解除し、保存上のComponent存在も外す。
    if (!gameplayLinkComponent_) {
        return false;
    }
    gameplayLinkComponent_.reset();
    SetComponentPresenceMarker(std::string(kGameplayLinkComponentType), false);
    return true;
}

GameplayLinkComponent* Object3d::GetGameplayLinkComponent() {
    // 参照だけでは接続Componentを作らない。
    return gameplayLinkComponent_ ? &*gameplayLinkComponent_ : nullptr;
}

const GameplayLinkComponent* Object3d::GetGameplayLinkComponent() const {
    // 接続先が未設定ならnullptrを返す。
    return gameplayLinkComponent_ ? &*gameplayLinkComponent_ : nullptr;
}

void Object3d::ApplyWorldMatrix(const engine::Matrix4x4& worldMatrix) {
    // 受け取ったワールド姿勢をTransformへ分解し、描画用行列を再計算する。
    Object3dTransformUtils::ApplyMatrixToTransform(transform_, worldMatrix);
    UpdateLocalMatrix();
    UpdateWorldMatrix();
}

NavAgentComponent* Object3d::EnsureNavAgentComponent() {
    // 経路探索の設定が必要になった時点でComponentを作る。
    if (!navAgentComponent_) {
        navAgentComponent_.emplace();
    }
    SetComponentPresenceMarker(std::string(kNavAgentComponentType), true);
    return &*navAgentComponent_;
}

bool Object3d::RemoveNavAgentComponent() {
    // 経路探索設定を消し、シーン保存上の存在情報も解除する。
    if (!navAgentComponent_) return false;
    navAgentComponent_.reset();
    SetComponentPresenceMarker(std::string(kNavAgentComponentType), false);
    return true;
}

NavAgentComponent* Object3d::GetNavAgentComponent() {
    // 未追加の経路探索Componentは生成せずnullptrを返す。
    return navAgentComponent_ ? &*navAgentComponent_ : nullptr;
}

const NavAgentComponent* Object3d::GetNavAgentComponent() const {
    // 読み取り操作では経路探索設定を実体化しない。
    return navAgentComponent_ ? &*navAgentComponent_ : nullptr;
}

void Object3d::SetEventID(int id) {
    // 無効IDだけのオブジェクトには空の接続Componentを作らない。
    if (!gameplayLinkComponent_ && id < 0) return;
    EnsureGameplayLinkComponent()->SetEventId(id);
}

int Object3d::GetEventID() const {
    // 接続Componentが無ければ-1を未設定IDとして返す。
    return gameplayLinkComponent_ ? gameplayLinkComponent_->GetEventId() : -1;
}

void Object3d::SetTargetID(int id) {
    // 有効な接続先があるときだけComponentを確保する。
    if (!gameplayLinkComponent_ && id < 0) return;
    EnsureGameplayLinkComponent()->SetTargetId(id);
}

int Object3d::GetTargetID() const {
    // 接続先が無い場合は-1を返し、通常のIDと区別する。
    return gameplayLinkComponent_ ? gameplayLinkComponent_->GetTargetId() : -1;
}

std::vector<Object3d::BuiltInComponentInfo> Object3d::GetBuiltInComponentInfos() const {
    // 編集画面用に必須Componentと着脱可能Componentの存在を一覧化する。
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
    // 実体を探す経路と存在判定を共有し、表示だけの空項目を数えない。
    return FindBuiltInComponent(typeId) != nullptr;
}

void* Object3d::FindBuiltInComponent(std::string_view typeId) {
    // 検索条件をconst版に集約し、変更可能な呼び出し側へ同じ実体を返す。
    return const_cast<void*>(static_cast<const Object3d*>(this)->FindBuiltInComponent(typeId));
}

const void* Object3d::FindBuiltInComponent(std::string_view typeId) const {
    // 編集用の型IDから組み込みComponent実体へ対応付ける。
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
    // 非表示とカメラオブジェクトは影パスへ送らない。
    if (!GetIsRenderVisible() || IsCameraObject()) return;
    if (meshRenderer_) {
        meshRenderer_->DrawShadow();
    }
}

void Object3d::SetShadowCommonState() {
    // メッシュがある場合だけ影描画の共通パイプライン状態を設定する。
    if (meshRenderer_) {
        meshRenderer_->SetShadowCommonState();
    }
}

void Object3d::DrawShadowOnly() {
    // 影だけの描画でも可視性とカメラ除外の条件を本体描画と揃える。
    if (!GetIsRenderVisible() || IsCameraObject()) return;
    if (meshRenderer_) {
        meshRenderer_->DrawShadowOnly();
    }
}

void Object3d::DrawLocalFog(uint32_t depthSrvHandle) {
    if (GetIsRenderVisible() && meshRenderer_) {
        // 深度を参照する霧の描画はメッシュレンダラーの専用パスへ渡す。
        meshRenderer_->DrawLocalFog(depthSrvHandle);
    }
}

MeshRenderer::LocalFogData* Object3d::GetLocalFogData() {
    // メッシュが無いオブジェクトではローカルフォグ設定も存在しない。
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

} // namespace engine
