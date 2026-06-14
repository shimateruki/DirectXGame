#pragma once
#include "engine/utility/math/Math.h"
#include "Object3dCommon.h"
#include "Model.h"
#include <wrl.h>
#include <string>
#include <memory>
#include <optional>
#include "Event.h"
#include <vector>
#include "Transform.h" 
#include "json.hpp"
#include "Collider.h"
#include "MeshRenderer.h" 

using json = nlohmann::json;

class GhostRecorder;
class SceneManager;
#include "GPUParticleEmitter.h"
class EffectObject3d;

class Object3d {
public:
    // 互換性維持のためエイリアスを作成
    using TransformationMatrix = MeshRenderer::TransformationMatrix;
    using DirectionalLight = MeshRenderer::DirectionalLight;
    using CameraForGPU = MeshRenderer::CameraForGPU;
    using Material = MeshRenderer::MaterialData;
    using LodLevel = MeshRenderer::LodLevel;

    // ゲームロジック用パラメータ
    struct EntityParameter {
        float hp = 100.0f;
        float maxHp = 100.0f;
        float speed = 1.0f;
        float gravity = 50.0f;
        float maxFallSpeed = 60.0f;
        float jumpPower = 10.0f;
        std::string enemyType = "";
        std::string gimmickType = "";
        std::string itemType = "";
        float healAmount = 1.0f;
        float interval = 3.0f;
        int maxCount = 5;
        float detectionRange = 20.0f;
        float shakeDuration = 1.0f;
        float fallDuration = 2.0f;
        int colorType = 0; // 0: Blue, 1: Red
        int switchMode = 0; // 0: Momentary, 1: Toggle, 2: Timed
        int actionMode = 0; // 0: Appear, 1: MoveY, 2: MoveX, 3: MoveZ, 4: Enable, 5: Disable
        float moveAmount = 10.0f;
        float moveSpeed = 6.0f;
        bool startActive = false;
        bool returnOnOff = true;
        EntityParameter() = default;
    };

    using ColliderConfig = ::ColliderConfig;

public:
    virtual ~Object3d();

