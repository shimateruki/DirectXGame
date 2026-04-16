#pragma once

#include <d3d12.h>
#include <string>
#include <vector>
#include <map>
#include <wrl.h>
#include "DirectXCommon.h"
#include "DirectXTex.h"

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
    uint32_t Load(const std::string& fileName);
    const DirectX::TexMetadata& GetMetadata(uint32_t textureHandle);
    void LoadAllTexture(const std::string& directoryPath);
    std::vector<std::string> GetLoadedTexturePaths() const;
    uint32_t GetSrvHandle(const std::string& filePath);

private:
    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;

    // キーをSRVハンドル、値をテクスチャデータとする
    std::map<uint32_t, TextureData> textureDatas_;

    // ファイルパスからSRVハンドルへのマップはそのまま
    std::map<std::string, uint32_t> textureHandleMap_;


};