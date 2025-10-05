#pragma once

#include "Math.h"
#include "Object3dCommon.h"
#include "Model.h" // Modelクラスをインクルード
#include <wrl.h>

/// <summary>
/// 個々の3Dオブジェクトを表すクラス
/// </summary>
class Object3d {
public: // メンバクラス（構造体）
    struct Transform
    {
        Vector3 scale;
        Vector3 rotate;
        Vector3 translate;
    };

    // 座標変換行列定数バッファ用構造体
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 world;
    };

    // 平行光源定数バッファ用構造体
    struct DirectionalLight {
        Vector4 color;   // ライトの色
        Vector3 direction; // ライトの向き
        float intensity; // 輝度
    };

public: // メンバ関数
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(Object3dCommon* common);

    /// <summary>
    /// 更新
    /// </summary>
    void Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix);

    /// <summary>
    /// 描画
    /// </summary>
    void Draw(ID3D12GraphicsCommandList* commandList);

    /// <summary>
    /// モデルをセット
    /// </summary>
    void SetModel(Model* model) { model_ = model; }

    /// <summary>
    /// トランスフォーム情報の取得
    /// </summary>
    Transform* GetTransform() { return &transform_; }

    /// <summary>
    /// マテリアル情報の取得 (ImGuiでの操作用)
    /// </summary>
    Model::Material* GetMaterial() { return model_ ? model_->GetMaterial() : nullptr; }

    /// <summary>
    /// 平行光源情報の取得 (ImGuiでの操作用)
    /// </summary>
    DirectionalLight* GetDirectionalLight() { return directionalLightData_; }

private: // メンバ変数
    // 共通部品へのポインタ
    Object3dCommon* common_ = nullptr;
    // モデルへのポインタ
    Model* model_ = nullptr;

    // 座標変換行列定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;

    // 平行光源定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    // ローカル座標
    Transform transform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
};