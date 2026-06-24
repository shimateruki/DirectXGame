
#include "DirectXCommon.h"
#include "WinApp.h"
#include <algorithm>
#include <cassert>
#include <format>
#include <vector>
#include <dxcapi.h>
#include <fstream>
#include <thread>
#include "SRVManager.h"
#include "PostEffect.h"
#include"ImguiManager.h"

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
// =========================================================
// GPUプロファイラの初期化 
// =========================================================

// 1. クエリヒープの作成（開始と終了の2つのタイムスタンプを保存できる箱）
	D3D12_QUERY_HEAP_DESC queryHeapDesc{};
	queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	queryHeapDesc.Count = kMaxGpuQueries;
	HRESULT hr = device_->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&queryHeap_));
	assert(SUCCEEDED(hr));

	// 2. リードバックバッファの作成
	D3D12_HEAP_PROPERTIES heapProp{};
	heapProp.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC resDesc{};
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resDesc.Width = sizeof(uint64_t) * kMaxGpuQueries;
	resDesc.Height = 1;
	resDesc.DepthOrArraySize = 1;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	hr = device_->CreateCommittedResource(
		&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(&queryResultBuffer_));
	assert(SUCCEEDED(hr));

	// 3. GPUのタイマーの周波数（1秒間に何回カウントするか）を取得
	commandQueue_->GetTimestampFrequency(&gpuFrequency_);
}

void DirectXCommon::PreDraw() {
	frameCount_++;
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

	// SetRenderClearColorで指定された色でクリアします。
	commandList_->ClearRenderTargetView(rtvHandle, clearColor_, 0, nullptr);

	// 深度バッファをクリアします。値を1.0f(最も遠い)に設定します。
	commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// ビューポート（描画する画面内の領域）を設定します。
	commandList_->RSSetViewports(1, &viewport_);
	// シザー矩形（ピクセルを描画する範囲を限定する矩形）を設定します。
	commandList_->RSSetScissorRects(1, &scissorRect_);

	//SRVManagerからデスクリプタヒープを取得して設定
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
	vertResoucesDesc.Width = sizeInBytes;
	//バッファの場合は1にする
	vertResoucesDesc.Height = 1;
	vertResoucesDesc.DepthOrArraySize = 1;
	vertResoucesDesc.MipLevels = 1;
	vertResoucesDesc.SampleDesc.Count = 1;
	//バッファの場合はこれをする決まり
	vertResoucesDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	//実際に頂点リソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
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
	// VSync ON (Present(1,0)) の場合、GPU側が既に60FPSを保証しているので
	// CPU側のsleep待ちは不要（二重同期によるジッターを防止）
	if (useVSync_) {
		reference_ = std::chrono::steady_clock::now();
		return;
	}

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
	const std::wstring& filePath, const wchar_t* profile, const wchar_t* entryPoint)
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
		  filePath.c_str(), L"-E", entryPoint, L"-T", profile,
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
	if (!commandQueue_ || !fence_) return;

	// --- GPUの完了を確実に待つ ---
	fenceValue_++;
	HRESULT hr = commandQueue_->Signal(fence_.Get(), fenceValue_);
	assert(SUCCEEDED(hr));

	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}

	// --- コマンドリストが開いていたら安全に閉じる ---
	hr = commandList_->Close();
	// Closeに失敗しても無視

	// --- 残りのリソースを解放 ---
	fenceEvent_ = nullptr;
	fence_.Reset();
	commandList_.Reset();
	commandAllocator_.Reset();
	commandQueue_.Reset();
	swapChain_.Reset();
	device_.Reset();
	dxgiFactory_.Reset();

	Log("[DirectXCommon] Finalized successfully.\n");
}


