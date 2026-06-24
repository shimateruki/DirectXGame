#pragma once
#include "engine/utility/math/Math.h"
#include "Object3dCommon.h"
#include <wrl.h>
#include <d3d12.h>
#include <stdint.h>

class Skybox {
public:
    // スカイボックス用の頂点構造体 (POSITIONのみ)
    struct VertexData {
        Vector4 position;
    };

    void Initialize(Object3dCommon* common, uint32_t textureHandle);
    void SetTextureHandle(uint32_t textureHandle) { textureHandle_ = textureHandle; }
    uint32_t GetTextureHandle() const { return textureHandle_; }
    void Draw(ID3D12Resource* vpResource);

private:
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
