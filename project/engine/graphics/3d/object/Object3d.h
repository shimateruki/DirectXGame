#pragma once
#include "engine/utility/math/Math.h"
#include "Object3dCommon.h"
#include "Model.h"
#include <wrl.h>
#include <cstdint>
#include <string>
#include <memory>
#include <optional>
#include "Event.h"
#include <vector>
#include <string_view>
#include "Transform.h" 
#include "json.hpp"
#include "Collider.h"
#include "GameplayLinkComponent.h"
#include "MeshEffectComponent.h"
#include "MeshRenderer.h" 
#include "ParticleEmitterComponent.h"
#include "PathMoverComponent.h"
#include "NavAgentComponent.h"
#include "engine/graphics/3d/camera/SceneCameraSettings.h"
#include "engine/animation/AnimatorController.h"
#include "engine/system/collision/CollisionEvent.h"

using json = nlohmann::json;

class GhostRecorder;
class SceneManager;
class Camera;
#include "GPUParticleEmitter.h"
class EffectObject3d;

// Object3dは、Transform、階層、描画、衝突、エフェクト連携を持つ3Dオブジェクトの基本クラスです。
class Object3d {
public:
    /// 既存機能をComponentとして公開するための軽量な型情報です。
    /// 実データの所有場所は移さず、Editorと将来のRegistryから共通参照できます。
    struct BuiltInComponentInfo {
        std::string_view typeId;
        std::string_view displayName;
        bool isPresent = false;
        bool removable = false;
    };

    static constexpr std::string_view kTransformComponentType = "Transform";
    static constexpr std::string_view kMeshRendererComponentType = "MeshRenderer";
    static constexpr std::string_view kColliderComponentType = "Collider";
    static constexpr std::string_view kParticleEmitterComponentType = ParticleEmitterComponent::kTypeId;
    static constexpr std::string_view kMeshEffectComponentType = MeshEffectComponent::kTypeId;
    static constexpr std::string_view kPathMoverComponentType = PathMoverComponent::kTypeId;
    static constexpr std::string_view kGameplayLinkComponentType = GameplayLinkComponent::kTypeId;
    static constexpr std::string_view kNavAgentComponentType = NavAgentComponent::kTypeId;

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
        float attackPower = 1.0f;
        float speed = 1.0f;
        float gravity = 50.0f;
        float maxFallSpeed = 60.0f;
        float jumpPower = 10.0f;
        bool morphLimited = true;
        float morphDuration = 5.0f;
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
        std::string targetScene = "SELECT";
        float moveAmount = 10.0f;
        float moveSpeed = 6.0f;
        bool startActive = false;
        bool returnOnOff = true;
        EntityParameter() = default;
    };

    // デバッグリプレイで1フレーム分の実行状態を保持します。
    // シーン保存用JSONとは分離し、実行中だけ変化するHPやアニメーション時刻も扱います。
    struct ReplayState {
        Vector3 scale = { 1.0f, 1.0f, 1.0f };
        Vector3 rotation = { 0.0f, 0.0f, 0.0f };
        Vector3 translation = { 0.0f, 0.0f, 0.0f };
        Quaternion quaternion = { 0.0f, 0.0f, 0.0f, 1.0f };
        bool quaternionMaster = true;

        bool visible = true;
        bool dead = false;
        bool collecting = false;
        float collectTimer = 0.0f;
        float animationTime = 0.0f;
        std::string animationName;
        bool animationLoop = true;
        std::string animatorControllerPath;
        AnimatorControllerRuntime::Snapshot animatorSnapshot;

        bool hasParameter = false;
        EntityParameter parameter;
        uint32_t collisionAttribute = 0;
        uint32_t collisionMask = 0;

        std::string modelName;
        std::string texturePath;
        int32_t materialType = 0;
        Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        float emissive = 1.0f;

        // 実体Componentの有無と実行状態も巻き戻し対象にします。
        // ParticleEmitterComponentには発生間隔タイマーも含まれます。
        std::optional<ParticleEmitterComponent> particleEmitterComponent;
        std::optional<MeshEffectComponent> meshEffectComponent;
        std::optional<PathMoverComponent> pathMoverComponent;
        std::optional<GameplayLinkComponent> gameplayLinkComponent;
        std::optional<NavAgentComponent> navAgentComponent;

        bool replayRemoved = false;
        json custom = json::object();
    };

    /// Prefab Asset内の元Objectと、Scene上のInstanceを結び付ける情報です。
    struct PrefabInstanceInfo {
        std::string assetId;
        std::string prefabName;
        std::string instanceId;
        std::string sourceObjectId;
        bool isRoot = false;

        bool IsLinked() const {
            return !assetId.empty() && !instanceId.empty() && !sourceObjectId.empty();
        }
    };

    using ColliderConfig = ::ColliderConfig;

