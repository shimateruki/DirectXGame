#pragma once

#include "DirectXTex.h"
#include <chrono>
#include <d3d12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <map>
#include <string>
#include <wrl.h>

class WinApp;

/// <summary>
/// DirectX 12の初期化、コマンド実行、描画ターゲット、GPU同期をまとめて管理する。
/// </summary>
// DirectXCommonは、D3D12デバイス、SwapChain、コマンド、各種レンダーターゲットを管理する基盤クラスです。
class DirectXCommon {
public:
    /// <summary>
    /// シングルトンインスタンスを取得する。
    /// </summary>
        // エンジン全体で共有するDirectX管理インスタンスを取得します。
static DirectXCommon* GetInstance();

    /// <summary>
    /// DirectX関連リソースを初期化する。
    /// </summary>
        // ウィンドウ情報を受け取り、DirectX12の主要リソースを初期化します。
void Initialize(WinApp* winApp);

    /// <summary>
    /// DirectX関連リソースを終了処理する。
    /// </summary>
        // GPU完了待ちを行い、DirectX関連リソースを安全に終了します。
void Finalize();

    /// <summary>
    /// バックバッファへの描画開始処理を行う。
    /// </summary>
        // 1フレームの描画開始前にコマンドリストやバックバッファ状態を準備します。
void PreDraw();

    /// <summary>
    /// バックバッファへの描画終了処理を行う。
    /// </summary>
        // 描画コマンドを実行し、Presentとフェンス更新を行います。
void PostDraw();

    // DirectXの主要オブジェクト取得。
    ID3D12Device* GetDevice() const { return device_.Get(); }
    WinApp* GetWinApp() const { return winApp_; }
    ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }
    DXGI_FORMAT GetRTVFormat() const { return rtvFormat_; }
    size_t GetBackBufferCount() const { return backBufferCount_; }
    uint32_t GetFrameCount() const { return frameCount_; }

    // SRVの最大数。
    static const uint32_t kMaxSRVCount = 512;

    void InitalaizeFixFPS();
    void UpdateFixFPS();

    /// <summary>
    /// HLSLシェーダーをコンパイルする。
    /// </summary>
        // HLSLファイルを指定プロファイルでコンパイルし、パイプライン作成に使えるBlobを返します。
Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profileconst, const wchar_t* entryPoint = L"main");

    /// <summary>
    /// アップロードヒープ上にバッファリソースを作成する。
    /// </summary>
        // 定数バッファや頂点バッファ用のアップロードリソースを作成します。
Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);

    /// <summary>
    /// テクスチャ用リソースを作成する。
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);

    /// <summary>
    /// テクスチャデータをリソースへアップロードする。
    /// </summary>
    void UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages);

    /// <summary>
    /// テクスチャファイルを読み込む。
    /// </summary>
        // 画像ファイルを読み込み、必要に応じてDDSキャッシュやsRGB変換を扱います。
static DirectX::ScratchImage LoadTexture(const std::string& filePath, bool forceSRGB = true);

    /// <summary>
    /// UTF-8文字列をワイド文字列に変換する。
    /// </summary>
    static std::wstring ConvertString(const std::string& str);

        // 送信済みコマンドの完了を待ち、必要ならコマンドリストを再利用可能にします。
