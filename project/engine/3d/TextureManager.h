#pragma once

#include <d3d12.h>
#include <string>
#include <vector>
#include <map>
#include <wrl.h>
#include "engine/base/DirectXCommon.h"

// DirectXTex (テクスチャ読み込み用ライブラリ) を使用するために必要
#include "externals/DirectXTex/DirectXTex.h"


/// <summary>
/// テクスチャ管理クラス
/// </summary>
class TextureManager {
public:
    /// <summary>
    /// 1つのテクスチャに関する全てのデータをまとめた構造体
    /// </summary>
    struct TextureData {
        std::string filePath; // テクスチャのファイルパス
        DirectX::TexMetadata metadata; // テクスチャのメタデータ (幅、高さ、フォーマットなど)
        Microsoft::WRL::ComPtr<ID3D12Resource> resource; // テクスチャリソース本体 (VRAM上)
        Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource; // 中間リソース (アップロード用)
        uint32_t srvHandle = 0; // SRVのハンドル (SRVManagerが管理)
    };

public: // シングルトン
    /// <summary>
    /// シングルトンインスタンスを取得する
    /// </summary>
    static TextureManager* GetInstance();

private:
    // シングルトンパターンのためのプライベートなコンストラクタ、デストラクタ、コピー制御
    TextureManager() = default;
    ~TextureManager() = default;
    TextureManager(const TextureManager&) = delete;
    const TextureManager& operator=(const TextureManager&) = delete;

public: // メンバ関数
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="dxCommon">DirectX汎用クラス</param>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// テクスチャをファイルから読み込む
    /// </summary>
    /// <param name="filePath">テクスチャのファイルパス</param>
    /// <returns>読み込んだテクスチャのハンドル (インデックス)</returns>
    uint32_t Load(const std::string& filePath);

    /// <summary>
    /// 指定したハンドルのテクスチャのメタデータを取得する
    /// </summary>
    /// <param name="textureHandle">テクスチャハンドル</param>
    /// <returns>テクスチャのメタデータ</returns>
    const DirectX::TexMetadata& GetMetadata(uint32_t textureHandle);

private:
    // DirectX汎用クラスへのポインタ
    DirectXCommon* dxCommon_ = nullptr;
    // D3D12デバイス
    Microsoft::WRL::ComPtr<ID3D12Device> device_;

    // 読み込んだテクスチャデータを格納するコンテナ
    std::vector<TextureData> textureDatas_;
    // ファイルパスとテクスチャハンドルの対応を記録するマップ (多重読み込み防止用)
    std::map<std::string, uint32_t> textureHandleMap_;
};