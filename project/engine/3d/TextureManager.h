#pragma once

#include <d3d12.h>
#include <string>
#include <vector>
#include <map>
#include <wrl.h>
#include "engine/base/DirectXCommon.h"
#include "externals/DirectXTex/DirectXTex.h"

class TextureManager {
public:
    struct TextureData {
        std::string filePath;
        DirectX::TexMetadata metadata;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;
        uint32_t srvHandle = 0;
    };

public:
    static TextureManager* GetInstance();

private:
    TextureManager() = default;
    ~TextureManager() = default;
    TextureManager(const TextureManager&) = delete;
    const TextureManager& operator=(const TextureManager&) = delete;

public:
    void Initialize(DirectXCommon* dxCommon);
    uint32_t Load(const std::string& filePath);
    const DirectX::TexMetadata& GetMetadata(uint32_t textureHandle);

private:
    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;

    // ★★★ 修正点：vectorからmapに変更 ★★★
    // キーをSRVハンドル、値をテクスチャデータとする
    std::map<uint32_t, TextureData> textureDatas_;

    // ファイルパスからSRVハンドルへのマップはそのまま
    std::map<std::string, uint32_t> textureHandleMap_;
};