void FlushCommandQueue(bool reset = true);

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);

    /// <summary>
    /// 標準の深度ステンシル設定を取得する。
    /// </summary>
    D3D12_DEPTH_STENCIL_DESC GetDefaultDepthStencilDesc() const;

    /// <summary>
    /// 深度ステンシルビューのフォーマットを取得する。
    /// </summary>
    DXGI_FORMAT GetDSVFormat() const;

    void WaitForGPUAndReset();
    void WaitForGPUIdle();

    ID3D12CommandAllocator* GetCommandAllocator() const { return commandAllocator_.Get(); }
    ID3D12CommandQueue* GetCommandQueue() const { return commandQueue_.Get(); }
    ID3D12Fence* GetFence() const { return fence_.Get(); }
    uint64_t GetFenceValue() const { return fenceValue_; }
    HANDLE GetFenceEvent() const { return fenceEvent_; }

    // GameView用レンダーテクスチャ。
    void CreateRenderTexture();
    void PreDrawRenderTexture();
    void PostDrawRenderTexture();
    uint32_t GetRenderTextureSrvHandle() const { return renderTextureSrvHandle_; }

    void SetRenderClearColor(float r, float g, float b, float a);

    void PreDrawBackBuffer();

    // シャドウマップ描画。
    void CreateShadowMap();
    uint32_t GetShadowMapSrvHandle() const { return shadowMapSrvHandle_; }
    int GetShadowMapResolution() const { return shadowMapResolution_; }
    void SetShadowMapResolution(int resolution);
    void PreDrawShadow();
    void PostDrawShadow();

    float GetGpuDrawTimeMs() const { return gpuDrawTimeMs_; }

    // GPU計測。
    void StartGpuProfile(const std::string& name = "Total");
    void EndGpuProfile(const std::string& name = "Total");
    void ResetGpuProfiles();
    void ReadAllGpuProfiles();

    void CreateDepthSrv();
    uint32_t GetDepthSrvHandle() const { return depthSrvHandle_; }

    // ローカルフォグや画面効果で使う描画先。
    void PreDrawLocalFog();
    void PostDrawLocalFog();
    uint32_t GetGrabSrvHandle() const { return grabSrvHandle_; }
    void UpdateGrabTexture();

    // Camera Preview小窓の描画中かどうか。
    // Preview中は通常GameView用のGrabTexture更新を避ける。
    void SetCameraPreviewRendering(bool isRendering) { cameraPreviewRendering_ = isRendering; }
    bool IsCameraPreviewRendering() const { return cameraPreviewRendering_; }

        // ウィンドウサイズ変更に合わせてSwapChainと関連RTV/DSVを作り直します。
void ResizeSwapChain(int32_t width, int32_t height);
    void RequestResize(int32_t width, int32_t height);
    void ProcessPendingResize();

private:
    DirectXCommon() = default;
    ~DirectXCommon() = default;
    DirectXCommon(const DirectXCommon&) = delete;
    const DirectXCommon& operator=(const DirectXCommon&) = delete;

    // DirectX初期化の内部手順。
        // DXGI Factory、D3D12 Device、DXCなどの低レベルオブジェクトを初期化します。
void InitializeDXGIDevice();
    void CreateCommand();
    void CreateSwapChain();
    void CreateRTV();
    void CreateDSV();
    void CreateFence();

    /// <summary>
    /// 深度ステンシル用のテクスチャリソースを作成する。
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(int32_t width, int32_t height);

private:
    WinApp* winApp_ = nullptr;

    // 固定FPS制御用の基準時刻。
    std::chrono::steady_clock::time_point reference_;

    // DirectXの主要リソース。
    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Device> device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_ = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources_[2] = {};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_ = nullptr;
    uint32_t depthSrvHandle_ = 0;
    bool pendingResize_ = false;
    int32_t pendingResizeWidth_ = 0;
    int32_t pendingResizeHeight_ = 0;

    // GPU同期用。
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_ = nullptr;
    uint64_t fenceValue_ = 0;
    HANDLE fenceEvent_ = nullptr;

    // 描画領域。
    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissorRect_{};
    bool cameraPreviewRendering_ = false;

    DXGI_FORMAT rtvFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    UINT backBufferIndex_ = 0;
    const size_t backBufferCount_ = 2;

    // シェーダーコンパイル用。
    Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_ = nullptr;
    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_ = nullptr;
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_ = nullptr;

    // GameView用レンダーテクスチャ。
    Microsoft::WRL::ComPtr<ID3D12Resource> renderTexture_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtRtvHeap_;
    uint32_t renderTextureSrvHandle_ = 0;
    float clearColor_[4] = { 0.1f, 0.25f, 0.5f, 1.0f };

    // シャドウマップ。
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowMapResource_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shadowDsvHeap_ = nullptr;
    uint32_t shadowMapSrvHandle_ = 0;
    int shadowMapResolution_ = 2048;

    // GPU計測。
    static const uint32_t kMaxGpuQueries = 128;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> queryResultBuffer_;
    uint64_t gpuFrequency_ = 0;
    float gpuDrawTimeMs_ = 0.0f;
    std::map<std::string, uint32_t> gpuProfileMap_;
    uint32_t nextQueryIndex_ = 0;

    bool useVSync_ = true;
    Microsoft::WRL::ComPtr<ID3D12Resource> grabTexture_;
    uint32_t grabSrvHandle_ = 0;
    uint32_t frameCount_ = 0;
};