void DirectXCommon::PostDraw() {


	// リソースバリアを再度設定します。
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList_->ResourceBarrier(1, &barrier);

    // フレームの最後で、記録した全クエリを一括でリゾルブする
    if (nextQueryIndex_ > 0) {
        commandList_->ResolveQueryData(
            queryHeap_.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            0,
            nextQueryIndex_,
            queryResultBuffer_.Get(),
            0
        );
    }

	// コマンドリストへのコマンドの記録を終了します。
	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	// コマンドリストの配列を作成します
	ID3D12CommandList* commandLists[] = { commandList_.Get() };

	// コマンドキューにコマンドリストを投入し、GPUに実行を指示します。
	commandQueue_->ExecuteCommandLists(1, commandLists);
	swapChain_->Present(1, 0);




	// フェンスの目標値をインクリメントします。
	fenceValue_++;
	// GPUがコマンドキューのここまで処理を終えたら、フェンスに新しい値を書き込むよう指示します。
	commandQueue_->Signal(fence_.Get(), fenceValue_);

	// GPUの処理がまだ完了していないかチェックします。
	if (fence_->GetCompletedValue() < fenceValue_) {
		// GPUが指定したフェンス値に達したときに発火するイベントを設定します。
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
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
	rtvDescriptorHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, (UINT)backBufferCount_, false);
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
	depthStencilResource_ = CreateDepthStencilTextureResource(WinApp::kClientWidth, WinApp::kClientHeight);

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
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

	resourceDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;

	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;

	// ★ここはDSV用として固定のまま
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));

	return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateTextureResource(const DirectX::TexMetadata& metadata)
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


DirectX::ScratchImage DirectXCommon::LoadTexture(const std::string& filePath, bool forceSRGB)
{
	DirectX::ScratchImage image{};
	DirectX::ScratchImage mipImages{};
	std::wstring filePathW = DirectXCommon::ConvertString(filePath);
	HRESULT hr = S_FALSE;

	// =========================================================
	//  拡張子が .dds かどうかで読み込み関数を分ける！
	// =========================================================
	if (filePath.size() >= 4 && filePath.substr(filePath.size() - 4) == ".dds")
	{
		// DDS用の読み込み関数
		hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);

		// DDSの場合はすでにミップマップやキューブマップが構築されていることが多いので、
		// そのまま返す（ミップマップ生成をスキップ）
		if (SUCCEEDED(hr)) {
			return image;
		}
	} else
	{
		// PNG, JPGなどの一般的な画像用。PBRのNormal/ORMなどはlinearのまま読み込みます。
		const auto wicFlags = forceSRGB ? DirectX::WIC_FLAGS_FORCE_SRGB : DirectX::WIC_FLAGS_IGNORE_SRGB;
		hr = DirectX::LoadFromWICFile(filePathW.c_str(), wicFlags, nullptr, image);
	}

	// テクスチャが読み込まれなかった場合は白色のテクスチャを張る
	if (FAILED(hr))
	{
		D3D12_RESOURCE_DESC materialData;
		materialData.Width = 1;
		materialData.Height = 1;
		materialData.DepthOrArraySize = 1;
		materialData.MipLevels = 1;
		materialData.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		materialData.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		// 白色のテクスチャを作成
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

		// ★ついでにメモリリーク修正: 配列をnewしたので delete[] が正解です
		delete[] pixels;
		return mipImages;
	}

	// ミニマップの作成 (DDS以外の場合のみ実行される)
	const auto filterFlags = forceSRGB ? DirectX::TEX_FILTER_SRGB : DirectX::TEX_FILTER_DEFAULT;
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), filterFlags, 0, mipImages);
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
	// --- 安全にコマンドリストを閉じる ---
	HRESULT hr = S_OK;
	hr = commandList_->Close();
	if (FAILED(hr)) {
	}

	// --- コマンドリストを実行 ---
	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(_countof(commandLists), commandLists);

	// --- フェンスシグナル送信 ---
	fenceValue_++;
	hr = commandQueue_->Signal(fence_.Get(), fenceValue_);
	assert(SUCCEEDED(hr) && "Failed to signal command queue fence");

	// --- GPUが完了するまで待機 ---
	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}

	// --- リセット処理（必要な場合のみ）---
	if (reset) {
		// まだコマンドリストがOpen状態の場合はここではResetしない
		// GPU完了を待っているので安全にリセットできる
		hr = commandAllocator_->Reset();
		assert(SUCCEEDED(hr) && "CommandAllocator reset failed");

		hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
		assert(SUCCEEDED(hr) && "CommandList reset failed");
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

/// <summary>
/// 標準的な深度ステンシルデスクリプタを取得する
/// </summary>
D3D12_DEPTH_STENCIL_DESC DirectXCommon::GetDefaultDepthStencilDesc() const {
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	// Depthの機能を有効化
	depthStencilDesc.DepthEnable = TRUE;
	// 書き込みします
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	// 比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	// Stencilの機能は利用しない
	depthStencilDesc.StencilEnable = FALSE;

	return depthStencilDesc;
}

/// <summary>
/// 深度ステンシルビューのフォーマットを取得する
/// </summary>
DXGI_FORMAT DirectXCommon::GetDSVFormat() const {

	return DXGI_FORMAT_D24_UNORM_S8_UINT;
}



void DirectXCommon::WaitForGPUAndReset() {
	// --- フェンス未生成または破棄済みならスキップ ---
	if (!fence_ || !commandQueue_ || !commandAllocator_ || !commandList_) {
		Log("[DirectXCommon] WaitForGPUAndReset skipped (resources not ready)\n");
		return;
	}

	// --- GPUが完了するまで待機 ---
	fenceValue_++;
	HRESULT hr = commandQueue_->Signal(fence_.Get(), fenceValue_);
	assert(SUCCEEDED(hr));

	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}



	// --- 再利用可能な状態にリセット ---
	hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));
}

