#pragma once
#include "engine/utility/math/Math.h"
#include "Object3dCommon.h"
#include "Model.h"
#include "CollisionConfig.h" 
#include <wrl.h>
#include <string>
#include <memory>
#include <optional>
#include"Event.h"
#include <vector>

#include "json.hpp"
using json = nlohmann::json;

class GhostRecorder;
class SceneManager;

class Object3d {
public:
    // ========================================================================
    // 構造体定義
    // ========================================================================
    struct Transform {
        Vector3 scale;
        Vector3 rotate;
        Vector3 translate;
    };
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 world;
        Matrix4x4 WorldInverseTranspose;
    };
    struct DirectionalLight {
        Vector4 color;
        Vector3 direction;
        float intensity;
    };
    struct CameraForGPU {
        Vector3 worldPosition;
    };
    struct PointLight {
        Vector4 color;       // ライトの色
        Vector3 position;    // ライトの位置
        float intensity;     // 輝度（強さ）
        float radius;        // ライトの届く最大距離
        float decay;         // 減衰率
        float padding[2];    // パディング
    };
    struct SpotLight {
        Vector4 color;        // 色
        Vector3 position;     // 位置
        float intensity;      // 強さ
        Vector3 direction;    // スポットライトの向いている方向
        float distance;       // 届く距離
        float decay;          // 減衰率
        float cosAngle;       // スポットライトの余弦 (角度)
        float cosFalloffStart;// ボケ足の開始角度
        float padding[1];     // パディング
    };

    // コンフィグ構造体（衝突判定のマスターデータ）
    struct ColliderConfig {
        ColliderType type = ColliderType::kAABB; // デフォルトはAABB
        Vector3 center = { 0.0f, 0.0f, 0.0f };   // モデル中心からのズレ（オフセット）
        Vector3 size = { 1.0f, 1.0f, 1.0f };     // サイズ (AABB:半サイズ, OBB:半サイズ, Sphere:x=半径)
    };

    struct EntityParameter {
        float hp = 100.0f;
        float maxHp = 100.0f;
        float speed = 1.0f;
        float gravity = 50.0f;
        float maxFallSpeed = 60.0f;  // 落下制限もここ
        float jumpPower = 10.0f;
        EntityParameter() = default;
    };



