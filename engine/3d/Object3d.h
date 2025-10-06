#pragma once

#include "engine/base/Math.h"
#include "engine/3d/Object3dCommon.h"
#include "engine/3d/Model.h"
#include <wrl.h>
#include <string> // std::string を使うためにインクルード

class Object3d {
public: // メンバクラス（構造体）
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

public: // メンバ関数
    void Initialize(Object3dCommon* common);
    void Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix);
    void Draw(ID3D12GraphicsCommandList* commandList);

    // --- セッター ---
    void SetModel(Model* model) { model_ = model; }
    // ★★★ ファイルパスでモデルを設定するオーバーロードを追加 ★★★
    void SetModel(const std::string& filePath);

    // ★★★ Transformのセッターを追加 ★★★
    void SetScale(const Vector3& scale) { transform_.scale = scale; }
    void SetRotate(const Vector3& rotate) { transform_.rotate = rotate; }
    void SetTranslate(const Vector3& translate) { transform_.translate = translate; }

    // --- ゲッター ---
    Transform* GetTransform() { return &transform_; }
    Model::Material* GetMaterial() { return model_ ? model_->GetMaterial() : nullptr; }
    DirectionalLight* GetDirectionalLight() { return directionalLightData_; }

    // ★★★ Transformのゲッターを追加 ★★★
    const Vector3& GetScale() const { return transform_.scale; }
    const Vector3& GetRotate() const { return transform_.rotate; }
    const Vector3& GetTranslate() const { return transform_.translate; }


private: // メンバ変数
    Object3dCommon* common_ = nullptr;
    Model* model_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    Transform transform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
};