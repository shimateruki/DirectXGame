#pragma once
#include "engine/utility/math/Math.h"
#include "Object3dCommon.h"
#include <wrl.h>
#include <d3d12.h>
#include <stdint.h>

/// <summary>シーン背景のキューブマップ用メッシュと描画処理を管理する。</summary>
class Skybox {
public:
    /// <summary>背景用キューブメッシュの頂点位置。</summary>
    struct VertexData {
        engine::Vector4 position;
    };

    /// <summary>共通描画機能とキューブマップテクスチャで初期化する。</summary>
    void Initialize(Object3dCommon* common, uint32_t textureHandle);

    /// <summary>描画に使うキューブマップのハンドルを変更する。</summary>
    void SetTextureHandle(uint32_t textureHandle) { textureHandle_ = textureHandle; }

    /// <summary>現在のキューブマップのハンドルを返す。</summary>
    uint32_t GetTextureHandle() const { return textureHandle_; }

    /// <summary>ビュー・射影行列を使って背景を描画する。</summary>
    void Draw(ID3D12Resource* vpResource);

private:
    // 背景用の立方体メッシュとインデックスを作成する。
    void CreateMesh();

private:
    Object3dCommon* common_ = nullptr;
    uint32_t textureHandle_ = 0;

    // メッシュリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
};