    virtual void Initialize(Object3dCommon* common);
    virtual void Update(float deltaTime);
    void UpdateParticle();
    virtual void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);
    void DrawShadow();
    void SetShadowCommonState(); // 共通の状態を設定
    void DrawShadowOnly();       // 設定済みの状態で描画だけ行う
    void DrawLocalFog(uint32_t depthSrvHandle);

    MeshRenderer::LocalFogData* GetLocalFogData();
    virtual std::unique_ptr<Object3d> Clone() const;

    // トランスフォーム
    void UpdateLocalMatrix();
    void UpdateWorldMatrix();

    Transform* GetTransform() { return &transform_; }
    const Transform& GetTransform() const { return transform_; }

    const Vector3& GetWorldPosition() const { return transform_.translate; }
    const Matrix4x4& GetWorldMatrix() const { return transform_.matWorld; }
    const Vector3& GetScale() const { return transform_.scale; }
    const Vector3& GetRotation() const { return transform_.rotate; }
    const Vector3& GetTranslate() const { return transform_.translate; }

    void SetTranslate(const Vector3& translate) { transform_.translate = translate; }
    void SetScale(const Vector3& scale) { transform_.scale = scale; }
    void SetRotation(const Vector3& rotate) { transform_.rotate = rotate; transform_.isQuaternionMaster = false;
    }
    void SetRotationY(float y) { transform_.rotate.y = y; transform_.isQuaternionMaster = false;
    }

    void SetParent(Object3d* parent);
    Object3d* GetParent() const { return parent_; }

    // グラフィックス (MeshRendererへ委譲)
    MeshRenderer* GetMeshRenderer() const { return meshRenderer_.get(); }

    void SetModel(Model* model);
    void SetModel(const std::string& modelName);
    Model* GetModel() const;
    std::string GetModelName() const;
    void SetLodEnabled(bool enabled);
    bool IsLodEnabled() const;
    bool HasLodLevels() const;
    const std::vector<LodLevel>& GetLodLevels() const;
    void SetLodLevels(const std::vector<LodLevel>& levels);
    void ClearLodLevels();
    bool SetLodLevelDistance(int level, float distance);
    bool ReloadLodManifest();
    int GetActiveLodLevel() const;
    std::string GetActiveLodModelName() const;
    float GetLodCameraDistance() const;

    Vector4 GetColor() const;
    void SetColor(const Vector4& color);

    void SetBlendMode(BlendMode blendMode);
    BlendMode GetBlendMode() const;

    void SetIntensity(float intensity);
    float GetIntensity() const;

    // データアクセッサ (互換性維持)
    DirectionalLight* GetDirectionalLightData();
    Material* GetMaterialData(); // 名前変更なしでMaterialを返す

    void SetMaterialType(int32_t type);
    void SetShininess(float shininess);
    int32_t GetMaterialType() const;
    void SetSelectedLighting(int32_t type);

    void SetName(const std::string& name) { name_ = name; }
    const std::string& GetName() const { return name_; }

    // 衝突判定 (Colliderへ委譲)
    Collider* GetCollider() const { return collider_.get(); }

    void SetColliderConfig(const ColliderConfig& config);
    const ColliderConfig& GetColliderConfig() const;

    void SetColliderType(ColliderType type);
    ColliderType GetColliderType() const;
    void SetCollisionSize(const Vector3& size);
    Vector3 GetCollisionSize() const;
    void SetCollisionRadius(float radius);
    float GetCollisionRadius() const;

    void SetCollisionAttribute(uint32_t attribute);
    uint32_t GetCollisionAttribute() const;
    void SetCollisionMask(uint32_t mask);
    uint32_t GetCollisionMask() const;
    void SetMetallic(float metallic) { if (meshRenderer_) meshRenderer_->SetMetallic(metallic); }
    float GetMetallic() const { return meshRenderer_ ? meshRenderer_->GetMetallic() : 0.0f; }
    void SetRoughness(float roughness) { if (meshRenderer_) meshRenderer_->SetRoughness(roughness); }
    float GetRoughness() const { return meshRenderer_ ? meshRenderer_->GetRoughness() : 0.3f; }
    AABB GetAABB() const;
    OBB GetOBB() const;
    AABB GetModelWorldAABB() const; // モデルデータに基づいたワールド空間AABB

    CollisionInfo CheckCollision(Object3d* other);
    virtual bool OnCollision(Object3d* other) { (void)other; return false; }

    void SetStatic(bool isStatic) { isStatic_ = isStatic; }
    bool IsStatic() const { return isStatic_; }

    // ゲームロジック
    std::optional<EntityParameter> param_;
    bool isDead = false;
    EventType eventType_ = EventType::None;

    void SetEventType(EventType type) { eventType_ = type; }
    EventType GetEventType() const { return eventType_; }
    const std::vector<Object3d*>& GetChildren() const { return children_; }

    void SetClassName(const std::string& name) { className_ = name; }
    std::string GetClassName() const { return className_; }
    void SetIsVisible(bool visible) { isVisible_ = visible; }
    bool GetIsVisible() const { return isVisible_; }

    void SetEventID(int id) { eventID_ = id; }
    int GetEventID() const { return eventID_; }
    void SetTargetID(int id) { targetID_ = id; }
    int GetTargetID() const { return targetID_; }

    void SetParticleName(const std::string& name) { particleName_ = name; }
    const std::string& GetParticleName() const { return particleName_; }

    void SetGPUParticleName(const std::string& name) { gpuParticleName_ = name; }
    const std::string& GetGPUParticleName() const { return gpuParticleName_; }

    void SetMeshEffect1Name(const std::string& name) { meshEffectName1_ = name; }
    const std::string& GetMeshEffect1Name() const { return meshEffectName1_; }
    void SetMeshEffect2Name(const std::string& name) { meshEffectName2_ = name; }
    const std::string& GetMeshEffect2Name() const { return meshEffectName2_; }

    void UpdateAttachedEffects(float deltaTime);
    void DrawAttachedEffects(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    void StartCollectionAnimation() { isCollecting_ = true; collectTimer_ = 0.0f; }
    bool IsCollecting() const { return isCollecting_; }

    json ExportToJson();
    void ImportFromJson(const json& j);

    virtual void OnTrigger() { isVisible_ = false; }
    virtual void OnSwitchEvent(bool active) { if (active) OnTrigger(); }
    void InitializeRecorder(SceneManager* sceneManager);
    void CopyFrom(const Object3d* other);
    void SetEnemyType(const std::string& type) { enemyType_ = type; }
    std::string GetEnemyType() const { return enemyType_; }
    void SetGimmickType(const std::string& type) { gimmickType_ = type; }
    std::string GetGimmickType() const { return gimmickType_; }
    void SetItemType(const std::string& type) { itemType_ = type; }
    std::string GetItemType() const { return itemType_; }
    virtual void OnRecordEvent(int eventID) {}
    // ゲッター・セッターがいっぱい並んでいるあたりに追加
    void SetEnableNormalMap(bool enable) { if (meshRenderer_) meshRenderer_->SetEnableNormalMap(enable); }
    bool GetEnableNormalMap() const { return meshRenderer_ ? meshRenderer_->GetEnableNormalMap() : false; }
    void SetNormalMap(const std::string& texturePath) { if (meshRenderer_) meshRenderer_->SetNormalMap(texturePath); }
    std::string GetNormalMapPath() const { return meshRenderer_ ? meshRenderer_->GetNormalMapPath() : ""; }
    void SetOrmMap(const std::string& texturePath) { if (meshRenderer_) meshRenderer_->SetOrmMap(texturePath); }
    std::string GetOrmMapPath() const { return meshRenderer_ ? meshRenderer_->GetOrmMapPath() : ""; }

    void SetTexture(const std::string& texturePath) { if (meshRenderer_) meshRenderer_->SetTexture(texturePath); }
    std::string GetTexturePath() const { return meshRenderer_ ? meshRenderer_->GetTexturePath() : ""; }
    void SetTextureTiling(const Vector2& tiling) { if (meshRenderer_) meshRenderer_->SetTextureTiling(tiling); }
    Vector2 GetTextureTiling() const { return meshRenderer_ ? meshRenderer_->GetTextureTiling() : Vector2{ 1.0f, 1.0f }; }
    void SetAutoTextureTiling(bool enabled) { if (meshRenderer_) meshRenderer_->SetAutoTextureTiling(enabled); }
    bool GetAutoTextureTiling() const { return meshRenderer_ ? meshRenderer_->GetAutoTextureTiling() : false; }
    void SetEnableEnvMap(bool enable) { if (meshRenderer_) meshRenderer_->SetEnableEnvMap(enable); }
    bool GetEnableEnvMap() const { return meshRenderer_ ? meshRenderer_->GetEnableEnvMap() : true; }
    bool GetEnableLighting() const { return meshRenderer_ ? meshRenderer_->GetEnableLighting() : false; }

    void SetEnvIntensity(float intensity) { if (meshRenderer_) meshRenderer_->SetEnvIntensity(intensity); }
    float GetEnvIntensity() const { return meshRenderer_ ? meshRenderer_->GetEnvIntensity() : 1.0f; }
    void SetEmissive(float emissive) { if (meshRenderer_) meshRenderer_->SetEmissive(emissive); }
    float GetEmissive() const { return meshRenderer_ ? meshRenderer_->GetEmissive() : 1.0f; }
    void SetIsUIPreview(bool isPreview) { 
        if (meshRenderer_) meshRenderer_->SetIsUIPreview(isPreview); 
    }
    void DrawWater(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    void DrawMagma(uint32_t depthSrvHandle, uint32_t colorSrvHandle);
    void DrawIce(uint32_t depthSrvHandle, uint32_t colorSrvHandle);  
    void DrawFire(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    void DrawLaser(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    void DrawSlimeGel(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    void DrawShockwave(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    void DrawLiquidContact(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    void DrawDamageCrack(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    void DrawUpdraft(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    void DrawStunBind(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    void DrawCrownUnlock(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    void DrawPoisonSpore(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    void DrawCloud(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    void DrawGatePortal(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    // --- ボーンアニメーション用 ---
    std::string animName_ = "";
    bool isAnimLoop_ = true;

    // --- GhostRecorder (パス再生) 用 ---
    std::string recordPathName_ = ""; // 読み込むパスデータのファイル名
    bool isRecordLoop_ = false;       // パスのループフラグ
    bool isRecordRelative_ = false;   // パスの相対再生フラグ
    GhostRecorder* recorder_ = nullptr;
    void SetSaveCategory(const std::string& category) { saveCategory_ = category; }
    std::string GetSaveCategory() const { return saveCategory_; }
    bool GetIsLocked() const { return isLocked_; }
    void SetIsLocked(bool locked) { isLocked_ = locked; }

protected:
    Object3dCommon* common_ = nullptr;
    std::string name_ = "Object";

    Transform transform_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform effectAnchor_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Object3d* parent_ = nullptr;

    float animationTime_ = 0.0f;

    // コンポーネント化された機能
    std::unique_ptr<Collider> collider_;
    std::unique_ptr<MeshRenderer> meshRenderer_;

    bool isStatic_ = false;
    std::vector<Object3d*> children_;
    std::string className_ = "Model";
    bool isVisible_ = true;
    int eventID_ = -1;
    int targetID_ = -1;
    std::string enemyType_ = "";
    std::string gimmickType_ = "";
    std::string itemType_ = "";

    std::string particleName_ = ""; // JSONファイル名
    std::string gpuParticleName_ = ""; // GPUパーティクル用
    float particleTimer_ = 0.0f;    // 発射タイミング管理用
    std::string saveCategory_ = "Object";
    bool isLocked_ = false;
    bool isCollecting_ = false;
    float collectTimer_ = 0.0f;
    std::unique_ptr<GPUParticleEmitter> gpuEmitter_ = nullptr;
    // 先ほどは <EffectObject3d> にしていましたが、<Object3d> に変更します
    std::string meshEffectName1_ = "";
    std::string meshEffectName2_ = "";
    std::string currentMeshEffect1_ = "";
    std::string currentMeshEffect2_ = "";
    std::vector<std::unique_ptr<Object3d>> attachedEffects1_;
    std::vector<std::unique_ptr<Object3d>> attachedEffects2_;


    // --- パフォーマンス計測用 ---
    float cpuUpdateTimeMs_ = 0.0f;
    float cpuDrawTimeMs_ = 0.0f;
    float cpuAnimTimeMs_ = 0.0f;
    float cpuMatrixTimeMs_ = 0.0f;

public:
    float GetCpuUpdateTimeMs() const { return cpuUpdateTimeMs_; }
    float GetCpuDrawTimeMs() const { return cpuDrawTimeMs_; }
    float GetCpuAnimTimeMs() const { return cpuAnimTimeMs_; }
    float GetCpuMatrixTimeMs() const { return cpuMatrixTimeMs_; }
};
