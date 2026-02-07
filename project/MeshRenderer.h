#pragma once
#include "Object3dCommon.h"
#include "Model.h"
#include "Transform.h"
#include "engine/utility/math/Math.h"
#include <wrl.h>
#include <memory>

class Object3d; // 前方宣言

class MeshRenderer {
public:
    // 定数バッファ用構造体 (Object3dから移植)
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
    struct MaterialData {
        Vector4 color;
        int32_t enableLighting;
        float padding1[3];
        Matrix4x4 uvTransform;
        int32_t selectedLighting;
        float shininess;
        int32_t materialType;
        float padding2;
    };

    struct PointLight {
        Vector4 color;
        Vector3 position;
        float intensity;
        float radius;
        float decay;
        float padding[2];
    };

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

public:
    // コンストラクタ: 描画対象のTransformを受け取る
    MeshRenderer(Transform* transform);
    ~MeshRenderer() = default;

    // 初期化
    void Initialize(Object3dCommon* common);

    // 更新 (WVP行列計算など)
    void Update();

    // 描画
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    // --- アクセッサ (Setters) ---
    void SetModel(Model* model) { model_ = model; }
    void SetModel(const std::string& modelName);
    Model* GetModel() const { return model_; }
    const std::string& GetModelName() const { return modelName_; }

    void SetColor(const Vector4& color);
    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }
    void SetMaterialType(int32_t type);
    void SetIntensity(float intensity);

    // ゲッター
    Vector4 GetColor() const;
    int32_t GetMaterialType() const;
    BlendMode GetBlendMode() const { return blendMode_; }

    // マテリアルやライトデータへの直接アクセス（必要に応じて）
    MaterialData* GetMaterialData() { return materialData_; }
    DirectionalLight* GetLightData() { return directionalLightData_; }

private:
    // 依存オブジェクト
    Object3dCommon* common_ = nullptr;
    Transform* transform_ = nullptr; // 位置情報元

    // モデル
    Model* model_ = nullptr;
    std::string modelName_;

    // 描画設定
    BlendMode blendMode_ = BlendMode::kNone;

    // --- DirectXリソース (Object3dから移動) ---
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    CameraForGPU* cameraData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    MaterialData* materialData_ = nullptr;
};