void DirectXCommon::WaitForGPUIdle() {
	if (!fence_ || !commandQueue_) {
		return;
	}

	fenceValue_++;
	HRESULT hr = commandQueue_->Signal(fence_.Get(), fenceValue_);
	assert(SUCCEEDED(hr));

	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}
}

void DirectXCommon::CreateRenderTexture() {
	// 1. リソース設定
	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.Width = WinApp::kClientWidth;
	resDesc.Height = WinApp::kClientHeight;
	resDesc.MipLevels = 1;
	resDesc.DepthOrArraySize = 1;

	// HDRフォーマット (歪み用コピー先もこれに合わせる)
	resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

	resDesc.SampleDesc.Count = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	// 2. クリアカラー
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	clearValue.Color[0] = clearColor_[0];
	clearValue.Color[1] = clearColor_[1];
	clearValue.Color[2] = clearColor_[2];
	clearValue.Color[3] = clearColor_[3];

	// 3. 生成 
	D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_DEFAULT };
	HRESULT hr = device_->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
		IID_PPV_ARGS(&renderTexture_)
	);
	assert(SUCCEEDED(hr));

	// 4. RTV (Render Target View) の作成
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 1;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	hr = device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtRtvHeap_));
	assert(SUCCEEDED(hr));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device_->CreateRenderTargetView(renderTexture_.Get(), &rtvDesc, rtRtvHeap_->GetCPUDescriptorHandleForHeapStart());

	// 5. SRV (Shader Resource View) の作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	renderTextureSrvHandle_ = SRVManager::GetInstance()->CreateSRV(renderTexture_.Get(), srvDesc);

	// =======================================================
	// ★追加: ディストーション用の背景コピーテクスチャ (GrabTexture) の生成
	// =======================================================
	// 6. GrabTexture本体の生成 (設定は renderTexture_ と全く同じ)
	HRESULT hrGrab = device_->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
		IID_PPV_ARGS(&grabTexture_)
	);
	assert(SUCCEEDED(hrGrab));

	// 7. GrabTexture をシェーダーで読むための SRV 作成
	grabSrvHandle_ = SRVManager::GetInstance()->CreateSRV(grabTexture_.Get(), srvDesc);
}

