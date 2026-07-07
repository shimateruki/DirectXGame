#pragma once

#include <d3d12.h>
#include <map>
#include <string>
#include <vector>
#include <wrl.h>

#include "DirectXCommon.h"
#include "DirectXTex.h"

// テクスチャの読み込み、GPU リソース生成、SRV ハンドル管理をまとめるクラス
// TextureManagerは、画像ファイルの読み込み、DDSキャッシュ、SRV登録、ハンドル管理を担当します。
class TextureManager {
public:
        // 読み込んだテクスチャ1件分のパス、メタデータ、GPUリソース、SRV番号です。
struct TextureData {
        std::string filePath;
        DirectX::TexMetadata metadata;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint32_t srvHandle = 0;
    };

public:
        // エンジン全体で共有するテクスチャ管理インスタンスを取得します。
static TextureManager* GetInstance();

private:
    TextureManager() = default;
    ~TextureManager() = default;
    TextureManager(const TextureManager&) = delete;
    const TextureManager& operator=(const TextureManager&) = delete;

public:
        // DirectXCommonとDeviceを保持し、テクスチャ読み込みに備えます。
void Initialize(DirectXCommon* dxCommon);
        // テクスチャを読み込み、既存キャッシュが使える場合は同じハンドルを返します。
uint32_t Load(const std::string& fileName, bool isNormalMap = false, bool allowDDSCache = true, bool forceReload = false);
        // テクスチャサイズやフォーマットなどのメタデータを取得します。
const DirectX::TexMetadata& GetMetadata(uint32_t textureHandle);
        // 指定ディレクトリ以下のテクスチャをまとめて読み込みます。
void LoadAllTexture(const std::string& directoryPath);
    std::vector<std::string> GetLoadedTexturePaths() const;
        // ファイルパスに対応するSRVハンドルを取得します。
uint32_t GetSrvHandle(const std::string& filePath);

private:
    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;

    // キーを SRV ハンドル、値をテクスチャデータとして保持する。
    std::map<uint32_t, TextureData> textureDatas_;

    // ファイルパスから SRV ハンドルを逆引きする。
    std::map<std::string, uint32_t> textureHandleMap_;
};
