#pragma once

#include <d3d12.h>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <wrl.h>

#include "DirectXCommon.h"
#include "DirectXTex.h"

/// <summary>画像読込、DDSキャッシュ、GPUリソース生成とSRV番号を管理する。</summary>
class TextureManager {
public:
    /// <summary>テクスチャをsRGBとして読むか、線形色として読むか。</summary>
    enum class TextureColorSpace {
        Auto,
        SRGB,
        Linear,
    };

    /// <summary>一枚のテクスチャのパス、メタデータ、GPUリソースとSRV番号。</summary>
    struct TextureData {
        std::string filePath;
        DirectX::TexMetadata metadata;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint32_t srvHandle = 0;
    };

public:
    /// <summary>エンジン全体で共有するテクスチャ管理インスタンスを返す。</summary>
    static TextureManager* GetInstance();

private:
    TextureManager() = default;
    ~TextureManager() = default;
    TextureManager(const TextureManager&) = delete;
    const TextureManager& operator=(const TextureManager&) = delete;

public:
    /// <summary>DirectX基盤を保持してテクスチャ転送に備える。</summary>
    void Initialize(DirectXCommon* dxCommon);

    // Sceneロード中のテクスチャ転送をCopy Queueへまとめ、描画を止めずに完了を監視します。
    bool BeginAsyncUploadBatch();

    /// <summary>新しいテクスチャ転送を非同期バッチへ記録するか切り替える。</summary>
    void SetAsyncUploadRecording(bool enabled);

    /// <summary>記録済みの転送バッチをGPUへ送信する。</summary>
    bool SubmitAsyncUploadBatch();

    /// <summary>送信済み転送バッチの完了を確認する。</summary>
    bool PollAsyncUploadBatch();

    /// <summary>送信済み転送バッチが完了するまで待つ。</summary>
    void WaitForAsyncUploadBatch();

    /// <summary>未完了の非同期転送バッチがあるか返す。</summary>
    bool IsAsyncUploadBatchPending() const;

    /// <summary>テクスチャを読み込み、既存キャッシュが使えれば同じSRV番号を返す。</summary>
    uint32_t Load(const std::string& fileName, bool isNormalMap = false, bool allowDDSCache = true, bool forceReload = false);

    /// <summary>色空間を明示してテクスチャを読み込む。</summary>
    uint32_t Load(
        const std::string& fileName,
        TextureColorSpace colorSpace,
        bool allowDDSCache = true,
        bool forceReload = false);

    // 画像ファイルのデコードだけを先行し、GPU生成は次回Loadまで遅延します。
    bool Prepare(
        const std::string& fileName,
        TextureColorSpace colorSpace = TextureColorSpace::Auto,
        bool allowDDSCache = true);

    /// <summary>テクスチャの寸法・フォーマットなどのメタデータを返す。</summary>
    const DirectX::TexMetadata& GetMetadata(uint32_t textureHandle);

    /// <summary>指定ディレクトリ以下のテクスチャを一括で読み込む。</summary>
    void LoadAllTexture(const std::string& directoryPath);

    /// <summary>現在ロード済みのテクスチャパスを返す。</summary>
    std::vector<std::string> GetLoadedTexturePaths() const;

    // GPUへ読み込まず、Editorで選択可能な画像ファイルを列挙します。
    std::vector<std::string> GetAvailableTexturePaths() const;

    /// <summary>ファイルパスに対応するSRV番号を返す。</summary>
    uint32_t GetSrvHandle(const std::string& filePath);

private:
    /// <summary>デコード済み画像とデコード所要時間を一時保持する。</summary>
    struct PreparedTextureData {
        DirectX::ScratchImage mipImages;
        float decodeDurationMs = 0.0f;
    };

    /// <summary>非同期転送中に保持する転送先と中間バッファ。</summary>
    struct AsyncTextureUpload {
        Microsoft::WRL::ComPtr<ID3D12Resource> destination;
        Microsoft::WRL::ComPtr<ID3D12Resource> intermediate;
    };

    // キャッシュを確認し、必要なデコード・GPU生成・SRV登録を行う。
    uint32_t LoadInternal(
        const std::string& fileName,
        TextureColorSpace colorSpace,
        bool allowDDSCache,
        bool forceReload);

    // 指定画像の転送コマンドを現在の非同期バッチへ記録する。
    bool RecordAsyncTextureUpload(
        ID3D12Resource* texture,
        const DirectX::ScratchImage& mipImages);

    // 完了したバッチの中間バッファなどを解放する。
    void ReleaseCompletedAsyncUploadBatch();

    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;

    // キーを SRV ハンドル、値をテクスチャデータとして保持する。
    std::map<uint32_t, TextureData> textureDatas_;

    // ファイルパスから SRV ハンドルを逆引きする。
    std::map<std::string, uint32_t> textureHandleMap_;
    std::map<std::string, std::unique_ptr<PreparedTextureData>> preparedTextures_;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> copyCommandQueue_;
    Microsoft::WRL::ComPtr<ID3D12Fence> copyFence_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> asyncUploadAllocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> asyncUploadCommandList_;
    std::vector<AsyncTextureUpload> asyncTextureUploads_;
    uint64_t copyFenceValue_ = 0;
    uint64_t pendingCopyFenceValue_ = 0;
    bool asyncUploadBatchActive_ = false;

    // 転送バッチへの記録指定は呼び出し元スレッドごとに保持します。
    static thread_local bool asyncUploadRecording_;
    bool asyncUploadBatchSubmitted_ = false;
    mutable std::mutex mutex_;
};