void DirectXCommon::PreDrawRenderTexture() {
	HRESULT hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));

	// --- 以下、既存のコード ---
	// 1. バリア：読むモード -> 描くモード
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = renderTexture_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList_->ResourceBarrier(1, &barrier);

	// 2. 描画先を「レンダーテクスチャ」に切り替える
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtRtvHeap_->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

	// 3. クリア
	commandList_->ClearRenderTargetView(rtvHandle, clearColor_, 0, nullptr);
	commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// 4. ビューポート設定
	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissorRect_);

	// 5. ヒープ設定
	ID3D12DescriptorHeap* descriptorHeaps[] = { SRVManager::GetInstance()->GetDescriptorHeap() };
	commandList_->SetDescriptorHeaps(1, descriptorHeaps);
}
// 描画終了：ImGuiが読めるように戻します
void DirectXCommon::PostDrawRenderTexture() {
	// バリア：描くモード -> 読むモード
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = renderTexture_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList_->ResourceBarrier(1, &barrier);
}
void DirectXCommon::PreDrawBackBuffer() {
	//  バックバッファのインデックスを取得 (これが必要)
	backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

	// ★追加: リソースバリア (表示モード -> 書き込みモードへ変更)
	// これがないとGPUはバックバッファへの描画を無視します
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList_->ResourceBarrier(1, &barrier);

	// 1. バックバッファのハンドルを取得
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	rtvHandle.ptr = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart().ptr +
		(backBufferIndex_ * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

	// 2. 描画先をセット
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

	// SetRenderClearColorで指定された色でクリアします。
	commandList_->ClearRenderTargetView(rtvHandle, clearColor_, 0, nullptr);
	// ImGuiが深度を使う場合はクリアが必要
	commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// 3. ビューポートなども画面サイズに戻す
	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissorRect_);

	// 4. ImGui用にヒープをセットし直す
	ID3D12DescriptorHeap* descriptorHeaps[] = { SRVManager::GetInstance()->GetDescriptorHeap() };
	commandList_->SetDescriptorHeaps(1, descriptorHeaps);
}

void DirectXCommon::CreateShadowMap() {
	// 1. デスクリプタヒープの作成 (DSV用)
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&shadowDsvHeap_));
	assert(SUCCEEDED(hr));

	// 2. リソースの設定 (DSVとSRVの両方で使えるように TYPELESS にする)
	D3D12_RESOURCE_DESC resDesc{};
	resDesc.Width = kShadowMapWidth;
	resDesc.Height = kShadowMapHeight;
	resDesc.MipLevels = 1;
	resDesc.DepthOrArraySize = 1;
	resDesc.Format = DXGI_FORMAT_R32_TYPELESS; // ★重要: 型無しフォーマット
	resDesc.SampleDesc.Count = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	// 3. ヒーププロパティとクリア値
	D3D12_HEAP_PROPERTIES heapProps{ D3D12_HEAP_TYPE_DEFAULT };
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = DXGI_FORMAT_D32_FLOAT; // クリア時は深度フォーマット
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	// 4. リソースの生成 (初期状態は読み込み可能な SRV 状態にしておく)
	hr = device_->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
		IID_PPV_ARGS(&shadowMapResource_)
	);
	assert(SUCCEEDED(hr));

	// 5. DSV (Depth Stencil View) の作成
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT; // 深度として解釈
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	device_->CreateDepthStencilView(shadowMapResource_.Get(), &dsvDesc, shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart());

	// 6. SRV (Shader Resource View) の作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT; // テクスチャとして読むときはFloat
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;

	// SRVManagerに登録してハンドルをもらう
	shadowMapSrvHandle_ = SRVManager::GetInstance()->CreateSRV(shadowMapResource_.Get(), srvDesc);

	Log("Created Shadow Map successfully.\n");
}

