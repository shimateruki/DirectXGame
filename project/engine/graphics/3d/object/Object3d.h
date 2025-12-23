#pragma once
#include "engine/utility/math/Math.h"
#include "Object3dCommon.h"
#include "Model.h"
#include "CollisionConfig.h" 
#include <wrl.h>
#include <string>

class Object3d {
public:
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

public:
    virtual void Initialize(Object3dCommon* common);
    virtual void Update(float deltaTime);
    virtual void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    /// <summary>
    /// ワールド行列の計算と定数バッファへの転送
    /// </summary>
    void UpdateLocalMatrix();

    void UpdateWorldMatrix();
    void SetColor(const Vector4& color);
    Vector4 GetColor() { return  directionalLightData_->color; }
    const Vector3& GetScale() const { return transform_.scale; }
    /// <summary>
    /// 親オブジェクトを設定する
    /// </summary>
    void SetParent(Object3d* parent);
    /// <summary>
    /// 親オブジェクトを取得する
    /// </summary>
    Object3d* GetParent() const { return parent_; }
    DirectionalLight* GetDirectionalLightData() { return directionalLightData_; }
    /// <summary>
    /// ワールド行列を取得する
    /// </summary>
    const Matrix4x4& GetWorldMatrix() const { return worldMatrix_; }

    OBB GetOBB() const;
    const std::string& GetModelName() const { return modelName_; }
    void SetModel(Model* model) { model_ = model; }
    void SetModel(const std::string& modelName);

    Model* GetModel() const { return model_; }
    void SetTranslate(const Vector3& translate) { transform_.translate = translate; }
    void SetScale(const Vector3& scale) { transform_.scale = scale; }
    void SetRotation(const Vector3& rotate) { transform_.rotate = rotate; }

    // ブレンドモード用のセッターとゲッター
    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }
    BlendMode GetBlendMode() const { return blendMode_; }

    Transform* GetTransform() { return &transform_; }
    const Vector3& GetTranslate() { return transform_.translate; }
    /// <summary>
    /// 回転量を取得する
    /// </summary>
    const Vector3& GetRotation() const { return transform_.rotate; }
    Model::Material* GetMaterial() { return model_ ? model_->GetMaterial() : nullptr; }
    DirectionalLight* GetDirectionalLight() { return directionalLightData_; }

 
    // 光の強さをセットする関数
    void SetIntensity(float intensity);
    // 現在の光の強さを取得する関数
    float GetIntensity() const { return directionalLightData_->intensity; }

    // --- 属性フラグ関連 ---
    void SetCollisionAttribute(uint32_t attribute) { collisionAttribute_ = attribute; }
    void SetCollisionMask(uint32_t mask) { collisionMask_ = mask; }
    uint32_t GetCollisionAttribute() const { return collisionAttribute_; }
    uint32_t GetCollisionMask() const { return collisionMask_; }

    // --- 形状設定関連 ---
    void SetColliderType(ColliderType type) { colliderType_ = type; }
    ColliderType GetColliderType() const { return colliderType_; }

    void SetCollisionRadius(float radius) { radius_ = radius; }
    float GetCollisionRadius() const { return radius_; }

    void SetCollisionSize(const Vector3& size) {
        aabbSize_ = size;
        collisionSize_ = size;
    }
    const Vector3& GetCollisionSize() const { return aabbSize_; }
    // const版も用意（読み取り専用）
    const Transform& GetTransform() const { return transform_; }
    /// <summary>
    /// Y軸の回転を設定する
    /// </summary>
    void SetRotationY(float y) { transform_.rotate.y = y; }

    // オブジェクトの識別名を設定
    void SetName(const std::string& name) { name_ = name; }
    // オブジェクトの識別名を取得
    const std::string& GetName() const { return name_; }

    // --- ゲッター ---
    const Vector3& GetWorldPosition() const { return transform_.translate; }


    AABB GetAABB() const {
        Vector3 center = transform_.translate;
        return {
            {center.x - aabbSize_.x, center.y - aabbSize_.y, center.z - aabbSize_.z},
            {center.x + aabbSize_.x, center.y + aabbSize_.y, center.z + aabbSize_.z}
        };
    }


    /// <summary>
    /// 別のObject3dとの精密な衝突判定を実行する
    /// </summary>
    /// <param name="other">衝突相手</param>
    /// <returns>衝突情報 (isColliding が true なら衝突)</returns>
    CollisionInfo CheckCollision(Object3d* other);

    /// <summary>
    /// このオブジェクトのコピーを作成する (仮想)
    /// </summary>
    /// <returns>作成されたオブジェクトの unique_ptr</returns>
    virtual std::unique_ptr<Object3d> Clone() const;


    /// <summary>
    /// 衝突時に呼び出される仮想関数 
    /// </summary>
    virtual  bool OnCollision(Object3d* other) {
        (void)other; // 未使用引数の警告避け
        return false;
    }

    /// <summary>
        /// このオブジェクトを静的（動かない）として設定する
        /// </summary>
    void SetStatic(bool isStatic) { isStatic_ = isStatic; }

    /// <summary>
    /// このオブジェクトが静的かを取得する
    /// </summary>
    bool IsStatic() const { return isStatic_; }

protected:
    Object3dCommon* common_ = nullptr;
    Model* model_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    CameraForGPU* cameraData_ = nullptr;

    Transform transform_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    BlendMode blendMode_ = BlendMode::kNormal;
    Matrix4x4 localMatrix_ = {}; // ローカル行列
    Matrix4x4 worldMatrix_ = {}; // 親の影響を含めたワールド行列
    Object3d* parent_ = nullptr;  // 親オブジェクトへのポインタ


    // ▼▼▼ あたり判定用のメンバ変数 ▼▼▼
    ColliderType colliderType_ = ColliderType::kNone;
    uint32_t collisionAttribute_ = 0;
    uint32_t collisionMask_ = 0xFFFFFFFF;
    float radius_ = 1.0f;
    Vector3 aabbSize_ = { 1.0f, 1.0f, 1.0f };
    std::string name_ = "Object";
    std::string modelName_;
    Vector3 collisionSize_;

    bool isStatic_ = false;

};