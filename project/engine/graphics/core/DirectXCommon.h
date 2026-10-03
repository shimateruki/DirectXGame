#pragma once

#include "DirectXTex.h"
#include <chrono>
#include <d3d12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <map>
#include <mutex>
#include <string>
#include <wrl.h>

class WinApp;

/// <summary>
/// DirectX 12の初期化、描画先、コマンド実行、GPU同期を管理する。
/// </summary>
class DirectXCommon {
public:
    /// <summary>エンジン全体で共有するDirectX管理インスタンスを取得する。</summary>
    static DirectXCommon* GetInstance();

    /// <summary>ウィンドウ情報からDirectX 12の主要リソースを初期化する。</summary>
    /// <param name="winApp">SwapChainの対象となるウィンドウ。</param>
    void Initialize(WinApp* winApp);

    /// <summary>GPUの完了を待ち、DirectX関連リソースを終了する。</summary>
    void Finalize();

    /// <summary>バックバッファ描画前にコマンドリストと描画先を準備する。</summary>
    void PreDraw();

    /// <summary>描画コマンドを実行してPresentとフェンスを更新する。</summary>
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

    /// <summary>固定FPS用の基準時刻を初期化する。</summary>
    void InitalaizeFixFPS();

    /// <summary>固定FPSの待機を行い、次フレームの基準時刻を更新する。</summary>
    void UpdateFixFPS();

    /// <summary>HLSLファイルを指定プロファイルでコンパイルする。</summary>
    /// <param name="filePath">読み込むシェーダーファイル。</param>
    /// <param name="profileconst">シェーダーのターゲットプロファイル。</param>
    /// <param name="entryPoint">コンパイル対象のエントリーポイント。</param>
    /// <returns>コンパイル済みバイトコード。</returns>
    Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profileconst, const wchar_t* entryPoint = L"main");

    /// <summary>定数・頂点データ用のアップロードヒープを作成する。</summary>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);

    /// <summary>
    /// テクスチャ用リソースを作成する。
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);

    /// <summary>
    /// テクスチャデータをリソースへアップロードする。
    /// </summary>
    void UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages);

    /// <summary>画像ファイルを読み込み、必要に応じてDDSキャッシュとsRGB変換を使う。</summary>
    static DirectX::ScratchImage LoadTexture(const std::string& filePath, bool forceSRGB = true);

    /// <summary>
    /// UTF-8文字列をワイド文字列に変換する。
    /// </summary>
    static std::wstring ConvertString(const std::string& str);

    /// <summary>送信済みコマンドの完了を待つ。</summary>
    /// <param name="reset">完了後にコマンドリストを再利用可能にするか。</param>
    void FlushCommandQueue(bool reset = true);

    /// <summary>指定種類・個数のディスクリプタヒープを作る。</summary>
    /// <param name="shaderVisible">シェーダーから参照可能なヒープにするか。</param>
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);

    /// <summary>
    /// 標準の深度ステンシル設定を取得する。
    /// </summary>
    D3D12_DEPTH_STENCIL_DESC GetDefaultDepthStencilDesc() const;

    /// <summary>
    /// 深度ステンシルビューのフォーマットを取得する。
    /// </summary>
    DXGI_FORMAT GetDSVFormat() const;

    /// <summary>GPUの完了を待ち、コマンドリストを再利用可能な状態へ戻す。</summary>
    void WaitForGPUAndReset();

    /// <summary>GPUに送信済みの作業が終わるまで待つ。</summary>
    void WaitForGPUIdle();

    ID3D12CommandAllocator* GetCommandAllocator() const { return commandAllocator_.Get(); }
    ID3D12CommandQueue* GetCommandQueue() const { return commandQueue_.Get(); }
    ID3D12Fence* GetFence() const { return fence_.Get(); }
    uint64_t GetFenceValue() const { return fenceValue_; }
    HANDLE GetFenceEvent() const { return fenceEvent_; }

    // GameView用レンダーテクスチャの生成、描画開始・終了、SRV取得。
    void CreateRenderTexture();
    void PreDrawRenderTexture();
    void PostDrawRenderTexture();
    uint32_t GetRenderTextureSrvHandle() const { return renderTextureSrvHandle_; }

    /// <summary>描画先をクリアするRGBA色を設定する。</summary>
    void SetRenderClearColor(float r, float g, float b, float a);

    /// <summary>SwapChainのバックバッファを描画先として準備する。</summary>
    void PreDrawBackBuffer();

    // シャドウマップ描画。
    void CreateShadowMap();
    uint32_t GetShadowMapSrvHandle() const { return shadowMapSrvHandle_; }
    int GetShadowMapResolution() const { return shadowMapResolution_; }
    void SetShadowMapResolution(int resolution);
    void PreDrawShadow();
    void PostDrawShadow();

    /// <summary>直近のGPU描画時間をミリ秒で返す。</summary>
    float GetGpuDrawTimeMs() const { return gpuDrawTimeMs_; }

    // GPU計測。
    void StartGpuProfile(const std::string& name = "Total");
    void EndGpuProfile(const std::string& name = "Total");
    void ResetGpuProfiles();
    void ReadAllGpuProfiles();

    /// <summary>深度テクスチャをシェーダーから参照するSRVを作る。</summary>
    void CreateDepthSrv();
    uint32_t GetDepthSrvHandle() const { return depthSrvHandle_; }

    // ローカルフォグや画面効果で使う描画先。
    void PreDrawLocalFog();
    void PostDrawLocalFog();
    uint32_t GetGrabSrvHandle() const { return grabSrvHandle_; }

    /// <summary>現在の画面カラーをGrabTextureへコピーする。</summary>
    void UpdateGrabTexture();

    // Camera Preview小窓の描画中かどうか。
    // Preview中は通常GameView用のGrabTexture更新を避ける。
    void SetCameraPreviewRendering(bool isRendering) { cameraPreviewRendering_ = isRendering; }
    bool IsCameraPreviewRendering() const { return cameraPreviewRendering_; }

    /// <summary>ウィンドウサイズに合わせてSwapChainとRTV/DSVを作り直す。</summary>
    void ResizeSwapChain(int32_t width, int32_t height);

    /// <summary>描画中の直接再生成を避けるため、リサイズを予約する。</summary>
    void RequestResize(int32_t width, int32_t height);

    /// <summary>予約済みのリサイズを安全なタイミングで適用する。</summary>
    void ProcessPendingResize();

private:
    DirectXCommon() = default;
    ~DirectXCommon() = default;
    DirectXCommon(const DirectXCommon&) = delete;
    const DirectXCommon& operator=(const DirectXCommon&) = delete;

    // DirectX初期化の内部手順。依存するリソースを順に作る。
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
    mutable std::mutex clearColorMutex_;

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
