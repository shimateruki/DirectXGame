
#include "engine/base/DirectXCommon.h"
#include "engine/base/WinApp.h"
#include <cassert>
#include <format>
#include <vector>
#include <dxcapi.h>
#include <fstream>
#include <thread>
#include "engine/base/SRVManager.h"
#include"engine/io/ImguiManager.h"

// ログ出力用のヘルパー関数（グローバル）
void Log(const std::string& message) { OutputDebugStringA(message.c_str()); }
std::string ConvertString(const std::wstring& str);

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")

DirectXCommon* DirectXCommon::GetInstance() {
    static DirectXCommon instance;
    return &instance;
}

void DirectXCommon::Initialize(WinApp* winApp) {
    assert(winApp);
    winApp_ = winApp;

    InitalaizeFixFPS();
    // 各種初期化処理
    InitializeDXGIDevice();
    CreateCommand();
    CreateSwapChain();
    CreateRTV();
    CreateDSV();
    CreateFence();

    // ビューポートとシザー矩形の設定
    viewport_.Width = (float)WinApp::kClientWidth;
    viewport_.Height = (float)WinApp::kClientHeight;
    viewport_.TopLeftX = 0;
    viewport_.TopLeftY = 0;
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;
    scissorRect_.left = 0;
    scissorRect_.right = WinApp::kClientWidth;
    scissorRect_.top = 0;
    scissorRect_.bottom = WinApp::kClientHeight;

    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
    dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);

}

void DirectXCommon::PreDraw() {
    // コマンドアロケータをリセットします。
    HRESULT hr = commandAllocator_->Reset();
    assert(SUCCEEDED(hr));

    // コマンドリストをリセットします。
    hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
    assert(SUCCEEDED(hr));

    // 現在描画対象となっているバックバッファのインデックスを取得します。
    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

    // リソースバリアを設定します。
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList_->ResourceBarrier(1, &barrier);

    // レンダーターゲットビュー(RTV)のディスクリプタハンドルのポインタを取得します。
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
    rtvHandle.ptr = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart().ptr + (backBufferIndex_ * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));

    // 深度ステンシルビュー(DSV)のディスクリプタハンドルのポインタを取得します。
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

    // 出力マージャ(OM)ステージに、描画先となるRTVとDSVを設定します。
    commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

    // 画面を指定した色でクリアします (例: 青みがかった灰色)。
    float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
    commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // 深度バッファをクリアします。値を1.0f(最も遠い)に設定します。
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // ビューポート（描画する画面内の領域）を設定します。
    commandList_->RSSetViewports(1, &viewport_);
    // シザー矩形（ピクセルを描画する範囲を限定する矩形）を設定します。
    commandList_->RSSetScissorRects(1, &scissorRect_);

    // ★★★ SRVManagerからデスクリプタヒープを取得して設定 ★★★
    ID3D12DescriptorHeap* descriptorHeaps[] = { SRVManager::GetInstance()->GetDescriptorHeap() };
    commandList_->SetDescriptorHeaps(1, descriptorHeaps);
}




Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateBufferResource(size_t sizeInBytes)
{
    //頂点とリソース用のヒープ設定
    D3D12_HEAP_PROPERTIES uploadHeapProperties{};
    uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    //頂点リソースの設定
    D3D12_RESOURCE_DESC vertResoucesDesc{};
    //バッファーリソーステクスチャの場合は別の指定をする
    vertResoucesDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vertResoucesDesc.Width = sizeInBytes; // ★★★ 注意：元のコードの *3 は削除しました ★★★
    //バッファの場合は1にする
    vertResoucesDesc.Height = 1;
    vertResoucesDesc.DepthOrArraySize = 1;
    vertResoucesDesc.MipLevels = 1;
    vertResoucesDesc.SampleDesc.Count = 1;
    //バッファの場合はこれをする決まり
    vertResoucesDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    //実際に頂点リソースを作る
    Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
    // ★★★ device をメンバ変数の device_ に変更 ★★★
    HRESULT hr = device_->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
        &vertResoucesDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));
    return resource;
}


