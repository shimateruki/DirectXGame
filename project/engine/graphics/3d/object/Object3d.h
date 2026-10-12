#pragma once
#include "engine/utility/math/Math.h"
#include "Object3dCommon.h"
#include "Model.h"
#include <functional>
#include <wrl.h>
#include <cstdint>
#include <string>
#include <memory>
#include <optional>
#include "Event.h"
#include <vector>
#include <string_view>
#include <unordered_map>
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
#include "engine/animation/BodyAnimationClip.h"
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
    // DecalSettingsは、通常のObject3dを地面・壁へ沿う永続/一時Decalとして扱う設定です。
    struct DecalSettings {
        bool enabled = false;
        Vector2 size = { 2.0f, 2.0f };
        float depthOffset = 0.012f;
        float lifetime = 0.0f;
        float fadeIn = 0.0f;
        float fadeOut = 0.35f;
        bool transient = false;
    };

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
        int actionMode = 0; // 0: Appear, 1: MoveY, 2: MoveX, 3: MoveZ, 4: Enable, 5: Disable, 6: BossCrownDrop
        std::string targetScene = "SELECT";
        float moveAmount = 10.0f;
        float moveSpeed = 6.0f;
        bool startActive = false;
        bool returnOnOff = true;
        bool motorContinuousCollision = false;
        bool motorSnapToGround = false;
        float motorMaxSlopeDegrees = 45.573f;
        float motorStepHeight = 0.0f;
        float motorGroundProbeDistance = 0.18f;
        float motorSkinWidth = 0.025f;
        int volumeMode = 0; // 0: Event, 1: Checkpoint, 2: FallDeath, 3: FeedbackCue, 4: BGM, 5: Environment
        std::string volumePayload = "";
        bool volumeTriggerOnce = true;
        bool volumeTriggerOnExit = false;
        float volumeRearmDelay = 0.0f;
        float volumeBlendDuration = 0.75f;
        std::string copyMemoryTypeA = "FireSlime";
        std::string copyMemoryTypeB = "ThunderSlime";
        std::string copyMemoryTypeC = "WindSlime";
        std::string copyMemoryTypeD; // 空欄なら従来の3台構成を維持します。
        float copyMemoryActivationRadius = 0.9f;
        float copyMemoryEssenceScale = 1.0f;
        float copyMemoryHoverAmount = 0.10f;
        float copyMemoryGlow = 0.45f;
        bool copyMemoryUnlimitedDuration = true;
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
    /// <summary>所有する描画・衝突コンポーネントを破棄する。</summary>
    virtual ~Object3d();

    /// <summary>描画共通機能を受け取り、オブジェクトの実行用コンポーネントを初期化する。</summary>
    virtual void Initialize(Object3dCommon* common);

    /// <summary>1フレーム分のアニメーション、移動、付随効果を更新する。</summary>
    virtual void Update(float deltaTime);

    // 物理や決定論的なゲーム処理向けの固定刻み更新です。
    // 既存のUpdate互換性を壊さないよう、既定実装は何もしません。
    virtual void FixedUpdate(float fixedDeltaTime) { (void)fixedDeltaTime; }

    /// <summary>ステータス管理からタイプ共通スケールを反映する。</summary>
    /// <remarks>伸縮アニメーションを持つ派生クラスは基準スケールも更新すること。</remarks>
    virtual void ApplyManagedScale(const Vector3& scale) { SetScale(scale); }

    /// <summary>このオブジェクトに接続されたパーティクルの状態を更新する。</summary>
    void UpdateParticle();

    /// <summary>有効な描画コンポーネントで通常の3Dモデルを描画する。</summary>
    virtual void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    /// <summary>通常描画定数を上書きせず、指定カメラのプレビューに描画する。</summary>
    virtual void DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource, int previewBufferIndex = 0);

    /// <summary>指定カメラのプレビューに特殊マテリアルを描画する。</summary>
    virtual void DrawSpecialMaterialForCamera(Camera* camera, uint32_t depthSrvHandle, uint32_t grabSrvHandle, int previewBufferIndex = 0);

    /// <summary>このオブジェクトの影マップ用形状を描画する。</summary>
    void DrawShadow();

    /// <summary>複数オブジェクトの影描画に共通するGPU状態を設定する。</summary>
    void SetShadowCommonState();

    /// <summary>設定済みの影描画状態で、このオブジェクトの形状だけを描画する。</summary>
    void DrawShadowOnly();

    /// <summary>シーン深度を参照してローカルフォグを描画する。</summary>
    void DrawLocalFog(uint32_t depthSrvHandle);

    /// <summary>ローカルフォグのGPUパラメータを編集用に取得する。</summary>
    MeshRenderer::LocalFogData* GetLocalFogData();

    /// <summary>このオブジェクトの複製を作る。</summary>
    virtual std::unique_ptr<Object3d> Clone() const;

    // Scene保存、Prefab Instance、EditorのUndo/Redoで共通利用する永続GUIDです。
    // Replay IDやPrefab内Source ID、ゲームイベントIDとは別の識別子です。
    static std::string GeneratePersistentGuid();

    /// <summary>永続GUIDが想定する形式か確認する。</summary>
    static bool IsPersistentGuidValid(std::string_view guid);

    /// <summary>未設定なら永続GUIDを生成し、現在の値を返す。</summary>
    const std::string& EnsurePersistentGuid();
    const std::string& GetPersistentGuid() const { return persistentGuid_; }

    /// <summary>検証済みGUIDを設定する。形式が不正ならfalse。</summary>
    bool SetPersistentGuid(const std::string& guid);

    /// <summary>永続GUIDを新しく発行し直す。</summary>
    void RegeneratePersistentGuid();

    /// <summary>内蔵コンポーネントの型と現在の有無を一覧にする。</summary>
    std::vector<BuiltInComponentInfo> GetBuiltInComponentInfos() const;

    /// <summary>指定型の内蔵コンポーネントがあるか調べる。</summary>
    bool HasBuiltInComponent(std::string_view typeId) const;

    /// <summary>指定型の内蔵コンポーネントを探す。未登録ならnullptr。</summary>
    void* FindBuiltInComponent(std::string_view typeId);

    /// <summary>指定型の内蔵コンポーネントを読み取り専用で探す。</summary>
    const void* FindBuiltInComponent(std::string_view typeId) const;

    // デバッグリプレイ専用の安定IDと実行状態を扱います。
    uint64_t EnsureReplayId();
    uint64_t GetReplayId() const { return replayId_; }

    /// <summary>巻き戻しに必要な実行状態を記録する。</summary>
    ReplayState CaptureReplayState() const;

    /// <summary>記録済みの実行状態を復元する。</summary>
    void RestoreReplayState(const ReplayState& state);
    void SetReplayRetained(bool retained) { replayRetained_ = retained; }
    bool IsReplayRetained() const { return replayRetained_; }
    void SetReplayRemoved(bool removed) { replayRemoved_ = removed; }
    bool IsReplayRemoved() const { return replayRemoved_; }

    // トランスフォーム
    /// <summary>ローカルの拡縮、回転、移動から変換行列を更新する。</summary>
    void UpdateLocalMatrix();

    // 階層のワールド行列を更新します。ゲームの一括更新中はfalseを指定し、
    // 全オブジェクトのロジック更新後に描画定数を一度だけ確定できます。
    void UpdateWorldMatrix(bool refreshRenderer = true);

    /// <summary>現在のカメラに合わせて描画定数だけを更新する。</summary>
    void RefreshRenderCameraData();

    const Transform* GetTransform() { return &transform_; }
    const Transform& GetTransform() const { return transform_; }

    /// <summary>現在のワールド行列から位置を取り出す。</summary>
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
    void SetTranslateX(float x) { transform_.translate.x = x; }
    void SetTranslateY(float y) { transform_.translate.y = y; }
    void SetTranslateZ(float z) { transform_.translate.z = z; }
    void SetScale(const Vector3& scale) { transform_.scale = scale; }

    /// <summary>表示用のEuler角を更新する。回転モードは変更しない。</summary>
    void SetEulerAngles(const Vector3& rotate) { transform_.rotate = rotate; }
    void SetEulerX(float x) { transform_.rotate.x = x; }
    void SetEulerY(float y) { transform_.rotate.y = y; }
    void SetEulerZ(float z) { transform_.rotate.z = z; }

    /// <summary>Quaternionを設定し、次の行列更新で優先する。</summary>
    void SetQuaternion(const Quaternion& quaternion) { transform_.quaternion = quaternion; transform_.isQuaternionMaster = true; }

    /// <summary>falseを指定すると次の行列更新でEuler角からQuaternionを作る。</summary>
    void SetQuaternionMaster(bool isQuaternionMaster) { transform_.isQuaternionMaster = isQuaternionMaster; }

    /// <summary>位置・回転・拡縮を一括設定する。行列と親はコピーしない。</summary>
    void SetLocalTransform(const Transform& transform) {
        transform_.translate = transform.translate;
        transform_.rotate = transform.rotate;
        transform_.scale = transform.scale;
        transform_.quaternion = transform.quaternion;
        transform_.isQuaternionMaster = transform.isQuaternionMaster;
    }

    /// <summary>オブジェクト階層に属さない外部アンカーを親行列として参照する。</summary>
    void SetTransformAnchor(const Transform* anchor) { transform_.parent = anchor; }

    /// <summary>Euler角を設定し、回転の主表現をEuler角へ切り替える。</summary>
    void SetRotation(const Vector3& rotate) { transform_.rotate = rotate; transform_.isQuaternionMaster = false;
    }

    /// <summary>Euler角のY成分を設定し、回転の主表現をEuler角へ切り替える。</summary>
    void SetRotationY(float y) { transform_.rotate.y = y; transform_.isQuaternionMaster = false;
    }

    /// <summary>親オブジェクトを設定する。keepWorldTransformが真なら現在の世界姿勢を維持する。</summary>
    void SetParent(Object3d* parent, bool keepWorldTransform = false);
    Object3d* GetParent() const { return parent_; }

    // 以下のモデル、LOD、マテリアル操作はMeshRendererへ委譲する。
    /// <summary>描画マテリアルやLODを扱うレンダラーを返す。</summary>
    MeshRenderer* GetMeshRenderer() const { return meshRenderer_.get(); }

    /// <summary>既存のモデルを描画対象に設定する。</summary>
    void SetModel(Model* model);

    /// <summary>モデル名から描画対象を探して設定する。</summary>
    void SetModel(const std::string& modelName);

    /// <summary>現在描画しているモデルを返す。</summary>
    Model* GetModel() const;

    /// <summary>現在のモデル名を返す。</summary>
    std::string GetModelName() const;

    /// <summary>描画対象のメッシュ番号を設定する。-1は全メッシュ。</summary>
    void SetMeshDrawIndex(int meshIndex);

    /// <summary>描画対象のメッシュ番号を返す。</summary>
    int GetMeshDrawIndex() const;

    /// <summary>特定メッシュだけを描画する設定か返す。</summary>
    bool IsMeshDrawFiltered() const;

    /// <summary>カメラ距離に応じたLOD切り替えを有効化する。</summary>
    void SetLodEnabled(bool enabled);

    /// <summary>LOD切り替えが有効か返す。</summary>
    bool IsLodEnabled() const;

    /// <summary>切り替え候補のLODが登録されているか返す。</summary>
    bool HasLodLevels() const;

    /// <summary>登録済みのLOD段階を参照する。</summary>
    const std::vector<LodLevel>& GetLodLevels() const;

    /// <summary>カメラ距離ごとのLOD段階を設定する。</summary>
    void SetLodLevels(const std::vector<LodLevel>& levels);

    /// <summary>登録済みのLOD段階を削除する。</summary>
    void ClearLodLevels();

    /// <summary>指定したLOD段階への切替距離を変更する。</summary>
    bool SetLodLevelDistance(int level, float distance);

    /// <summary>現在のモデルのLOD定義を読み直す。</summary>
    bool ReloadLodManifest();

    /// <summary>現在選ばれているLOD段階を返す。</summary>
    int GetActiveLodLevel() const;

    /// <summary>現在描画しているLODモデル名を返す。</summary>
    std::string GetActiveLodModelName() const;

    /// <summary>オブジェクトからカメラまでのLOD判定距離を返す。</summary>
    float GetLodCameraDistance() const;

    /// <summary>描画マテリアルの色を返す。</summary>
    Vector4 GetColor() const;

    /// <summary>描画マテリアルの色を設定する。</summary>
    void SetColor(const Vector4& color);

    /// <summary>描画時のブレンドモードを設定する。</summary>
    void SetBlendMode(BlendMode blendMode);

    /// <summary>現在のブレンドモードを返す。</summary>
    BlendMode GetBlendMode() const;

    // 共有Material Instanceを適用し、Scene再読込時にも同じAssetへ追従させます。
    bool ApplyMaterialInstance(const std::string& assetPath, std::string* errorMessage = nullptr);

    /// <summary>現在のマテリアル設定を共有Material Instanceとして保存する。</summary>
    bool SaveMaterialInstance(const std::string& assetPath, std::string* errorMessage = nullptr);

    /// <summary>マテリアルインスタンスとの参照を解除する。</summary>
    void ClearMaterialInstanceLink() { materialInstancePath_.clear(); }
    const std::string& GetMaterialInstancePath() const { return materialInstancePath_; }

    /// <summary>デカールの投影範囲と再生時間を設定する。</summary>
    void SetDecalSettings(const DecalSettings& settings);
    const DecalSettings& GetDecalSettings() const { return decalSettings_; }
    bool IsDecal() const { return decalSettings_.enabled || className_ == "Decal"; }

    /// <summary>デカールの経過時間を戻して再生し直す。</summary>
    void RestartDecalPlayback();
    float GetDecalElapsedTime() const { return decalElapsedTime_; }

    /// <summary>平行光源の強度を設定する。</summary>
    void SetIntensity(float intensity);

    /// <summary>平行光源の強度を返す。</summary>
    float GetIntensity() const;

    // データアクセッサ (互換性維持)
    DirectionalLight* GetDirectionalLightData();

    /// <summary>描画マテリアルの編集可能なデータを取得する。</summary>
    Material* GetMaterialData();

    /// <summary>特殊マテリアルの種類を設定する。</summary>
    void SetMaterialType(int32_t type);

    /// <summary>鏡面反射の強さを設定する。</summary>
    void SetShininess(float shininess);

    /// <summary>現在のマテリアル種類を返す。</summary>
    int32_t GetMaterialType() const;

    /// <summary>適用するライティング方式を設定する。</summary>
    void SetSelectedLighting(int32_t type);

    void SetName(const std::string& name) { name_ = name; }
    const std::string& GetName() const { return name_; }
    const PrefabInstanceInfo& GetPrefabInstanceInfo() const { return prefabInstanceInfo_; }
    void SetPrefabInstanceInfo(const PrefabInstanceInfo& info) { prefabInstanceInfo_ = info; }
    void ClearPrefabInstanceInfo() { prefabInstanceInfo_ = PrefabInstanceInfo{}; }
    bool IsPrefabInstance() const { return prefabInstanceInfo_.IsLinked(); }

    // 衝突判定 (Colliderへ委譲)
    Collider* GetCollider() const { return collider_.get(); }

    /// <summary>衝突形状と判定設定をまとめて変更する。</summary>
    void SetColliderConfig(const ColliderConfig& config);

    /// <summary>現在の衝突判定設定を返す。</summary>
    const ColliderConfig& GetColliderConfig() const;

    /// <summary>衝突形状の種類を設定する。</summary>
    void SetColliderType(ColliderType type);

    /// <summary>衝突形状の種類を返す。</summary>
    ColliderType GetColliderType() const;

    /// <summary>箱型衝突形状の大きさを設定する。</summary>
    void SetCollisionSize(const Vector3& size);

    /// <summary>衝突形状の大きさを返す。</summary>
    Vector3 GetCollisionSize() const;

    /// <summary>球型衝突形状の半径を設定する。</summary>
    void SetCollisionRadius(float radius);

    /// <summary>衝突形状の半径を返す。</summary>
    float GetCollisionRadius() const;

    /// <summary>自身が属する衝突カテゴリのビットを設定する。</summary>
    void SetCollisionAttribute(uint32_t attribute);

    /// <summary>自身が属する衝突カテゴリのビットを返す。</summary>
    uint32_t GetCollisionAttribute() const;

    /// <summary>判定対象となる衝突カテゴリのビットを設定する。</summary>
    void SetCollisionMask(uint32_t mask);

    /// <summary>判定対象となる衝突カテゴリのビットを返す。</summary>
    uint32_t GetCollisionMask() const;

    /// <summary>地形の衝突データをファイルから読み込む。</summary>
    bool LoadTerrainCollisionFromFile(const std::string& path);

    /// <summary>地形の衝突データと参照元パスを設定する。</summary>
    void SetTerrainCollisionData(const TerrainCollisionData& data, const std::string& path = "");

    /// <summary>地形の衝突データを返す。未設定ならnullptr。</summary>
    const TerrainCollisionData* GetTerrainCollisionData() const;
    void SetTerrainCollisionPath(const std::string& path) { terrainCollisionPath_ = path; }
    const std::string& GetTerrainCollisionPath() const { return terrainCollisionPath_; }
    void SetMetallic(float metallic) { if (meshRenderer_) meshRenderer_->SetMetallic(metallic); }
    float GetMetallic() const { return meshRenderer_ ? meshRenderer_->GetMetallic() : 0.0f; }
    void SetRoughness(float roughness) { if (meshRenderer_) meshRenderer_->SetRoughness(roughness); }
    float GetRoughness() const { return meshRenderer_ ? meshRenderer_->GetRoughness() : 0.3f; }

    /// <summary>衝突形状のワールド空間AABBを返す。</summary>
    AABB GetAABB() const;

    /// <summary>衝突形状のワールド空間OBBを返す。</summary>
    OBB GetOBB() const;

    /// <summary>モデル形状からワールド空間のAABBを求める。</summary>
    AABB GetModelWorldAABB() const;

    /// <summary>指定オブジェクトとの衝突状態を調べる。</summary>
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

    // Editorの隔離表示はScene保存値と分離し、複数の一時非表示理由を合成します。
    void SetEditorHiddenReason(std::uint32_t reasonMask, bool hidden) {
        if (hidden) {
            editorHiddenReasonMask_ |= reasonMask;
        }
        else {
            editorHiddenReasonMask_ &= ~reasonMask;
        }
    }
    void ClearEditorHiddenReasons() { editorHiddenReasonMask_ = 0; }
    bool IsEditorTemporarilyHidden() const { return editorHiddenReasonMask_ != 0; }
    bool GetIsRenderVisible() const { return isVisible_ && !IsEditorTemporarilyHidden(); }
    void SetCastShadow(bool enabled) { castShadow_ = enabled; }
    bool GetCastShadow() const { return castShadow_; }

    /// <summary>ゲームイベント連携コンポーネントを取得し、なければ追加する。</summary>
    GameplayLinkComponent* EnsureGameplayLinkComponent();

    /// <summary>ゲームイベント連携コンポーネントを取り外す。</summary>
    bool RemoveGameplayLinkComponent();
    bool HasGameplayLinkComponent() const { return gameplayLinkComponent_.has_value(); }

    /// <summary>ゲームイベント連携コンポーネントを返す。なければnullptr。</summary>
    GameplayLinkComponent* GetGameplayLinkComponent();

    /// <summary>ゲームイベント連携コンポーネントを読み取り専用で返す。</summary>
    const GameplayLinkComponent* GetGameplayLinkComponent() const;

    /// <summary>ナビゲーションコンポーネントを取得し、なければ追加する。</summary>
    NavAgentComponent* EnsureNavAgentComponent();

    /// <summary>ナビゲーションコンポーネントを取り外す。</summary>
    bool RemoveNavAgentComponent();
    bool HasNavAgentComponent() const { return navAgentComponent_.has_value(); }

    /// <summary>ナビゲーションコンポーネントを返す。なければnullptr。</summary>
    NavAgentComponent* GetNavAgentComponent();

    /// <summary>ナビゲーションコンポーネントを読み取り専用で返す。</summary>
    const NavAgentComponent* GetNavAgentComponent() const;

    /// <summary>発行するゲームイベントのIDを設定する。</summary>
    void SetEventID(int id);

    /// <summary>発行するゲームイベントのIDを返す。</summary>
    int GetEventID() const;

    /// <summary>イベントの接続先IDを設定する。</summary>
    void SetTargetID(int id);

    /// <summary>イベントの接続先IDを返す。</summary>
    int GetTargetID() const;

    /// <summary>パーティクル発生器を取得し、なければ追加する。</summary>
    ParticleEmitterComponent* EnsureParticleEmitterComponent();

    /// <summary>パーティクル発生器を取り外す。</summary>
    bool RemoveParticleEmitterComponent();
    bool HasParticleEmitterComponent() const { return particleEmitterComponent_.has_value(); }

    /// <summary>パーティクル発生器を返す。なければnullptr。</summary>
    ParticleEmitterComponent* GetParticleEmitterComponent();

    /// <summary>パーティクル発生器を読み取り専用で返す。</summary>
    const ParticleEmitterComponent* GetParticleEmitterComponent() const;

    /// <summary>通常パーティクルの設定名を指定する。</summary>
    void SetParticleName(const std::string& name);

    /// <summary>通常パーティクルの設定名を返す。</summary>
    const std::string& GetParticleName() const;

    /// <summary>GPUパーティクルの設定名を指定する。</summary>
    void SetGPUParticleName(const std::string& name);

    /// <summary>GPUパーティクルの設定名を返す。</summary>
    const std::string& GetGPUParticleName() const;

    /// <summary>メッシュエフェクトを取得し、なければ追加する。</summary>
    MeshEffectComponent* EnsureMeshEffectComponent();

    /// <summary>メッシュエフェクトを取り外す。</summary>
    bool RemoveMeshEffectComponent();
    bool HasMeshEffectComponent() const { return meshEffectComponent_.has_value(); }

    /// <summary>メッシュエフェクトを返す。なければnullptr。</summary>
    MeshEffectComponent* GetMeshEffectComponent();

    /// <summary>メッシュエフェクトを読み取り専用で返す。</summary>
    const MeshEffectComponent* GetMeshEffectComponent() const;

    /// <summary>第一メッシュエフェクトの設定名を指定する。</summary>
    void SetMeshEffect1Name(const std::string& name);

    /// <summary>第一メッシュエフェクトの設定名を返す。</summary>
    const std::string& GetMeshEffect1Name() const;

    /// <summary>第二メッシュエフェクトの設定名を指定する。</summary>
    void SetMeshEffect2Name(const std::string& name);

    /// <summary>第二メッシュエフェクトの設定名を返す。</summary>
    const std::string& GetMeshEffect2Name() const;

    /// <summary>経路移動コンポーネントを取得し、なければ追加する。</summary>
    PathMoverComponent* EnsurePathMoverComponent();

    /// <summary>経路移動コンポーネントを取り外す。</summary>
    bool RemovePathMoverComponent();
    bool HasPathMoverComponent() const { return pathMoverComponent_.has_value(); }

    /// <summary>経路移動コンポーネントを返す。なければnullptr。</summary>
    PathMoverComponent* GetPathMoverComponent();

    /// <summary>経路移動コンポーネントを読み取り専用で返す。</summary>
    const PathMoverComponent* GetPathMoverComponent() const;

    /// <summary>記録済み経路の名前を設定する。</summary>
    void SetRecordPathName(const std::string& name);

    /// <summary>記録済み経路の名前を返す。</summary>
    const std::string& GetRecordPathName() const;

    /// <summary>経路のループ再生を切り替える。</summary>
    void SetRecordLoop(bool loop);

    /// <summary>経路のループ再生が有効か返す。</summary>
    bool IsRecordLoop() const;

    /// <summary>経路座標をオブジェクト相対として扱うか設定する。</summary>
    void SetRecordRelative(bool relative);

    /// <summary>経路座標がオブジェクト相対か返す。</summary>
    bool IsRecordRelative() const;

    /// <summary>接続されたパーティクルとメッシュエフェクトを更新する。</summary>
    void UpdateAttachedEffects(float deltaTime);

    /// <summary>接続されたエフェクトを現在の光源で描画する。</summary>
    void DrawAttachedEffects(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    /// <summary>取得時のアニメーションを先頭から開始する。</summary>
    void StartCollectionAnimation() { isCollecting_ = true; collectTimer_ = 0.0f; }
    bool IsCollecting() const { return isCollecting_; }

    // Animator Controllerは状態、条件、補間を管理し、Model Animationを滑らかに切り替えます。
    using AnimatorEventCallback = std::function<void(const AnimatorEventInstance&)>;

    /// <summary>アニメーター定義を読み込み、状態遷移を有効にする。</summary>
    bool SetAnimatorController(const std::string& assetPath);

    /// <summary>アニメーター定義と実行状態を解除する。</summary>
    void ClearAnimatorController();
    const std::string& GetAnimatorControllerPath() const { return animatorControllerPath_; }
    bool HasAnimatorController() const { return animatorControllerLoaded_; }
    AnimatorControllerAsset* GetAnimatorControllerAsset() { return animatorControllerLoaded_ ? &animatorControllerAsset_ : nullptr; }
    AnimatorControllerRuntime* GetAnimatorControllerRuntime() { return animatorControllerLoaded_ ? &animatorControllerRuntime_ : nullptr; }
    const AnimatorControllerRuntime* GetAnimatorControllerRuntime() const { return animatorControllerLoaded_ ? &animatorControllerRuntime_ : nullptr; }

    /// <summary>指定状態へ遷移する。負の補間時間・イージング値なら定義側の設定を使う。</summary>
    bool PlayAnimatorState(const std::string& stateName, float blendDuration = -1.0f, int easing = -1);
    bool EvaluateAnimatorState(
        const std::string& stateName,
        float timeSeconds,
        bool loop,
        float blendDuration,
        int easing,
        bool exactPreview);

    /// <summary>現在再生中のアニメーター状態名を返す。</summary>
    std::string GetAnimatorCurrentStateName() const;
    void SetAnimatorEventCallback(const AnimatorEventCallback& callback) { animatorEventCallback_ = callback; }

    /// <summary>アニメーターの再生時刻と遷移状態を保存する。</summary>
    AnimatorControllerRuntime::Snapshot CaptureAnimatorSnapshot() const;

    /// <summary>保存したアニメーター状態を復元する。</summary>
    void RestoreAnimatorSnapshot(const AnimatorControllerRuntime::Snapshot& snapshot);
    float GetAnimationTime() const { return animationTime_; }

    /// <summary>モデルアニメーションの名前、再生秒、ループ設定を復元する。</summary>
    void RestoreAnimationPlayback(const std::string& animationName, float timeSeconds, bool loop);

    // 現在のスケルトン姿勢から、指定ジョイントのワールド行列を取得します。
    bool TryGetJointWorldMatrix(const std::string& jointName, Matrix4x4& outMatrix) const;

    /// <summary>指定ジョイントのワールド位置を取得する。見つからなければfalse。</summary>
    bool TryGetJointWorldPosition(const std::string& jointName, Vector3& outPosition) const;

    // ボーンソケットなど、外部で求めたワールド行列をTransformへ反映します。
    void ApplyWorldMatrix(const Matrix4x4& worldMatrix);

    /// <summary>シーン保存に使うオブジェクト設定をJSONへ書き出す。</summary>
    json ExportToJson();

    /// <summary>シーンJSONからオブジェクト設定を読み込む。</summary>
    void ImportFromJson(const json& j);

    /// <summary>実体Componentをcomponents Payloadへ直列化し、未知のデータも保持する。</summary>
    json SerializeFeatureComponents() const;

    /// <summary>components Payloadを優先し、旧Top-Level JSONも読み込む。</summary>
    void DeserializeFeatureComponents(const json& objectData);
    const json& GetOpaqueComponents() const { return opaqueComponents_; }

    /// <summary>未解釈のコンポーネント情報をオブジェクト形式で保持する。</summary>
    void SetOpaqueComponents(const json& components) {
        opaqueComponents_ = components.is_object() ? components : json::object();
    }

    /// <summary>値が未設定でもComponentが追加済みであることを示すMarkerを確認する。</summary>
    bool HasComponentPresenceMarker(const std::string& componentTypeId) const;

    /// <summary>Componentの有無だけを更新し、未知のComponentデータは保持する。</summary>
    void SetComponentPresenceMarker(const std::string& componentTypeId, bool present);

    virtual void OnTrigger() { isVisible_ = false; }
    virtual void OnSwitchEvent(bool active) { if (active) OnTrigger(); }

    /// <summary>シーンに接続したデバッグリプレイ記録を初期化する。</summary>
    void InitializeRecorder(SceneManager* sceneManager);

    /// <summary>別オブジェクトから複製対象の設定をコピーする。</summary>
    void CopyFrom(const Object3d* other);
    void SetEnemyType(const std::string& type) { enemyType_ = type; }
    std::string GetEnemyType() const { return enemyType_; }
    void SetGimmickType(const std::string& type) { gimmickType_ = type; }
    std::string GetGimmickType() const { return gimmickType_; }
    void SetItemType(const std::string& type) { itemType_ = type; }
    std::string GetItemType() const { return itemType_; }
    virtual void OnRecordEvent(int eventID) {}

    // ゲッター・セッター
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

    /// <summary>UIプレビュー向けの描画設定をレンダラーへ伝える。</summary>
    void SetIsUIPreview(bool isPreview) { 
        if (meshRenderer_) meshRenderer_->SetIsUIPreview(isPreview); 
    }

    // 以下の特殊マテリアル描画はMeshRendererへ委譲する。第一引数はシーン深度、
    // 第二引数は画面カラーを参照するSRV番号（旧名grabSrvHandleを含む）。
    /// <summary>水面マテリアルで描画する。</summary>
    void DrawWater(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>マグママテリアルで描画する。</summary>
    void DrawMagma(uint32_t depthSrvHandle, uint32_t colorSrvHandle);

    /// <summary>氷マテリアルで描画する。</summary>
    void DrawIce(uint32_t depthSrvHandle, uint32_t colorSrvHandle);  

    /// <summary>炎マテリアルで描画する。</summary>
    void DrawFire(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>レーザーマテリアルで描画する。</summary>
    void DrawLaser(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>スライムゲルの特殊マテリアルで描画する。</summary>
    void DrawSlimeGel(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>衝撃波の特殊マテリアルで描画する。</summary>
    void DrawShockwave(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>液体接触の特殊マテリアルで描画する。</summary>
    void DrawLiquidContact(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>ダメージ亀裂の特殊マテリアルで描画する。</summary>
    void DrawDamageCrack(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>上昇気流の特殊マテリアルで描画する。</summary>
    void DrawUpdraft(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>スタン拘束の特殊マテリアルで描画する。</summary>
    void DrawStunBind(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>王冠解放の特殊マテリアルで描画する。</summary>
    void DrawCrownUnlock(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>毒胞子の特殊マテリアルで描画する。</summary>
    void DrawPoisonSpore(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>雲の特殊マテリアルで描画する。</summary>
    void DrawCloud(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>ゲートポータルの特殊マテリアルで描画する。</summary>
    void DrawGatePortal(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>風の球の特殊マテリアルで描画する。</summary>
    void DrawWindOrb(uint32_t depthSrvHandle, uint32_t grabSrvHandle);
    virtual bool HasOwnedSpecialMaterialVisuals() const { return false; }

    /// <summary>派生クラスが所有する特殊マテリアル演出を描画する拡張点。</summary>
    virtual void DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
        (void)depthSrvHandle;
        (void)grabSrvHandle;
    }
    virtual void DrawOwnedSpecialMaterialVisualsForCamera(
        Camera* camera,
        uint32_t depthSrvHandle,
        uint32_t grabSrvHandle,
        int previewBufferIndex = 0) {
        (void)camera;
        (void)depthSrvHandle;
        (void)grabSrvHandle;
        (void)previewBufferIndex;
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
    /// <summary>派生クラス固有の実行状態をリプレイ用JSONへ書き込む。</summary>
    virtual void CaptureReplayCustomState(json& state) const;

    /// <summary>派生クラス固有の実行状態をリプレイ用JSONから戻す。</summary>
    virtual void RestoreReplayCustomState(const json& state);

    // 派生オブジェクトの生成時に同じ描画環境を渡すための参照。
    Object3dCommon* common_ = nullptr;

    /// <summary>派生クラスがアニメーションを先頭から再生するときに使用する。</summary>
    void ResetAnimationTime() { animationTime_ = 0.0f; }

    /// <summary>派生クラスの複製で、行列と外部アンカーを含む姿勢を引き継ぐ。</summary>
    void CopyTransformStateFrom(const Object3d& source) { transform_ = source.transform_; }

private:
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
    std::unordered_map<std::string, BodyAnimationClip> animatorBodyClips_;
    AnimatorEventCallback animatorEventCallback_;
    bool animatorControllerLoaded_ = false;
    bool animatorBodyPoseApplied_ = false;

    // Animator Controllerの状態進行と、得られた姿勢のモデル反映は別段階で行う。
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
    std::uint32_t editorHiddenReasonMask_ = 0;
    bool castShadow_ = true;
    PrefabInstanceInfo prefabInstanceInfo_;
    std::string enemyType_ = "";
    std::string gimmickType_ = "";
    std::string itemType_ = "";

    std::string materialInstancePath_;
    DecalSettings decalSettings_;
    float decalElapsedTime_ = 0.0f;
    float decalAuthoredAlpha_ = 1.0f;
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