void DirectXCommon::PreDrawShadow() {
	// 1. バリア：画像として読むモード -> 深度を書き込むモードへ
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = shadowMapResource_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	commandList_->ResourceBarrier(1, &barrier);

	// 2. 描画先を「シャドウマップのDSV」のみに設定（RTVは無し）
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart();
	commandList_->OMSetRenderTargets(0, nullptr, false, &dsvHandle);

	// 3. 画面を真っ白（深度1.0f）にクリアする！
	commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// 4. ビューポートとシザー矩形をシャドウマップの解像度に合わせる
	D3D12_VIEWPORT viewport{};
	viewport.Width = (float)kShadowMapWidth;
	viewport.Height = (float)kShadowMapHeight;
	viewport.MaxDepth = 1.0f;
	commandList_->RSSetViewports(1, &viewport);

	D3D12_RECT scissorRect{};
	scissorRect.right = kShadowMapWidth;
	scissorRect.bottom = kShadowMapHeight;
	commandList_->RSSetScissorRects(1, &scissorRect);
}

void DirectXCommon::PostDrawShadow() {
	// 1. バリア：深度を書き込むモード -> 画像として読むモードに戻す
	// （これがないとImGuiやメインシェーダーで読めなくてエラーになります）
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = shadowMapResource_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList_->ResourceBarrier(1, &barrier);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtRtvHeap_->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

	// ビューポートとシザー矩形もメイン画面用に戻す
	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissorRect_);
}

void DirectXCommon::StartGpuProfile(const std::string& name) {
    // まだ登録されていない名前なら、新しいインデックス（開始・終了ペア）を割り当てる
    if (!gpuProfileMap_.contains(name)) {
        if (nextQueryIndex_ + 2 > kMaxGpuQueries) return;
        gpuProfileMap_[name] = nextQueryIndex_;
        nextQueryIndex_ += 2;
    }

    uint32_t startIndex = gpuProfileMap_[name];
    commandList_->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, startIndex);
}

void DirectXCommon::EndGpuProfile(const std::string& name) {
    if (!gpuProfileMap_.contains(name)) return;

    uint32_t startIndex = gpuProfileMap_[name];
    uint32_t endIndex = startIndex + 1;
    commandList_->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, endIndex);

    // ※ 個別に Resolve せず、フレームの最後で一括で行うように変更
}

void DirectXCommon::ResetGpuProfiles() {
    gpuProfileMap_.clear();
    nextQueryIndex_ = 0;
}

#include "ProfilerManager.h"

void DirectXCommon::ReadAllGpuProfiles() {
    // 前フレームの結果を読み取る
    D3D12_RANGE readRange = { 0, sizeof(uint64_t) * nextQueryIndex_ };
    uint64_t* mappedData = nullptr;
    
    // 読み取り範囲を指定してMap（キャッシュの無効化を確実にする）
    HRESULT hr = queryResultBuffer_->Map(0, &readRange, reinterpret_cast<void**>(&mappedData));
    if (SUCCEEDED(hr)) {
        for (auto& pair : gpuProfileMap_) {
            const std::string& name = pair.first;
            uint32_t startIndex = pair.second;

            uint64_t start = mappedData[startIndex];
            uint64_t end = mappedData[startIndex + 1];

            if (end > start && gpuFrequency_ > 0) {
                float timeMs = static_cast<float>(end - start) / static_cast<float>(gpuFrequency_) * 1000.0f;
                ProfilerManager::GetInstance()->RecordGpuTime(name, timeMs);

                if (name == "Total") {
                    gpuDrawTimeMs_ = timeMs;
                }
            }
        }
        
        // 読み終わったら、書き込みは行わないので Empty Range で Unmap
        D3D12_RANGE writtenRange = { 0, 0 };
        queryResultBuffer_->Unmap(0, &writtenRange);
    }
}
void DirectXCommon::CreateDepthSrv() {
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// ここでSRVを作る！
	depthSrvHandle_ = SRVManager::GetInstance()->CreateSRV(depthStencilResource_.Get(), srvDesc);
}

void DirectXCommon::PreDrawLocalFog() {
	// バリア：深度「書き込みモード」 -> 「読み込みモード (画像)」に変換！
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = depthStencilResource_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList_->ResourceBarrier(1, &barrier);
	// =================================================================
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtRtvHeap_->GetCPUDescriptorHandleForHeapStart();
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, nullptr); // ← DSVを nullptr にする！
}