void DirectXCommon::InitalaizeFixFPS()
{
	// 現在の時刻を取得して記録
	reference_ = std::chrono::steady_clock::now();
}

void DirectXCommon::UpdateFixFPS()
{
    //1/60秒びったりの時間
    const std::chrono::microseconds kMinTimer(uint64_t(1000000.0f / 60.0f));
    //1/60秒よりわずかに短い時間
    const std::chrono::microseconds  kMinCheckTime(uint64_t(1000000.0f / 65.0f));
	// 現在の時刻を取得
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	// 前回記録した時刻からの経過時間を計算
	std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - reference_);
//1/60秒(よりわずかに短い時間)立っていない場合
    if (elapsed < kMinTimer)
    {
        //1/60経過するまでの微小なスリープを繰り返す
        while (std::chrono::steady_clock::now() - reference_ < kMinTimer)
        {
			std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }
	// 現在の時刻を再取得
	reference_ = std::chrono::steady_clock::now();
}

Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompileShader(
    const std::wstring& filePath, const wchar_t* profile)
{
    // ログ出力用のストリームを準備
    std::ofstream logStream("shader_compile.log", std::ios_base::app);

    Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
    // メンバ変数のdxcUtils_を使う
    HRESULT hr = dxcUtils_->LoadFile(filePath.c_str(), nullptr, &shaderSource);
    assert(SUCCEEDED(hr));

    DxcBuffer shaderSourceBuffer;
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8;

    LPCWSTR arguments[] = {
        filePath.c_str(), L"-E", L"main", L"-T", profile,
        L"-Zi", L"-Qembed_debug", L"-Od", L"-Zpr",
    };

    Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
    // メンバ変数のdxcCompiler_, includeHandler_を使う
    hr = dxcCompiler_->Compile(
        &shaderSourceBuffer, arguments, _countof(arguments),
        includeHandler_.Get(), IID_PPV_ARGS(&shaderResult));
    assert(SUCCEEDED(hr));

    Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError = nullptr;
    shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
        // Log(shaderError->GetStringPointer()); // ログ関数を呼び出す
        assert(false);
    }

    Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    assert(SUCCEEDED(hr));

    return shaderBlob;
}

void DirectXCommon::Finalize() {
;
}

void DirectXCommon::PostDraw() {
    ImGuiManager::GetInstance()->Draw();
    // リソースバリアを再度設定します。
    // 描画が終わったバックバッファの状態を「描画ターゲット用(RENDER_TARGET)」から「表示用(PRESENT)」に切り替えます。
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList_->ResourceBarrier(1, &barrier);

    // コマンドリストへのコマンドの記録を終了します。
    HRESULT hr = commandList_->Close();
    assert(SUCCEEDED(hr));

    // FPS固定のための更新処理（独自実装の関数）
    UpdateFixFPS();

    // コマンドリストの配列を作成します（今回は1つだけ）。
    ID3D12CommandList* commandLists[] = { commandList_.Get() };
    // コマンドキューにコマンドリストを投入し、GPUに実行を指示します。
    commandQueue_->ExecuteCommandLists(1, commandLists);

    // スワップチェーンに命令を送り、バックバッファをフロントバッファに表示（プレゼント）します。
    // 第1引数: VSync同期間隔 (1ならVSync待ち)
    // 第2引数: オプションフラグ
    swapChain_->Present(1, 0);

    // --- ここから次のフレームのためのCPUとGPUの同期処理 ---

    // フェンスの目標値をインクリメントします。
    fenceValue_++;
    // GPUがコマンドキューのここまで処理を終えたら、フェンスに新しい値を書き込むよう指示します。
    commandQueue_->Signal(fence_.Get(), fenceValue_);

    // GPUの処理がまだ完了していないかチェックします。
    if (fence_->GetCompletedValue() < fenceValue_) {
        // GPUが指定したフェンス値に達したときに発火するイベントを設定します。
        fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
        // CPUは、そのイベントが発火するまでここで待機します。
        // これにより、GPUが前のフレームの処理を終えるまで、CPUが次のフレームの準備を始めるのを防ぎます。
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}
void DirectXCommon::InitializeDXGIDevice() {

#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12Debug1> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        //debugController->SetEnableGPUBasedValidation(true);
    }
#endif

    HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory_));
    assert(SUCCEEDED(hr));

    Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter;
    for (UINT i = 0; dxgiFactory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC3 adapterDesc{};
        hr = useAdapter->GetDesc3(&adapterDesc);
        assert(SUCCEEDED(hr));
        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
            break;
        }
        useAdapter = nullptr;
    }
    assert(useAdapter != nullptr);

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };
    for (size_t i = 0; i < _countof(featureLevels); ++i) {
        hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device_));
        if (SUCCEEDED(hr)) {
            break;
        }
    }
    assert(device_ != nullptr);