public:
    virtual ~Object3d();

    virtual void Initialize(Object3dCommon* common);
    virtual void Update(float deltaTime);
    // 物理や決定論的なゲーム処理向けの固定刻み更新です。
    // 既存のUpdate互換性を壊さないよう、既定実装は何もしません。
    virtual void FixedUpdate(float fixedDeltaTime) { (void)fixedDeltaTime; }
    /// ステータス管理からタイプ共通スケールを反映します。
    /// 伸縮アニメーションを持つ派生クラスは、基準スケールも更新してください。
    virtual void ApplyManagedScale(const Vector3& scale) { SetScale(scale); }
    void UpdateParticle();
    virtual void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);
    virtual void DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource, int previewBufferIndex = 0);
    virtual void DrawSpecialMaterialForCamera(Camera* camera, uint32_t depthSrvHandle, uint32_t grabSrvHandle, int previewBufferIndex = 0);
    void DrawShadow();
    void SetShadowCommonState(); // 共通の状態を設定
    void DrawShadowOnly();       // 設定済みの状態で描画だけ行う
    void DrawLocalFog(uint32_t depthSrvHandle);

    MeshRenderer::LocalFogData* GetLocalFogData();
    virtual std::unique_ptr<Object3d> Clone() const;

    // Scene保存、Prefab Instance、EditorのUndo/Redoで共通利用する永続GUIDです。
    // Replay IDやPrefab内Source ID、ゲームイベントIDとは別の識別子です。
    static std::string GeneratePersistentGuid();
    static bool IsPersistentGuidValid(std::string_view guid);
    const std::string& EnsurePersistentGuid();
    const std::string& GetPersistentGuid() const { return persistentGuid_; }
    bool SetPersistentGuid(const std::string& guid);
    void RegeneratePersistentGuid();

    std::vector<BuiltInComponentInfo> GetBuiltInComponentInfos() const;
    bool HasBuiltInComponent(std::string_view typeId) const;
    void* FindBuiltInComponent(std::string_view typeId);
    const void* FindBuiltInComponent(std::string_view typeId) const;

    // デバッグリプレイ専用の安定IDと実行状態を扱います。
    uint64_t EnsureReplayId();
    uint64_t GetReplayId() const { return replayId_; }
    ReplayState CaptureReplayState() const;
    void RestoreReplayState(const ReplayState& state);
    void SetReplayRetained(bool retained) { replayRetained_ = retained; }
    bool IsReplayRetained() const { return replayRetained_; }
    void SetReplayRemoved(bool removed) { replayRemoved_ = removed; }
    bool IsReplayRemoved() const { return replayRemoved_; }

    // トランスフォーム
    void UpdateLocalMatrix();
    // 階層のワールド行列を更新します。ゲームの一括更新中はfalseを指定し、
    // 全オブジェクトのロジック更新後に描画定数を一度だけ確定できます。
    void UpdateWorldMatrix(bool refreshRenderer = true);
    void RefreshRenderCameraData();

    Transform* GetTransform() { return &transform_; }
    const Transform& GetTransform() const { return transform_; }

    Vector3 GetWorldPosition() const {
        return {
            transform_.matWorld.m[3][0],
            transform_.matWorld.m[3][1],
            transform_.matWorld.m[3][2],
        };
    }
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

    void SetParent(Object3d* parent, bool keepWorldTransform = false);
    Object3d* GetParent() const { return parent_; }

    // グラフィックス (MeshRendererへ委譲)
        // 描画マテリアルやLODを編集するためのMeshRendererを取得します。
