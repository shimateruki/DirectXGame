#pragma once
#include "engine/utility/math/Math.h"
#include "Object3dCommon.h"
#include "Model.h"
#include "CollisionConfig.h" 
#include <wrl.h>
#include <string>
#include <memory>
#include <optional>
#include "Event.h"
#include <vector>
#include "Transform.h" 
#include "json.hpp"
using json = nlohmann::json;

class GhostRecorder;
class SceneManager;

class Object3d {
public:
    // ========================================================================
    // 定数バッファ用構造体定義
    // ========================================================================

    // 座標変換行列 (GPU転送用)
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 world;
        Matrix4x4 WorldInverseTranspose;
    };

    // 平行光源
    struct DirectionalLight {
        Vector4 color;
        Vector3 direction;
        float intensity;
    };

    // カメラ座標
    struct CameraForGPU {
        Vector3 worldPosition;
    };

    // ポイントライト
    struct PointLight {
        Vector4 color;
        Vector3 position;
        float intensity;
        float radius;
        float decay;
        float padding[2];
    };

    // スポットライト
    struct SpotLight {
        Vector4 color;
        Vector3 position;
        float intensity;
        Vector3 direction;
        float distance;
        float decay;
        float cosAngle;
        float cosFalloffStart;
        float padding[1];
    };

    // マテリアル設定
    struct Material {
        Vector4 color;
        int32_t enableLighting;
        float padding1[3];
        Matrix4x4 uvTransform;
        int32_t selectedLighting;
        float shininess;
        int32_t materialType; // 0:通常, 1:ガラス
        float padding2;
    };

    // 衝突判定の設定データ
    struct ColliderConfig {
        ColliderType type = ColliderType::kAABB;
        Vector3 center = { 0.0f, 0.0f, 0.0f };
        Vector3 size = { 1.0f, 1.0f, 1.0f };
        Vector3 rotation = { 0.0f, 0.0f, 0.0f };
    };

    // ゲーム内パラメータ（HPや速度など）
    struct EntityParameter {
        float hp = 100.0f;
        float maxHp = 100.0f;
        float speed = 1.0f;
        float gravity = 50.0f;
        float maxFallSpeed = 60.0f;
        float jumpPower = 10.0f;
        std::string enemyType = "Goblin";
        float interval = 3.0f;
        int maxCount = 5;
        EntityParameter() = default;
    };