#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
        D3D12_MESSAGE_ID denyids[] = { D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE };
        D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
        D3D12_INFO_QUEUE_FILTER filter{};
        filter.DenyList.NumIDs = _countof(denyids);
        filter.DenyList.pIDList = denyids;
        filter.DenyList.NumSeverities = _countof(severities);
        filter.DenyList.pSeverityList = severities;
        infoQueue->PushStorageFilter(&filter);
    }
#endif
}

void DirectXCommon::CreateCommand() {
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
    HRESULT hr = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
    assert(SUCCEEDED(hr));
    hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
    assert(SUCCEEDED(hr));
    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_));
    assert(SUCCEEDED(hr));
}

void DirectXCommon::CreateSwapChain() {
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = WinApp::kClientWidth;
    swapChainDesc.Height = WinApp::kClientHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = (UINT)backBufferCount_;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    HRESULT hr = dxgiFactory_->CreateSwapChainForHwnd(commandQueue_.Get(), winApp_->GetHwnd(), &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain_.GetAddressOf()));
    assert(SUCCEEDED(hr));
}

void DirectXCommon::CreateRTV() {
    rtvDescriptorHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, backBufferCount_, false);
    for (UINT i = 0; i < backBufferCount_; ++i) {
        HRESULT hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&swapChainResources_[i]));
        assert(SUCCEEDED(hr));
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
        rtvHandle.ptr = rtvStartHandle.ptr + (i * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
        device_->CreateRenderTargetView(swapChainResources_[i].Get(), &rtvDesc, rtvHandle);
    }
}

void DirectXCommon::CreateDSV() {

    dsvDescriptorHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

    // 深度バッファとして使用するテクスチャリソースを作成
    // サイズはクライアント領域（ウィンドウ）の幅と高さに合わせる
    depthStencilResource_ = CreateDepthStencilTextureResource(WinApp::kClientWidth, WinApp::kClientHeight);

    // 深度ステンシルビューのデスクリプタ（設定）を定義
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    // リソースのフォーマットを指定。D24_UNORM_S8_UINTは24bitの深度と8bitのステンシルを意味する
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    // どのような次元のリソースとして見るかを設定（今回は2Dテクスチャ）
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    // 上記の設定を基に、深度ステンシルビューを作成
    device_->CreateDepthStencilView(depthStencilResource_.Get(), &dsvDesc, dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());
}

void DirectXCommon::CreateFence() {
    HRESULT hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    assert(SUCCEEDED(hr));
    fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(fenceEvent_ != nullptr);
    fenceValue_ = 0;
}


Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateDepthStencilTextureResource(int32_t width, int32_t height) {
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE depthClearValue{};
    depthClearValue.DepthStencil.Depth = 1.0f;
    depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
    HRESULT hr = device_->CreateCommittedResource(
        &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));

    return resource;
}


Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateTextureResource(const DirectX::TexMetadata & metadata)
{
    //metadataを軸にResoucesの設定
    D3D12_RESOURCE_DESC resouceDesc{};
    resouceDesc.Width = UINT(metadata.width);
    resouceDesc.Height = UINT(metadata.height);
    resouceDesc.MipLevels = UINT(metadata.mipLevels);
    resouceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
    resouceDesc.Format = metadata.format;
    resouceDesc.SampleDesc.Count = 1;
    resouceDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);

    //利用するheapの設定
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    //resoucesの作成
    Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
    // ★★★ device をメンバ変数の device_ に変更 ★★★
    HRESULT hr = device_->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resouceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));
    return resource;
}