public:
    virtual ~Object3d();
    // ========================================================================
    // ライフサイクル (初期化・更新・描画)
    // ========================================================================
    virtual void Initialize(Object3dCommon* common);
    virtual void Update(float deltaTime);
    virtual void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    /// <summary>
    /// このオブジェクトのコピーを作成する (仮想)
    /// </summary>
    virtual std::unique_ptr<Object3d> Clone() const;

    // ========================================================================
    // トランスフォーム (位置・回転・サイズ・親子関係)
    // ========================================================================

    // 行列更新
    void UpdateLocalMatrix();
    void UpdateWorldMatrix();

    // ゲッター
    Transform* GetTransform() { return &transform_; }
    const Transform& GetTransform() const { return transform_; } // const版
    const Vector3& GetWorldPosition() const { return transform_.translate; }
    const Matrix4x4& GetWorldMatrix() const { return worldMatrix_; }
    const Vector3& GetScale() const { return transform_.scale; }
    const Vector3& GetRotation() const { return transform_.rotate; }
    const Vector3& GetTranslate() const { return transform_.translate; }

    // セッター
    void SetTranslate(const Vector3& translate) { transform_.translate = translate; }
    void SetScale(const Vector3& scale) { transform_.scale = scale; }
    void SetRotation(const Vector3& rotate) { transform_.rotate = rotate; }
    void SetRotationY(float y) { transform_.rotate.y = y; }

    // 親子関係
    void SetParent(Object3d* parent);
    Object3d* GetParent() const { return parent_; }

    // ========================================================================
    // グラフィックス & マテリアル
    // ========================================================================
    void SetModel(Model* model) { model_ = model; }
    void SetModel(const std::string& modelName);
    Model* GetModel() const { return model_; }
    const std::string& GetModelName() const { return modelName_; }

    void SetColor(const Vector4& color);
    Vector4 GetColor() { return  directionalLightData_->color; }

    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }
    BlendMode GetBlendMode() const { return blendMode_; }

    void SetIntensity(float intensity);
    float GetIntensity() const { return directionalLightData_->intensity; }
    DirectionalLight* GetDirectionalLightData() { return directionalLightData_; }
    Model::Material* GetMaterial() { return model_ ? model_->GetMaterial() : nullptr; }

    // ========================================================================
    // 名前・識別
    // ========================================================================
    void SetName(const std::string& name) { name_ = name; }
    const std::string& GetName() const { return name_; }

    // ========================================================================
    // 衝突判定 (Collision) - ここを重点的に修正
    // ========================================================================

    // --- コンフィグへのアクセス (ImGui等はこれを使う) ---
    void SetColliderConfig(const ColliderConfig& config) { colliderConfig_ = config; }
    const ColliderConfig& GetColliderConfig() const { return colliderConfig_; }

    // --- ショートカット関数 (内部で必ずConfigを参照する) ---

    // タイプ設定
    void SetColliderType(ColliderType type) { colliderConfig_.type = type; }
    ColliderType GetColliderType() const { return colliderConfig_.type; }

    // サイズ設定 (AABB/OBB用)
    void SetCollisionSize(const Vector3& size) { colliderConfig_.size = size; }
    Vector3 GetCollisionSize() const { return colliderConfig_.size; }

    // 半径設定 (球用: size.x を半径として扱うルール)
    void SetCollisionRadius(float radius) { colliderConfig_.size = { radius, radius, radius }; }
    float GetCollisionRadius() const { return colliderConfig_.size.x; }

    // --- 属性・マスク ---
    void SetCollisionAttribute(uint32_t attribute) { collisionAttribute_ = attribute; }
    uint32_t GetCollisionAttribute() const { return collisionAttribute_; }

    void SetCollisionMask(uint32_t mask) { collisionMask_ = mask; }
    uint32_t GetCollisionMask() const { return collisionMask_; }

    // --- 形状取得 ---

    // AABB取得 (ConfigのCenterオフセットを加味)
    AABB GetAABB() const {
        Vector3 centerPos = transform_.translate + colliderConfig_.center;
        Vector3 size = colliderConfig_.size;
        return {
            {centerPos.x - size.x, centerPos.y - size.y, centerPos.z - size.z},
            {centerPos.x + size.x, centerPos.y + size.y, centerPos.z + size.z}
        };
    }

    // OBB取得 (実装はcppにある想定)
    OBB GetOBB() const;

    // --- 判定処理 ---
    CollisionInfo CheckCollision(Object3d* other);

    // 衝突コールバック (仮想関数)
    virtual bool OnCollision(Object3d* other) {
        (void)other;
        return false;
    }

    // --- 静的オブジェクト設定 ---
    void SetStatic(bool isStatic) { isStatic_ = isStatic; }
    bool IsStatic() const { return isStatic_; }

    // ステータスデータ（
    std::optional<EntityParameter> param_;

    bool isDead = false;

    EventType eventType_ = EventType::None;

    // アクセッサ（あると便利）
    void SetEventType(EventType type) { eventType_ = type; }
    EventType GetEventType() const { return eventType_; }
    const std::vector<Object3d*>& GetChildren() const { return children_; }

    // クラス名（種類）のセット・ゲット
    void SetClassName(const std::string& name) { className_ = name; }
    std::string GetClassName() const { return className_; }

    // 可視フラグのセット・ゲット
    void SetIsVisible(bool visible) { isVisible_ = visible; }
    bool GetIsVisible() const { return isVisible_; }

    // 受信ID (自分自身のID) の設定・取得
    void SetEventID(int id) { eventID_ = id; }
    int GetEventID() const { return eventID_; }

    // 送信ID (ターゲットのID) の設定・取得
    void SetTargetID(int id) { targetID_ = id; }
    int GetTargetID() const { return targetID_; }

    json ExportToJson();

    // ★追加: JSONオブジェクトから設定を読み込む
    void ImportFromJson(const json& j);

    // ギミック発動時に呼ばれる関数
    virtual void OnTrigger() {
        isVisible_ = false;
    }
    // 初期化時にレコーダーも準備する
    void InitializeRecorder(SceneManager* sceneManager);

    void CopyFrom(const Object3d* other);
    void SetEnemyType(const std::string& type) { enemyType_ = type; }
    std::string GetEnemyType() const { return enemyType_; }

    // アニメーション関連
    std::string animName_ = "";      // 再生するファイル名 (例: "door_open")
    bool isAnimLoop_ = true;        // ループするか？
    bool isAnimRelative_ = false;     // 相対座標か？ 
    GhostRecorder* recorder_ = nullptr;




protected:
    // メンバ変数


    // 基盤
    Object3dCommon* common_ = nullptr;
    Model* model_ = nullptr;
    std::string name_ = "Object";
    std::string modelName_;

    // DirectXリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    CameraForGPU* cameraData_ = nullptr;

    // トランスフォーム
    Transform transform_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Matrix4x4 localMatrix_ = {};
    Matrix4x4 worldMatrix_ = {};
    Object3d* parent_ = nullptr;

    // グラフィックス設定
    BlendMode blendMode_ = BlendMode::kNormal;

    //  衝突判定データ
    ColliderConfig colliderConfig_;

    // 属性などはConfigに含まれないので別途保持
    uint32_t collisionAttribute_ = 0;
    uint32_t collisionMask_ = 0xFFFFFFFF;
    bool isStatic_ = false;

    std::vector<Object3d*> children_;
    std::string className_ = "Model";
    bool isVisible_ = true;
    int eventID_ = -1;  // 受信ID（私は誰か）
    int targetID_ = -1; // 送信ID（誰を動かすか）
    std::string enemyType_ = "";
};