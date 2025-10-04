#pragma once
#include "Math.h"
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include"ModelData.h"

class DirectXCommon;
class Object3dCommon;


class Object3d {
public:
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 更新
    /// </summary>
    void Update(const class DebugCamera& camera);

    /// <summary>
    /// 描画
    /// </summary>
    void Draw();

    /// <summary>
    /// モデルのセット
    /// </summary>
    void SetModel(const ModelData* modelData);

    // --- 静的メンバ ---
    static void SetCommon(Object3dCommon* common) { common_ = common; }

public: // パラメータ設定用
    Transform transform; // 位置、回転、拡縮

private:
    // DirectX基盤
    DirectXCommon* dxCommon_ = nullptr;
    // 3Dオブジェクト共通描画設定
    static Object3dCommon* common_;

    // モデルデータ
    const ModelData* modelData_ = nullptr;
    // テクスチャハンドル
    uint32_t textureHandle_ = 0;

    // 頂点バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // WVP行列用リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_ = nullptr;
    TransformationMatrix* wvpData_ = nullptr;

    // マテリアル用リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_ = nullptr;
    Material* materialDataForGPU_ = nullptr;
};