void DirectXCommon::PostDrawLocalFog() {
	// バリア：「読み込みモード」 -> 元の「書き込みモード」に戻す！
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = depthStencilResource_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	commandList_->ResourceBarrier(1, &barrier);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtRtvHeap_->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle); // ← DSVを復活！
}

void DirectXCommon::UpdateGrabTexture() {
	// バリア: RTV(描画先) -> COPY_SOURCE(コピー元) へ
	D3D12_RESOURCE_BARRIER barrier1{};
	barrier1.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier1.Transition.pResource = renderTexture_.Get();
	barrier1.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier1.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	commandList_->ResourceBarrier(1, &barrier1);

	// バリア: SRV(画像) -> COPY_DEST(コピー先) へ
	D3D12_RESOURCE_BARRIER barrier2{};
	barrier2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier2.Transition.pResource = grabTexture_.Get();
	barrier2.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier2.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	commandList_->ResourceBarrier(1, &barrier2);

	// ★コピー実行！(現在の画面を丸ごと保存)
	commandList_->CopyResource(grabTexture_.Get(), renderTexture_.Get());

	// バリアを元の状態に戻す
	barrier1.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	barrier1.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList_->ResourceBarrier(1, &barrier1);

	barrier2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier2.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList_->ResourceBarrier(1, &barrier2);
}

void DirectXCommon::ResizeSwapChain(int32_t width, int32_t height) {
	width = (std::max)(width, 640);
	height = (std::max)(height, 360);

	if (width == WinApp::kClientWidth && height == WinApp::kClientHeight && swapChainResources_[0]) {
		return;
	}

	// 1. GPUの完了を待つ。ここではコマンドリストをResetしない。
	WaitForGPUIdle();

	// 2. 今持っているバックバッファと深度バッファを一度リセットする。
	for (size_t i = 0; i < backBufferCount_; ++i) {
		swapChainResources_[i].Reset();
	}
	depthStencilResource_.Reset();

	// 3. スワップチェーン自体のサイズを変更
	DXGI_SWAP_CHAIN_DESC1 desc{};
	swapChain_->GetDesc1(&desc);
	HRESULT hr = swapChain_->ResizeBuffers(
		(UINT)backBufferCount_,
		(UINT)width,
		(UINT)height,
		desc.Format,
		desc.Flags
	);
	assert(SUCCEEDED(hr));

	WinApp::kClientWidth = width;
	WinApp::kClientHeight = height;

	for (UINT i = 0; i < backBufferCount_; ++i) {
		hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&swapChainResources_[i]));
		assert(SUCCEEDED(hr));

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
		rtvHandle.ptr += i * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		device_->CreateRenderTargetView(swapChainResources_[i].Get(), &rtvDesc, rtvHandle);
	}

	// 4. DSV（深度バッファ）を新しいサイズで再作成する。
	depthStencilResource_ = CreateDepthStencilTextureResource(width, height);
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device_->CreateDepthStencilView(depthStencilResource_.Get(), &dsvDesc, dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());

	// 5. ビューポートとシザー矩形も更新する。
	viewport_.TopLeftX = 0.0f;
	viewport_.TopLeftY = 0.0f;
	viewport_.Width = static_cast<float>(width);
	viewport_.Height = static_cast<float>(height);
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;
	scissorRect_.left = 0;
	scissorRect_.top = 0;
	scissorRect_.right = width;
	scissorRect_.bottom = height;

	CreateDepthSrv();
	CreateRenderTexture();
	PostEffect::GetInstance()->ResizeRenderTextures(width, height);
}

void DirectXCommon::RequestResize(int32_t width, int32_t height) {
	if (width <= 0 || height <= 0) {
		return;
	}

	pendingResizeWidth_ = width;
	pendingResizeHeight_ = height;
	pendingResize_ = true;
}

void DirectXCommon::ProcessPendingResize() {
	if (!pendingResize_) {
		return;
	}

	const int32_t width = pendingResizeWidth_;
	const int32_t height = pendingResizeHeight_;
	pendingResize_ = false;

	ResizeSwapChain(width, height);
}