public:
    virtual ~Object3d();

    // ========================================================================
    // 基本処理 (ライフサイクル)
    // ========================================================================

    // 初期化処理。リソースの確保や変数の初期化を行う
    virtual void Initialize(Object3dCommon* common);

    // 更新処理。行列の計算やアニメーションの更新を行う
    virtual void Update(float deltaTime);

    // 描画処理。コマンドリストへの積込を行う
    virtual void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    // オブジェクトの複製を作成する
    virtual std::unique_ptr<Object3d> Clone() const;

    // ========================================================================
    // トランスフォーム操作
    // ========================================================================

    // ローカル行列の再計算 (Scale * Rotate * Translate)
    void UpdateLocalMatrix();

    // ワールド行列の再計算 (親行列との合成) と GPUへの転送
    void UpdateWorldMatrix();

    // Transformへのアクセッサ
    Transform* GetTransform() { return &transform_; }
    const Transform& GetTransform() const { return transform_; }

    // 各座標情報への簡易アクセッサ
    // 行列計算結果は Transform 構造体が保持しているものを使用する
    const Vector3& GetWorldPosition() const { return transform_.translate; }
    const Matrix4x4& GetWorldMatrix() const { return transform_.matWorld; }
    const Vector3& GetScale() const { return transform_.scale; }
    const Vector3& GetRotation() const { return transform_.rotate; }
    const Vector3& GetTranslate() const { return transform_.translate; }

    // 値の設定
    void SetTranslate(const Vector3& translate) { transform_.translate = translate; }
    void SetScale(const Vector3& scale) { transform_.scale = scale; }
    void SetRotation(const Vector3& rotate) { transform_.rotate = rotate; }
    void SetRotationY(float y) { transform_.rotate.y = y; }

    // 親子関係の設定
    void SetParent(Object3d* parent);
    Object3d* GetParent() const { return parent_; }

    // ========================================================================
    // グラフィックス設定
    // ========================================================================

    // 描画するモデルのセット
    void SetModel(Model* model) { model_ = model; }
    void SetModel(const std::string& modelName);
    Model* GetModel() const { return model_; }
    const std::string& GetModelName() const { return modelName_; }

    // マテリアル・ライト設定
    Vector4 GetColor() const { if (materialData_) return materialData_->color; return { 1.0f, 1.0f, 1.0f, 1.0f }; }
    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }
    BlendMode GetBlendMode() const { return blendMode_; }

    void SetIntensity(float intensity);
    float GetIntensity() const { return directionalLightData_->intensity; }
    DirectionalLight* GetDirectionalLightData() { return directionalLightData_; }
    Model::Material* GetMaterial() { return model_ ? model_->GetMaterial() : nullptr; }

    void SetMaterialType(int32_t type);
    void SetColor(const Vector4& color);
    void SetShininess(float shininess);
    int32_t GetMaterialType() const { return materialData_->materialType; }
    void SetSelectedLighting(int32_t type) { materialData_->selectedLighting = type; }

    // オブジェクト名の設定
    void SetName(const std::string& name) { name_ = name; }
    const std::string& GetName() const { return name_; }

    // ========================================================================
    // 衝突判定 (Collision)
    // ========================================================================

    // 判定設定 (Config) のセット
    void SetColliderConfig(const ColliderConfig& config) { colliderConfig_ = config; }
    const ColliderConfig& GetColliderConfig() const { return colliderConfig_; }

    // 個別の設定用ショートカット
    void SetColliderType(ColliderType type) { colliderConfig_.type = type; }
    ColliderType GetColliderType() const { return colliderConfig_.type; }
    void SetCollisionSize(const Vector3& size) { colliderConfig_.size = size; }
    Vector3 GetCollisionSize() const { return colliderConfig_.size; }
    void SetCollisionRadius(float radius) { colliderConfig_.size = { radius, radius, radius }; }

    // 半径の取得 (スケールを加味した最大値)
    float GetCollisionRadius() const {
        float maxScale = (std::max)({ std::abs(transform_.scale.x), std::abs(transform_.scale.y), std::abs(transform_.scale.z) });
        return colliderConfig_.size.x * maxScale;
    }

    // 属性とマスクの設定
    void SetCollisionAttribute(uint32_t attribute) { collisionAttribute_ = attribute; }
    uint32_t GetCollisionAttribute() const { return collisionAttribute_; }
    void SetCollisionMask(uint32_t mask) { collisionMask_ = mask; }
    uint32_t GetCollisionMask() const { return collisionMask_; }

    // 形状データの取得
    AABB GetAABB() const;
    OBB GetOBB() const;

    // 衝突判定の実行
    CollisionInfo CheckCollision(Object3d* other);
    virtual bool OnCollision(Object3d* other) { (void)other; return false; }

    // 静的オブジェクト（動かない壁など）かどうかのフラグ
    void SetStatic(bool isStatic) { isStatic_ = isStatic; }
    bool IsStatic() const { return isStatic_; }

    // ========================================================================
    // ゲームロジック・その他
    // ========================================================================

    // パラメータ
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

    // イベント連携ID
    void SetEventID(int id) { eventID_ = id; }
    int GetEventID() const { return eventID_; }
    void SetTargetID(int id) { targetID_ = id; }
    int GetTargetID() const { return targetID_; }

    // JSON入出力
    json ExportToJson();
    void ImportFromJson(const json& j);

    // ギミック等のトリガー動作
    virtual void OnTrigger() { isVisible_ = false; }

    // ゴーストレコーダーの初期化
    void InitializeRecorder(SceneManager* sceneManager);

    // 他のオブジェクトからデータをコピー
    void CopyFrom(const Object3d* other);

    // 敵タイプの設定
    void SetEnemyType(const std::string& type) { enemyType_ = type; }
    std::string GetEnemyType() const { return enemyType_; }

    // アニメーション制御
    std::string animName_ = "";
    bool isAnimLoop_ = true;
    bool isAnimRelative_ = false;
    GhostRecorder* recorder_ = nullptr;

protected:
    // 基盤ポインタ
    Object3dCommon* common_ = nullptr;
    Model* model_ = nullptr;
    std::string name_ = "Object";
    std::string modelName_;

    // DirectXリソース (GPU転送用)
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    CameraForGPU* cameraData_ = nullptr;

    // トランスフォームデータ
    // 計算結果(matLocal/matWorld)は、この構造体の中に格納される
    Transform transform_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Object3d* parent_ = nullptr;

    // 描画設定
    BlendMode blendMode_ = BlendMode::kNone;
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;
    float animationTime_ = 0.0f;

    // 衝突判定データ
    ColliderConfig colliderConfig_;
    uint32_t collisionAttribute_ = 0;
    uint32_t collisionMask_ = 0xFFFFFFFF;
    bool isStatic_ = false;

    // 階層構造・識別情報
    std::vector<Object3d*> children_;
    std::string className_ = "Model";
    bool isVisible_ = true;
    int eventID_ = -1;
    int targetID_ = -1;
    std::string enemyType_ = "";
};