void DirectXCommon::UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages)
{
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

    for (size_t mipLevel = 0; mipLevel < metadata.mipLevels; mipLevel++)
    {
        const DirectX::Image* img = mipImages.GetImage(mipLevel, 0, 0);
        HRESULT hr = texture->WriteToSubresource
        (
            UINT(mipLevel),
            nullptr,
            img->pixels,
            UINT(img->rowPitch),
            UINT(img->slicePitch)
        );
        assert(SUCCEEDED(hr));
    }
}


DirectX::ScratchImage DirectXCommon::LoadTexture(const std::string& filePath)
{
    //テクスチャファイルを読み込んでプログラムで扱えるようにする
    DirectX::ScratchImage image{};
    DirectX::ScratchImage mipImages{};
    // ★★★ クラス内の static 関数を呼び出すように変更 ★★★
    std::wstring filePathW = DirectXCommon::ConvertString(filePath);
    HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);

    //テクスチャが読み込まれなかった場合は白色のテクスチャを張る
    if (FAILED(hr))
    {
        D3D12_RESOURCE_DESC materialData;
        materialData.Width = 1;
        materialData.Height = 1;
        materialData.DepthOrArraySize = 1;
        materialData.MipLevels = 1;
        materialData.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        materialData.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        //白色のテクスチャを作成
        DirectX::Image whiteImage;
        whiteImage.width = 1;
        whiteImage.height = 1;
        whiteImage.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        whiteImage.rowPitch = 4;
        whiteImage.slicePitch = 4;

        uint8_t* pixels = new uint8_t[4]{ 255, 255, 255, 255 };
        whiteImage.pixels = pixels;

        image.InitializeFromImage(whiteImage);
        mipImages.InitializeFromImage(whiteImage);
        delete pixels;
        return mipImages;
    }

    //ミニマップの作成
    hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
    assert(SUCCEEDED(hr));

    return mipImages;
}


std::wstring DirectXCommon::ConvertString(const std::string& str) {
    if (str.empty()) {

        return std::wstring();
    }
    auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
    if (sizeNeeded == 0) {
        return std::wstring();
    }
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
    return result;
}
void DirectXCommon::FlushCommandQueue(bool reset) {
    // コマンドリストをクローズする
    HRESULT hr = commandList_->Close();
    assert(SUCCEEDED(hr));

    // GPUにコマンドリストの実行を指示する
    ID3D12CommandList* commandLists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, commandLists);

    // fenceの値もインクリメントしておく
    fenceValue_++;
    // GPUにSignalコマンドを送信する
    commandQueue_->Signal(fence_.Get(), fenceValue_);

    // fenceの値が指定したSignal値に到達するのを待つ
    if (fence_->GetCompletedValue() < fenceValue_) {
        // イベントハンドルの取得
        // イベントはfenceのSignal値に到達したときに発行されるように指定
        fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
        // イベント待機
        WaitForSingleObject(fenceEvent_, INFINITE);
    }


    // resetがtrueの場合のみ、アロケータとリストをリセットする
    if (reset) {
        // 次のコマンドリストを準備
        hr = commandAllocator_->Reset();
        assert(SUCCEEDED(hr));
        hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
        assert(SUCCEEDED(hr));
    }
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DirectXCommon::CreateDescriptorHeap(
    D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    UINT numDescriptors,
    bool shaderVisible)
{
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
    descriptorHeapDesc.Type = heapType;
    descriptorHeapDesc.NumDescriptors = numDescriptors;
    descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT hr = device_->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
    assert(SUCCEEDED(hr));
    return descriptorHeap;
}