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

// テクスチャの読み込み、GPU リソース生成、SRV ハンドル管理をまとめるクラス
// TextureManagerは、画像ファイルの読み込み、DDSキャッシュ、SRV登録、ハンドル管理を担当します。
class TextureManager {
public:
    enum class TextureColorSpace {
        Auto,
        SRGB,
        Linear,
    };

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
    // Sceneロード中のテクスチャ転送をCopy Queueへまとめ、描画を止めずに完了を監視します。
    bool BeginAsyncUploadBatch();
    void SetAsyncUploadRecording(bool enabled);
    bool SubmitAsyncUploadBatch();
    bool PollAsyncUploadBatch();
    void WaitForAsyncUploadBatch();
    bool IsAsyncUploadBatchPending() const;
        // テクスチャを読み込み、既存キャッシュが使える場合は同じハンドルを返します。
uint32_t Load(const std::string& fileName, bool isNormalMap = false, bool allowDDSCache = true, bool forceReload = false);
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
        // テクスチャサイズやフォーマットなどのメタデータを取得します。
const DirectX::TexMetadata& GetMetadata(uint32_t textureHandle);
        // 指定ディレクトリ以下のテクスチャをまとめて読み込みます。
void LoadAllTexture(const std::string& directoryPath);
    std::vector<std::string> GetLoadedTexturePaths() const;
    // GPUへ読み込まず、Editorで選択可能な画像ファイルを列挙します。
    std::vector<std::string> GetAvailableTexturePaths() const;
        // ファイルパスに対応するSRVハンドルを取得します。
uint32_t GetSrvHandle(const std::string& filePath);

private:
    struct PreparedTextureData {
        DirectX::ScratchImage mipImages;
        float decodeDurationMs = 0.0f;
    };

    struct AsyncTextureUpload {
        Microsoft::WRL::ComPtr<ID3D12Resource> destination;
        Microsoft::WRL::ComPtr<ID3D12Resource> intermediate;
    };

    uint32_t LoadInternal(
        const std::string& fileName,
        TextureColorSpace colorSpace,
        bool allowDDSCache,
        bool forceReload);
    bool RecordAsyncTextureUpload(
        ID3D12Resource* texture,
        const DirectX::ScratchImage& mipImages);
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
    bool asyncUploadRecording_ = false;
    bool asyncUploadBatchSubmitted_ = false;
    mutable std::mutex mutex_;
};
