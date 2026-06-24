#pragma once

#include <d3d12.h>
#include <map>
#include <string>
#include <vector>
#include <wrl.h>

#include "DirectXCommon.h"
#include "DirectXTex.h"

// テクスチャの読み込み、GPU リソース生成、SRV ハンドル管理をまとめるクラス
class TextureManager {
public:
    struct TextureData {
        std::string filePath;
        DirectX::TexMetadata metadata;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
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
    uint32_t Load(const std::string& fileName, bool isNormalMap = false, bool allowDDSCache = true, bool forceReload = false);
    const DirectX::TexMetadata& GetMetadata(uint32_t textureHandle);
    void LoadAllTexture(const std::string& directoryPath);
    std::vector<std::string> GetLoadedTexturePaths() const;
    uint32_t GetSrvHandle(const std::string& filePath);

private:
    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;

    // キーを SRV ハンドル、値をテクスチャデータとして保持する。
    std::map<uint32_t, TextureData> textureDatas_;

    // ファイルパスから SRV ハンドルを逆引きする。
    std::map<std::string, uint32_t> textureHandleMap_;
};
