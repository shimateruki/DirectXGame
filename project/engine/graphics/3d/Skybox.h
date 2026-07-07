#pragma once
#include "engine/utility/math/Math.h"
#include "Object3dCommon.h"
#include <wrl.h>
#include <d3d12.h>
#include <stdint.h>

// Skyboxは、シーン背景として表示するキューブマップ用メッシュと描画処理を管理します。
class Skybox {
public:
    // スカイボックス用の頂点構造体 (POSITIONのみ)
        // スカイボックス用キューブメッシュの頂点情報です。
struct VertexData {
        Vector4 position;
    };

        // 共通描画機能とキューブマップテクスチャを受け取り、背景描画を初期化します。
void Initialize(Object3dCommon* common, uint32_t textureHandle);
    void SetTextureHandle(uint32_t textureHandle) { textureHandle_ = textureHandle; }
    uint32_t GetTextureHandle() const { return textureHandle_; }
        // ビュープロジェクション行列を使ってスカイボックスを描画します。
void Draw(ID3D12Resource* vpResource);

private:
        // 背景用の立方体メッシュとインデックスを作成します。
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
