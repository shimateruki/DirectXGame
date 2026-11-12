#pragma once

#include <optional>
#include <span>

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

// 担当：画像デコード、DDSキャッシュ、GPU転送とSRV番号を管理する。
// 関連：DirectXCommon（転送）、SrvManager（記述子）、ScenePreloader（事前準備）
// 主な入口：Initialize / Load / BeginAsyncUploadBatch / PollAsyncUploadBatch
class TextureManager {
public:
    /// <summary>テクスチャをsRGBとして読むか、線形色として読むか。</summary>
    enum class TextureColorSpace {
        Auto,
        SRGB,
        Linear,
    };

    /// <summary>一枚のテクスチャのパス、メタデータ、GPUリソースとSRV番号。</summary>
    // 関連：TextureManager（この型を使う機能の本体）
    struct TextureData {
        std::string filePath;
        DirectX::TexMetadata metadata;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint32_t srvHandle = 0; // 画像をシェーダーから読むSRV番号。
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
    uint32_t Load(const std::string& fileName, bool isNormalMap = false, bool allowDdsCache = true, bool forceReload = false);

    /// <summary>色空間を明示してテクスチャを読み込む。</summary>
    uint32_t Load(
        const std::string& fileName,
        TextureColorSpace colorSpace,
        bool allowDdsCache = true,
        bool forceReload = false);

    // 画像ファイルのデコードだけを先行し、GPU生成は次回Loadまで遅延します。
    bool Prepare(
        const std::string& fileName,
        TextureColorSpace colorSpace = TextureColorSpace::Auto,
        bool allowDdsCache = true);

    /// <summary>テクスチャの寸法・フォーマットなどのメタデータを返す。</summary>
    const DirectX::TexMetadata& GetMetadata(uint32_t textureHandle);

    // 同時に一つの再生元が固定寸法の動画テクスチャを借りる。
    std::optional<uint32_t> AcquireVideoSurface(const void* owner);
    void ReleaseVideoSurface(const void* owner);
    // 描画リストへ記録し、前回の転送が未完了なら次のフレームへ延期する。
    bool UploadVideoSurface(const void* owner, ID3D12GraphicsCommandList& commands, std::span<const uint8_t> pixels);

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
    // 関連：TextureManager（この型を使う機能の本体）
    struct PreparedTextureData {
        DirectX::ScratchImage mipImages;
        float decodeDurationMs = 0.0f; // CPUデコードの所要時間（ミリ秒）。
    };

    /// <summary>非同期転送中に保持する転送先と中間バッファ。</summary>
    // 関連：TextureManager（この型を使う機能の本体）
    struct AsyncTextureUpload {
        Microsoft::WRL::ComPtr<ID3D12Resource> destination; // GPUへ転送する画像本体。
        Microsoft::WRL::ComPtr<ID3D12Resource> intermediate; // 転送完了まで保持するアップロードバッファ。
    };

    // キャッシュを確認し、必要なデコード・GPU生成・SRV登録を行う。
    uint32_t LoadInternal(
        const std::string& fileName,
        TextureColorSpace colorSpace,
        bool allowDdsCache,
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
    const void* videoOwner_ = nullptr;
    std::optional<uint32_t> videoHandle_;
    std::optional<uint64_t> videoUploadFence_; // 前回の転送が含まれるフレーム終端の値。
    Microsoft::WRL::ComPtr<ID3D12Resource> videoUpload_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT videoFootprint_{};

    // ファイルパスから SRV ハンドルを逆引きする。
    std::map<std::string, uint32_t> textureHandleMap_;
    std::map<std::string, std::unique_ptr<PreparedTextureData>> preparedTextures_; // GPU生成前のデコード済み画像。

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> copyCommandQueue_;
    Microsoft::WRL::ComPtr<ID3D12Fence> copyFence_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> asyncUploadAllocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> asyncUploadCommandList_;
    std::vector<AsyncTextureUpload> asyncTextureUploads_;
    uint64_t copyFenceValue_ = 0; // 転送キューのフェンス発行値。
    uint64_t pendingCopyFenceValue_ = 0; // 現在の転送完了判定で待つ値。
    bool asyncUploadBatchActive_ = false; // 画像転送をまとめて記録中か。

    // 転送バッチへの記録指定は呼び出し元スレッドごとに保持します。
    static thread_local bool asyncUploadRecording_;
    bool asyncUploadBatchSubmitted_ = false; // 記録した転送をGPUへ送信済みか。
    mutable std::mutex mutex_;
};