MeshRenderer* GetMeshRenderer() const { return meshRenderer_.get(); }

    void SetModel(Model* model);
    void SetModel(const std::string& modelName);
    Model* GetModel() const;
    std::string GetModelName() const;
    void SetMeshDrawIndex(int meshIndex);
    int GetMeshDrawIndex() const;
    bool IsMeshDrawFiltered() const;
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
    const PrefabInstanceInfo& GetPrefabInstanceInfo() const { return prefabInstanceInfo_; }
    void SetPrefabInstanceInfo(const PrefabInstanceInfo& info) { prefabInstanceInfo_ = info; }
    void ClearPrefabInstanceInfo() { prefabInstanceInfo_ = PrefabInstanceInfo{}; }
    bool IsPrefabInstance() const { return prefabInstanceInfo_.IsLinked(); }

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
    bool LoadTerrainCollisionFromFile(const std::string& path);
    void SetTerrainCollisionData(const TerrainCollisionData& data, const std::string& path = "");
    const TerrainCollisionData* GetTerrainCollisionData() const;
    void SetTerrainCollisionPath(const std::string& path) { terrainCollisionPath_ = path; }
    const std::string& GetTerrainCollisionPath() const { return terrainCollisionPath_; }
    void SetMetallic(float metallic) { if (meshRenderer_) meshRenderer_->SetMetallic(metallic); }
    float GetMetallic() const { return meshRenderer_ ? meshRenderer_->GetMetallic() : 0.0f; }
    void SetRoughness(float roughness) { if (meshRenderer_) meshRenderer_->SetRoughness(roughness); }
    float GetRoughness() const { return meshRenderer_ ? meshRenderer_->GetRoughness() : 0.3f; }
    AABB GetAABB() const;
    OBB GetOBB() const;
    AABB GetModelWorldAABB() const; // モデルデータに基づいたワールド空間AABB

    CollisionInfo CheckCollision(Object3d* other);
    virtual bool OnCollision(Object3d* other) { (void)other; return false; }
    virtual void OnCollisionEnter(const CollisionEvent& event) { (void)event; }
    virtual void OnCollisionStay(const CollisionEvent& event) { (void)event; }
    virtual void OnCollisionExit(const CollisionEvent& event) { (void)event; }
    virtual void OnTriggerEnter(const CollisionEvent& event) { (void)event; }
    virtual void OnTriggerStay(const CollisionEvent& event) { (void)event; }
    virtual void OnTriggerExit(const CollisionEvent& event) { (void)event; }

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
    bool IsCameraObject() const { return className_ == "Camera" || className_ == "CinematicCamera"; }
    SceneCameraSettings& GetSceneCameraSettings() { return sceneCameraSettings_; }
    const SceneCameraSettings& GetSceneCameraSettings() const { return sceneCameraSettings_; }
    void SetSceneCameraSettings(const SceneCameraSettings& settings) { sceneCameraSettings_ = settings; }
    void SetTag(const std::string& tag) { tag_ = tag; }
    const std::string& GetTag() const { return tag_; }
    bool HasTag(const std::string& tag) const { return tag_ == tag; }
    void SetLayer(const std::string& layer) { layer_ = layer.empty() ? "Default" : layer; }
    const std::string& GetLayer() const { return layer_; }
    bool IsLayer(const std::string& layer) const { return layer_ == layer; }
    void SetIsVisible(bool visible) { isVisible_ = visible; }
    bool GetIsVisible() const { return isVisible_; }
    void SetCastShadow(bool enabled) { castShadow_ = enabled; }
    bool GetCastShadow() const { return castShadow_; }

    GameplayLinkComponent* EnsureGameplayLinkComponent();
    bool RemoveGameplayLinkComponent();
    bool HasGameplayLinkComponent() const { return gameplayLinkComponent_.has_value(); }
    GameplayLinkComponent* GetGameplayLinkComponent();
    const GameplayLinkComponent* GetGameplayLinkComponent() const;
    NavAgentComponent* EnsureNavAgentComponent();
    bool RemoveNavAgentComponent();
    bool HasNavAgentComponent() const { return navAgentComponent_.has_value(); }
    NavAgentComponent* GetNavAgentComponent();
    const NavAgentComponent* GetNavAgentComponent() const;
    void SetEventID(int id);
    int GetEventID() const;
    void SetTargetID(int id);
    int GetTargetID() const;

    ParticleEmitterComponent* EnsureParticleEmitterComponent();
    bool RemoveParticleEmitterComponent();
    bool HasParticleEmitterComponent() const { return particleEmitterComponent_.has_value(); }
    ParticleEmitterComponent* GetParticleEmitterComponent();
    const ParticleEmitterComponent* GetParticleEmitterComponent() const;
    void SetParticleName(const std::string& name);
    const std::string& GetParticleName() const;
    void SetGPUParticleName(const std::string& name);
    const std::string& GetGPUParticleName() const;

    MeshEffectComponent* EnsureMeshEffectComponent();
    bool RemoveMeshEffectComponent();
    bool HasMeshEffectComponent() const { return meshEffectComponent_.has_value(); }
    MeshEffectComponent* GetMeshEffectComponent();
    const MeshEffectComponent* GetMeshEffectComponent() const;
    void SetMeshEffect1Name(const std::string& name);
    const std::string& GetMeshEffect1Name() const;
    void SetMeshEffect2Name(const std::string& name);
    const std::string& GetMeshEffect2Name() const;

    PathMoverComponent* EnsurePathMoverComponent();
    bool RemovePathMoverComponent();
    bool HasPathMoverComponent() const { return pathMoverComponent_.has_value(); }
    PathMoverComponent* GetPathMoverComponent();
    const PathMoverComponent* GetPathMoverComponent() const;
    void SetRecordPathName(const std::string& name);
    const std::string& GetRecordPathName() const;
    void SetRecordLoop(bool loop);
    bool IsRecordLoop() const;
    void SetRecordRelative(bool relative);
    bool IsRecordRelative() const;

    void UpdateAttachedEffects(float deltaTime);
    void DrawAttachedEffects(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    void StartCollectionAnimation() { isCollecting_ = true; collectTimer_ = 0.0f; }
    bool IsCollecting() const { return isCollecting_; }

    // Animator Controllerは状態、条件、補間を管理し、Model Animationを滑らかに切り替えます。
    bool SetAnimatorController(const std::string& assetPath);
    void ClearAnimatorController();
    const std::string& GetAnimatorControllerPath() const { return animatorControllerPath_; }
    bool HasAnimatorController() const { return animatorControllerLoaded_; }
    AnimatorControllerAsset* GetAnimatorControllerAsset() { return animatorControllerLoaded_ ? &animatorControllerAsset_ : nullptr; }
    AnimatorControllerRuntime* GetAnimatorControllerRuntime() { return animatorControllerLoaded_ ? &animatorControllerRuntime_ : nullptr; }
    const AnimatorControllerRuntime* GetAnimatorControllerRuntime() const { return animatorControllerLoaded_ ? &animatorControllerRuntime_ : nullptr; }
    bool PlayAnimatorState(const std::string& stateName, float blendDuration = -1.0f, int easing = -1);
    bool EvaluateAnimatorState(
        const std::string& stateName,
        float timeSeconds,
        bool loop,
        float blendDuration,
        int easing,
        bool exactPreview);
    std::string GetAnimatorCurrentStateName() const;
    AnimatorControllerRuntime::Snapshot CaptureAnimatorSnapshot() const;
    void RestoreAnimatorSnapshot(const AnimatorControllerRuntime::Snapshot& snapshot);
    float GetAnimationTime() const { return animationTime_; }
    void RestoreAnimationPlayback(const std::string& animationName, float timeSeconds, bool loop);

    json ExportToJson();
    void ImportFromJson(const json& j);
    /// 実体Componentを新しいcomponents Payloadへ直列化します。未知のPayloadも保持します。
    json SerializeFeatureComponents() const;
    /// 新しいcomponents Payloadを優先し、旧Top-Level JSONをFallbackとして読み込みます。
    void DeserializeFeatureComponents(const json& objectData);
    const json& GetOpaqueComponents() const { return opaqueComponents_; }
    void SetOpaqueComponents(const json& components) {
        opaqueComponents_ = components.is_object() ? components : json::object();
    }
    /// 値が未設定でもComponentが追加済みであることを保存するMarkerを確認します。
    bool HasComponentPresenceMarker(const std::string& componentTypeId) const;
    /// Componentの追加・削除状態だけを更新し、未知のComponentデータは保持します。
    void SetComponentPresenceMarker(const std::string& componentTypeId, bool present);

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
    void SetEnableLighting(bool enable) { if (meshRenderer_) meshRenderer_->SetEnableLighting(enable); }
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
    void DrawWindOrb(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    virtual bool HasOwnedSpecialMaterialVisuals() const { return false; }
    virtual void DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
        (void)depthSrvHandle;
        (void)grabSrvHandle;
    }
    // --- ボーンアニメーション用 ---
    std::string animName_ = "";
    bool isAnimLoop_ = true;

    // GhostRecorder本体はPathMoverComponentの再生Runtimeとして段階的に分離します。
    GhostRecorder* recorder_ = nullptr;
    void SetSaveCategory(const std::string& category) { saveCategory_ = category; }
    std::string GetSaveCategory() const { return saveCategory_; }
    bool GetIsLocked() const { return isLocked_; }
    void SetIsLocked(bool locked) { isLocked_ = locked; }
    // Editorの描画補助専用Object。Sceneには存在するが通常の選択・一覧・保存対象にはしない。
    void SetEditorInternal(bool editorInternal) { isEditorInternal_ = editorInternal; }
    bool IsEditorInternal() const { return isEditorInternal_; }

protected:
    virtual void CaptureReplayCustomState(json& state) const;
    virtual void RestoreReplayCustomState(const json& state);

    Object3dCommon* common_ = nullptr;
    std::string name_ = "Object";
    std::string persistentGuid_;
    // 未知Componentを読み込んでも再保存時に失わないための透過データです。
    json opaqueComponents_ = json::object();

    Transform transform_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform effectAnchor_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Object3d* parent_ = nullptr;

    float animationTime_ = 0.0f;
    std::string animatorControllerPath_;
    AnimatorControllerAsset animatorControllerAsset_;
    AnimatorControllerRuntime animatorControllerRuntime_;
    bool animatorControllerLoaded_ = false;

    bool UpdateAnimatorController(float deltaTime, Model* model);
    bool ApplyAnimatorControllerPose(Model* model, bool forceModelUpdate);

    // コンポーネント化された機能
    std::unique_ptr<Collider> collider_;
    std::unique_ptr<MeshRenderer> meshRenderer_;
    std::string terrainCollisionPath_;

    bool isStatic_ = false;
    std::vector<Object3d*> children_;
    std::string className_ = "Model";
    SceneCameraSettings sceneCameraSettings_;
    std::string tag_;
    std::string layer_ = "Default";
    bool isVisible_ = true;
    bool castShadow_ = true;
    PrefabInstanceInfo prefabInstanceInfo_;
    std::string enemyType_ = "";
    std::string gimmickType_ = "";
    std::string itemType_ = "";

    std::optional<ParticleEmitterComponent> particleEmitterComponent_;
    std::optional<MeshEffectComponent> meshEffectComponent_;
    std::optional<PathMoverComponent> pathMoverComponent_;
    std::optional<GameplayLinkComponent> gameplayLinkComponent_;
    std::optional<NavAgentComponent> navAgentComponent_;
    std::string saveCategory_ = "Object";
    bool isLocked_ = false;
    bool isEditorInternal_ = false;
    bool isCollecting_ = false;
    float collectTimer_ = 0.0f;
    uint64_t replayId_ = 0;
    bool replayRetained_ = false;
    bool replayRemoved_ = false;
    std::unique_ptr<GPUParticleEmitter> gpuEmitter_ = nullptr;
    // MeshEffectComponentの再生中InstanceはObject3d側の描画Runtimeとして保持します。
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
