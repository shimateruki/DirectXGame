#pragma once
#include "engine/base/Math.h"
#include "engine/3d/Object3dCommon.h"
#include "engine/3d/Model.h"
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
    };
    struct DirectionalLight {
        Vector4 color;
        Vector3 direction;
        float intensity;
    };

public:
    void Initialize(Object3dCommon* common);
    void Update();
    void Draw();

    void SetModel(Model* model) { model_ = model; }
    void SetModel(const std::string& filePath);
    void SetTranslate(const Vector3& translate) { transform_.translate = translate; }

    // ブレンドモード用のセッターとゲッター
    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }
    BlendMode GetBlendMode() const { return blendMode_; }

    Transform* GetTransform() { return &transform_; }
    Model::Material* GetMaterial() { return model_ ? model_->GetMaterial() : nullptr; }
    DirectionalLight* GetDirectionalLight() { return directionalLightData_; }

private:
    Object3dCommon* common_ = nullptr;
    Model* model_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;
    Transform transform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    BlendMode blendMode_ = BlendMode::kNormal;
};