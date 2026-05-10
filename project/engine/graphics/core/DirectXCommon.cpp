
#include "DirectXCommon.h"
#include "WinApp.h"
#include <cassert>
#include <format>
#include <vector>
#include <dxcapi.h>
#include <fstream>
#include <thread>
#include "SRVManager.h"
#include"ImguiManager.h"
#include <mmsystem.h>

// 繝ｭ繧ｰ蜃ｺ蜉帷畑縺ｮ繝倥Ν繝代・髢｢謨ｰ・医げ繝ｭ繝ｼ繝舌Ν・・
void Log(const std::string& message) { OutputDebugStringA(message.c_str()); }
std::string ConvertString(const std::wstring& str);

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "winmm.lib")

DirectXCommon* DirectXCommon::GetInstance() {
	static DirectXCommon instance;
	return &instance;
}

void DirectXCommon::Initialize(WinApp* winApp) {
	assert(winApp);
	winApp_ = winApp;
	timeBeginPeriod(1);
	InitalaizeFixFPS();
	// 蜷・ｨｮ蛻晄悄蛹門・逅・
	InitializeDXGIDevice();
	CreateCommand();
	CreateSwapChain();
	CreateRTV();
	CreateDSV();
	CreateFence();

	// 繝薙Η繝ｼ繝昴・繝医→繧ｷ繧ｶ繝ｼ遏ｩ蠖｢縺ｮ險ｭ螳・
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
// GPU繝励Ο繝輔ぃ繧､繝ｩ縺ｮ蛻晄悄蛹・
// =========================================================

// 1. 繧ｯ繧ｨ繝ｪ繝偵・繝励・菴懈・・磯幕蟋九→邨ゆｺ・・2縺､縺ｮ繧ｿ繧､繝繧ｹ繧ｿ繝ｳ繝励ｒ菫晏ｭ倥〒縺阪ｋ邂ｱ・・
	D3D12_QUERY_HEAP_DESC queryHeapDesc{};
	queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	queryHeapDesc.Count = 2;
	HRESULT hr = device_->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&queryHeap_));
	assert(SUCCEEDED(hr));

	// 2. 繝ｪ繝ｼ繝峨ヰ繝・け繝舌ャ繝輔ぃ縺ｮ菴懈・
	D3D12_HEAP_PROPERTIES heapProp{};
	heapProp.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC resDesc{};
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resDesc.Width = sizeof(uint64_t) * 2;
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

	// 3. GPU縺ｮ繧ｿ繧､繝槭・縺ｮ蜻ｨ豕｢謨ｰ・・遘帝俣縺ｫ菴募屓繧ｫ繧ｦ繝ｳ繝医☆繧九°・峨ｒ蜿門ｾ・
	commandQueue_->GetTimestampFrequency(&gpuFrequency_);
}

void DirectXCommon::PreDraw() {
	// 繧ｳ繝槭Φ繝峨い繝ｭ繧ｱ繝ｼ繧ｿ繧偵Μ繧ｻ繝・ヨ縺励∪縺吶・
	HRESULT hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));

	// 繧ｳ繝槭Φ繝峨Μ繧ｹ繝医ｒ繝ｪ繧ｻ繝・ヨ縺励∪縺吶・
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));

	// 迴ｾ蝨ｨ謠冗判蟇ｾ雎｡縺ｨ縺ｪ縺｣縺ｦ縺・ｋ繝舌ャ繧ｯ繝舌ャ繝輔ぃ縺ｮ繧､繝ｳ繝・ャ繧ｯ繧ｹ繧貞叙蠕励＠縺ｾ縺吶・
	backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

	// 繝ｪ繧ｽ繝ｼ繧ｹ繝舌Μ繧｢繧定ｨｭ螳壹＠縺ｾ縺吶・
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList_->ResourceBarrier(1, &barrier);

	// 繝ｬ繝ｳ繝繝ｼ繧ｿ繝ｼ繧ｲ繝・ヨ繝薙Η繝ｼ(RTV)縺ｮ繝・ぅ繧ｹ繧ｯ繝ｪ繝励ち繝上Φ繝峨Ν縺ｮ繝昴う繝ｳ繧ｿ繧貞叙蠕励＠縺ｾ縺吶・
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	rtvHandle.ptr = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart().ptr + (backBufferIndex_ * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));

	// 豺ｱ蠎ｦ繧ｹ繝・Φ繧ｷ繝ｫ繝薙Η繝ｼ(DSV)縺ｮ繝・ぅ繧ｹ繧ｯ繝ｪ繝励ち繝上Φ繝峨Ν縺ｮ繝昴う繝ｳ繧ｿ繧貞叙蠕励＠縺ｾ縺吶・
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

	// 蜃ｺ蜉帙・繝ｼ繧ｸ繝｣(OM)繧ｹ繝・・繧ｸ縺ｫ縲∵緒逕ｻ蜈医→縺ｪ繧騎TV縺ｨDSV繧定ｨｭ螳壹＠縺ｾ縺吶・
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

	// 逕ｻ髱｢繧呈欠螳壹＠縺溯牡縺ｧ繧ｯ繝ｪ繧｢縺励∪縺・(萓・ 髱偵∩縺後°縺｣縺溽・濶ｲ)縲・
	float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
	commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	// 豺ｱ蠎ｦ繝舌ャ繝輔ぃ繧偵け繝ｪ繧｢縺励∪縺吶ょ､繧・.0f(譛繧る□縺・縺ｫ險ｭ螳壹＠縺ｾ縺吶・
	commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// 繝薙Η繝ｼ繝昴・繝茨ｼ域緒逕ｻ縺吶ｋ逕ｻ髱｢蜀・・鬆伜沺・峨ｒ險ｭ螳壹＠縺ｾ縺吶・
	commandList_->RSSetViewports(1, &viewport_);
	// 繧ｷ繧ｶ繝ｼ遏ｩ蠖｢・医ヴ繧ｯ繧ｻ繝ｫ繧呈緒逕ｻ縺吶ｋ遽・峇繧帝剞螳壹☆繧狗洸蠖｢・峨ｒ險ｭ螳壹＠縺ｾ縺吶・
	commandList_->RSSetScissorRects(1, &scissorRect_);

	//SRVManager縺九ｉ繝・せ繧ｯ繝ｪ繝励ち繝偵・繝励ｒ蜿門ｾ励＠縺ｦ險ｭ螳・
	ID3D12DescriptorHeap* descriptorHeaps[] = { SRVManager::GetInstance()->GetDescriptorHeap() };
	commandList_->SetDescriptorHeaps(1, descriptorHeaps);
}




Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateBufferResource(size_t sizeInBytes)
{
	//鬆らせ縺ｨ繝ｪ繧ｽ繝ｼ繧ｹ逕ｨ縺ｮ繝偵・繝苓ｨｭ螳・
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	//鬆らせ繝ｪ繧ｽ繝ｼ繧ｹ縺ｮ險ｭ螳・
	D3D12_RESOURCE_DESC vertResoucesDesc{};
	//繝舌ャ繝輔ぃ繝ｼ繝ｪ繧ｽ繝ｼ繧ｹ繝・け繧ｹ繝√Ε縺ｮ蝣ｴ蜷医・蛻･縺ｮ謖・ｮ壹ｒ縺吶ｋ
	vertResoucesDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertResoucesDesc.Width = sizeInBytes;
	//繝舌ャ繝輔ぃ縺ｮ蝣ｴ蜷医・1縺ｫ縺吶ｋ
	vertResoucesDesc.Height = 1;
	vertResoucesDesc.DepthOrArraySize = 1;
	vertResoucesDesc.MipLevels = 1;
	vertResoucesDesc.SampleDesc.Count = 1;
	//繝舌ャ繝輔ぃ縺ｮ蝣ｴ蜷医・縺薙ｌ繧偵☆繧区ｱｺ縺ｾ繧・
	vertResoucesDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	//螳滄圀縺ｫ鬆らせ繝ｪ繧ｽ繝ｼ繧ｹ繧剃ｽ懊ｋ
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device_->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
		&vertResoucesDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));
	return resource;
}


void DirectXCommon::InitalaizeFixFPS()
{
	// 迴ｾ蝨ｨ縺ｮ譎ょ綾繧貞叙蠕励＠縺ｦ險倬鹸
	reference_ = std::chrono::steady_clock::now();
}

void DirectXCommon::UpdateFixFPS()
{
	// 1/60遘偵・縺｣縺溘ｊ縺ｮ譎る俣
	const std::chrono::microseconds kMinTimer(uint64_t(1000000.0f / 60.0f));

	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - reference_);
	std::chrono::microseconds waitTime = kMinTimer - elapsed;

	if (waitTime.count() > 0)
	{
		// 蠕・ｩ滓凾髢薙′2ms莉･荳翫≠繧句ｴ蜷医・縲∝ｮ牙・繝槭・繧ｸ繝ｳ繧貞叙縺｣縺ｦ繧ｹ繝ｪ繝ｼ繝・
		if (waitTime.count() > 2000)
		{
			std::this_thread::sleep_for(std::chrono::microseconds(waitTime.count() - 2000));
		}
		// 谿九ｊ縺ｮ蠕ｮ蟆上↑譎る俣縺ｯ遨ｺ繝ｫ繝ｼ繝暦ｼ医せ繝斐Φ繝ｭ繝・け・峨〒豁｣遒ｺ縺ｫ蠕・▽
		while (std::chrono::steady_clock::now() - reference_ < kMinTimer)
		{
			// 菴輔ｂ縺帙★蝗槭☆
		}
	}

	// 迴ｾ蝨ｨ縺ｮ譎ょ綾繧貞・蜿門ｾ・
	reference_ = std::chrono::steady_clock::now();
}


Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompileShader(
	const std::wstring& filePath, const wchar_t* profile, const wchar_t* entryPoint)
{
	// 繝ｭ繧ｰ蜃ｺ蜉帷畑縺ｮ繧ｹ繝医Μ繝ｼ繝繧呈ｺ門ｙ
	std::ofstream logStream("shader_compile.log", std::ios_base::app);

	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
	// 繝｡繝ｳ繝仙､画焚縺ｮdxcUtils_繧剃ｽｿ縺・
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
	// 繝｡繝ｳ繝仙､画焚縺ｮdxcCompiler_, includeHandler_繧剃ｽｿ縺・
	hr = dxcCompiler_->Compile(
		&shaderSourceBuffer, arguments, _countof(arguments),
		includeHandler_.Get(), IID_PPV_ARGS(&shaderResult));
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		// Log(shaderError->GetStringPointer()); // 繝ｭ繧ｰ髢｢謨ｰ繧貞他縺ｳ蜃ｺ縺・
		assert(false);
	}

	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));

	return shaderBlob;
}

void DirectXCommon::Finalize() {
	if (!commandQueue_ || !fence_) return;

	// --- GPU縺ｮ螳御ｺ・ｒ遒ｺ螳溘↓蠕・▽ ---
	fenceValue_++;
	HRESULT hr = commandQueue_->Signal(fence_.Get(), fenceValue_);
	assert(SUCCEEDED(hr));

	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}

	// --- 繧ｳ繝槭Φ繝峨Μ繧ｹ繝医′髢九＞縺ｦ縺・◆繧牙ｮ牙・縺ｫ髢峨§繧・---
	hr = commandList_->Close();
	// Close縺ｫ螟ｱ謨励＠縺ｦ繧ら┌隕・

	// --- 谿九ｊ縺ｮ繝ｪ繧ｽ繝ｼ繧ｹ繧定ｧ｣謾ｾ ---
	fenceEvent_ = nullptr;
	fence_.Reset();
	commandList_.Reset();
	commandAllocator_.Reset();
	commandQueue_.Reset();
	swapChain_.Reset();
	device_.Reset();
	dxgiFactory_.Reset();
	timeEndPeriod(1);
	Log("[DirectXCommon] Finalized successfully.\n");
}


void DirectXCommon::PostDraw() {


	// 繝ｪ繧ｽ繝ｼ繧ｹ繝舌Μ繧｢繧貞・蠎ｦ險ｭ螳壹＠縺ｾ縺吶・
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList_->ResourceBarrier(1, &barrier);

	// 繧ｳ繝槭Φ繝峨Μ繧ｹ繝医∈縺ｮ繧ｳ繝槭Φ繝峨・險倬鹸繧堤ｵゆｺ・＠縺ｾ縺吶・
	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	// 繧ｳ繝槭Φ繝峨Μ繧ｹ繝医・驟榊・繧剃ｽ懈・縺励∪縺・
	ID3D12CommandList* commandLists[] = { commandList_.Get() };

	// 繧ｳ繝槭Φ繝峨く繝･繝ｼ縺ｫ繧ｳ繝槭Φ繝峨Μ繧ｹ繝医ｒ謚募・縺励；PU縺ｫ螳溯｡後ｒ謖・､ｺ縺励∪縺吶・
	commandQueue_->ExecuteCommandLists(1, commandLists);
	swapChain_->Present(0, 0);




	// 繝輔ぉ繝ｳ繧ｹ縺ｮ逶ｮ讓吝､繧偵う繝ｳ繧ｯ繝ｪ繝｡繝ｳ繝医＠縺ｾ縺吶・
	fenceValue_++;
	// GPU縺後さ繝槭Φ繝峨く繝･繝ｼ縺ｮ縺薙％縺ｾ縺ｧ蜃ｦ逅・ｒ邨ゅ∴縺溘ｉ縲√ヵ繧ｧ繝ｳ繧ｹ縺ｫ譁ｰ縺励＞蛟､繧呈嶌縺崎ｾｼ繧繧医≧謖・､ｺ縺励∪縺吶・
	commandQueue_->Signal(fence_.Get(), fenceValue_);

	// GPU縺ｮ蜃ｦ逅・′縺ｾ縺螳御ｺ・＠縺ｦ縺・↑縺・°繝√ぉ繝・け縺励∪縺吶・
	if (fence_->GetCompletedValue() < fenceValue_) {
		// GPU縺梧欠螳壹＠縺溘ヵ繧ｧ繝ｳ繧ｹ蛟､縺ｫ驕斐＠縺溘→縺阪↓逋ｺ轣ｫ縺吶ｋ繧､繝吶Φ繝医ｒ險ｭ螳壹＠縺ｾ縺吶・
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

	// ロード専用のコマンドリスト作成
	hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&loadCommandAllocator_));
	assert(SUCCEEDED(hr));
	hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, loadCommandAllocator_.Get(), nullptr, IID_PPV_ARGS(&loadCommandList_));
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

	// 笘・％縺薙・DSV逕ｨ縺ｨ縺励※蝗ｺ螳壹・縺ｾ縺ｾ
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
	//metadata繧定ｻｸ縺ｫResouces縺ｮ險ｭ螳・
	D3D12_RESOURCE_DESC resouceDesc{};
	resouceDesc.Width = UINT(metadata.width);
	resouceDesc.Height = UINT(metadata.height);
	resouceDesc.MipLevels = UINT(metadata.mipLevels);
	resouceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
	resouceDesc.Format = metadata.format;
	resouceDesc.SampleDesc.Count = 1;
	resouceDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);

	//蛻ｩ逕ｨ縺吶ｋheap縺ｮ險ｭ螳・
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	//resouces縺ｮ菴懈・
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


DirectX::ScratchImage DirectXCommon::LoadTexture(const std::string& filePath)
{
	DirectX::ScratchImage image{};
	DirectX::ScratchImage mipImages{};
	std::wstring filePathW = DirectXCommon::ConvertString(filePath);
	HRESULT hr = S_FALSE;

	// =========================================================
	//  諡｡蠑ｵ蟄舌′ .dds 縺九←縺・°縺ｧ隱ｭ縺ｿ霎ｼ縺ｿ髢｢謨ｰ繧貞・縺代ｋ・・
	// =========================================================
	if (filePath.size() >= 4 && filePath.substr(filePath.size() - 4) == ".dds")
	{
		// DDS逕ｨ縺ｮ隱ｭ縺ｿ霎ｼ縺ｿ髢｢謨ｰ
		hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);

		// DDS縺ｮ蝣ｴ蜷医・縺吶〒縺ｫ繝溘ャ繝励・繝・・繧・く繝･繝ｼ繝悶・繝・・縺梧ｧ狗ｯ峨＆繧後※縺・ｋ縺薙→縺悟､壹＞縺ｮ縺ｧ縲・
		// 縺昴・縺ｾ縺ｾ霑斐☆・医Α繝・・繝槭ャ繝礼函謌舌ｒ繧ｹ繧ｭ繝・・・・
		if (SUCCEEDED(hr)) {
			return image;
		}
	} else
	{
		// PNG, JPG縺ｪ縺ｩ縺ｮ荳闊ｬ逧・↑逕ｻ蜒冗畑
		hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	}

	// 繝・け繧ｹ繝√Ε縺瑚ｪｭ縺ｿ霎ｼ縺ｾ繧後↑縺九▲縺溷ｴ蜷医・逋ｽ濶ｲ縺ｮ繝・け繧ｹ繝√Ε繧貞ｼｵ繧・
	if (FAILED(hr))
	{
		D3D12_RESOURCE_DESC materialData;
		materialData.Width = 1;
		materialData.Height = 1;
		materialData.DepthOrArraySize = 1;
		materialData.MipLevels = 1;
		materialData.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		materialData.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		// 逋ｽ濶ｲ縺ｮ繝・け繧ｹ繝√Ε繧剃ｽ懈・
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

		// 笘・▽縺・〒縺ｫ繝｡繝｢繝ｪ繝ｪ繝ｼ繧ｯ菫ｮ豁｣: 驟榊・繧地ew縺励◆縺ｮ縺ｧ delete[] 縺梧ｭ｣隗｣縺ｧ縺・
		delete[] pixels;
		return mipImages;
	}

	// 繝溘ル繝槭ャ繝励・菴懈・ (DDS莉･螟悶・蝣ｴ蜷医・縺ｿ螳溯｡後＆繧後ｋ)
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
	// --- 螳牙・縺ｫ繧ｳ繝槭Φ繝峨Μ繧ｹ繝医ｒ髢峨§繧・---
	HRESULT hr = S_OK;
	hr = commandList_->Close();
	if (FAILED(hr)) {
	}

	// --- 繧ｳ繝槭Φ繝峨Μ繧ｹ繝医ｒ螳溯｡・---
	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(_countof(commandLists), commandLists);

	// --- 繝輔ぉ繝ｳ繧ｹ繧ｷ繧ｰ繝翫Ν騾∽ｿ｡ ---
	fenceValue_++;
	hr = commandQueue_->Signal(fence_.Get(), fenceValue_);
	assert(SUCCEEDED(hr) && "Failed to signal command queue fence");

	// --- GPU縺悟ｮ御ｺ・☆繧九∪縺ｧ蠕・ｩ・---
	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}

	// --- 繝ｪ繧ｻ繝・ヨ蜃ｦ逅・ｼ亥ｿ・ｦ√↑蝣ｴ蜷医・縺ｿ・・--
	if (reset) {
		// 縺ｾ縺繧ｳ繝槭Φ繝峨Μ繧ｹ繝医′Open迥ｶ諷九・蝣ｴ蜷医・縺薙％縺ｧ縺ｯReset縺励↑縺・
		// GPU螳御ｺ・ｒ蠕・▲縺ｦ縺・ｋ縺ｮ縺ｧ螳牙・縺ｫ繝ｪ繧ｻ繝・ヨ縺ｧ縺阪ｋ
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
/// 讓呎ｺ也噪縺ｪ豺ｱ蠎ｦ繧ｹ繝・Φ繧ｷ繝ｫ繝・せ繧ｯ繝ｪ繝励ち繧貞叙蠕励☆繧・
/// </summary>
D3D12_DEPTH_STENCIL_DESC DirectXCommon::GetDefaultDepthStencilDesc() const {
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	// Depth縺ｮ讖溯・繧呈怏蜉ｹ蛹・
	depthStencilDesc.DepthEnable = TRUE;
	// 譖ｸ縺崎ｾｼ縺ｿ縺励∪縺・
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	// 豈碑ｼ・未謨ｰ縺ｯLessEqual縲ゅ▽縺ｾ繧翫∬ｿ代￠繧後・謠冗判縺輔ｌ繧・
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	// Stencil縺ｮ讖溯・縺ｯ蛻ｩ逕ｨ縺励↑縺・
	depthStencilDesc.StencilEnable = FALSE;

	return depthStencilDesc;
}

/// <summary>
/// 豺ｱ蠎ｦ繧ｹ繝・Φ繧ｷ繝ｫ繝薙Η繝ｼ縺ｮ繝輔か繝ｼ繝槭ャ繝医ｒ蜿門ｾ励☆繧・
/// </summary>
DXGI_FORMAT DirectXCommon::GetDSVFormat() const {

	return DXGI_FORMAT_D24_UNORM_S8_UINT;
}



void DirectXCommon::WaitForGPUAndReset() {
	// --- 繝輔ぉ繝ｳ繧ｹ譛ｪ逕滓・縺ｾ縺溘・遐ｴ譽・ｸ医∩縺ｪ繧峨せ繧ｭ繝・・ ---
	if (!fence_ || !commandQueue_ || !commandAllocator_ || !commandList_) {
		Log("[DirectXCommon] WaitForGPUAndReset skipped (resources not ready)\n");
		return;
	}

	// --- GPU縺悟ｮ御ｺ・☆繧九∪縺ｧ蠕・ｩ・---
	fenceValue_++;
	HRESULT hr = commandQueue_->Signal(fence_.Get(), fenceValue_);
	assert(SUCCEEDED(hr));

	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}



	// --- 蜀榊茜逕ｨ蜿ｯ閭ｽ縺ｪ迥ｶ諷九↓繝ｪ繧ｻ繝・ヨ ---
	hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));
}
void DirectXCommon::CreateRenderTexture() {
	// 1. 繝ｪ繧ｽ繝ｼ繧ｹ險ｭ螳・
	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.Width = WinApp::kClientWidth;
	resDesc.Height = WinApp::kClientHeight;
	resDesc.MipLevels = 1;
	resDesc.DepthOrArraySize = 1;

	// HDR繝輔か繝ｼ繝槭ャ繝・(豁ｪ縺ｿ逕ｨ繧ｳ繝斐・蜈医ｂ縺薙ｌ縺ｫ蜷医ｏ縺帙ｋ)
	resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

	resDesc.SampleDesc.Count = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	// 2. 繧ｯ繝ｪ繧｢繧ｫ繝ｩ繝ｼ
	clearColor_[0] = 0.1f;
	clearColor_[1] = 0.25f;
	clearColor_[2] = 0.5f;
	clearColor_[3] = 1.0f;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	clearValue.Color[0] = clearColor_[0];
	clearValue.Color[1] = clearColor_[1];
	clearValue.Color[2] = clearColor_[2];
	clearValue.Color[3] = clearColor_[3];

	// 3. 逕滓・ 
	D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_DEFAULT };
	HRESULT hr = device_->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
		IID_PPV_ARGS(&renderTexture_)
	);
	assert(SUCCEEDED(hr));

	// 4. RTV (Render Target View) 縺ｮ菴懈・
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 1;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	hr = device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtRtvHeap_));
	assert(SUCCEEDED(hr));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device_->CreateRenderTargetView(renderTexture_.Get(), &rtvDesc, rtRtvHeap_->GetCPUDescriptorHandleForHeapStart());

	// 5. SRV (Shader Resource View) 縺ｮ菴懈・
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	renderTextureSrvHandle_ = SRVManager::GetInstance()->CreateSRV(renderTexture_.Get(), srvDesc);

	// =======================================================
	// 笘・ｿｽ蜉: 繝・ぅ繧ｹ繝医・繧ｷ繝ｧ繝ｳ逕ｨ縺ｮ閭梧勹繧ｳ繝斐・繝・け繧ｹ繝√Ε (GrabTexture) 縺ｮ逕滓・
	// =======================================================
	// 6. GrabTexture譛ｬ菴薙・逕滓・ (險ｭ螳壹・ renderTexture_ 縺ｨ蜈ｨ縺丞酔縺・
	HRESULT hrGrab = device_->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
		IID_PPV_ARGS(&grabTexture_)
	);
	assert(SUCCEEDED(hrGrab));

	// 7. GrabTexture 繧偵す繧ｧ繝ｼ繝繝ｼ縺ｧ隱ｭ繧縺溘ａ縺ｮ SRV 菴懈・
	grabSrvHandle_ = SRVManager::GetInstance()->CreateSRV(grabTexture_.Get(), srvDesc);
}

void DirectXCommon::PreDrawRenderTexture() {
	HRESULT hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));

	// --- 莉･荳九∵里蟄倥・繧ｳ繝ｼ繝・---
	// 1. 繝舌Μ繧｢・夊ｪｭ繧繝｢繝ｼ繝・-> 謠上￥繝｢繝ｼ繝・
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = renderTexture_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList_->ResourceBarrier(1, &barrier);

	// 2. 謠冗判蜈医ｒ縲後Ξ繝ｳ繝繝ｼ繝・け繧ｹ繝√Ε縲阪↓蛻・ｊ譖ｿ縺医ｋ
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtRtvHeap_->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

	// 3. 繧ｯ繝ｪ繧｢
	commandList_->ClearRenderTargetView(rtvHandle, clearColor_, 0, nullptr);
	commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// 4. 繝薙Η繝ｼ繝昴・繝郁ｨｭ螳・
	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissorRect_);

	// 5. 繝偵・繝苓ｨｭ螳・
	ID3D12DescriptorHeap* descriptorHeaps[] = { SRVManager::GetInstance()->GetDescriptorHeap() };
	commandList_->SetDescriptorHeaps(1, descriptorHeaps);
}
// 謠冗判邨ゆｺ・ｼ唔mGui縺瑚ｪｭ繧√ｋ繧医≧縺ｫ謌ｻ縺励∪縺・
void DirectXCommon::PostDrawRenderTexture() {
	// 繝舌Μ繧｢・壽緒縺上Δ繝ｼ繝・-> 隱ｭ繧繝｢繝ｼ繝・
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = renderTexture_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList_->ResourceBarrier(1, &barrier);
}
void DirectXCommon::PreDrawBackBuffer() {
	//  繝舌ャ繧ｯ繝舌ャ繝輔ぃ縺ｮ繧､繝ｳ繝・ャ繧ｯ繧ｹ繧貞叙蠕・(縺薙ｌ縺悟ｿ・ｦ・
	backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

	// 笘・ｿｽ蜉: 繝ｪ繧ｽ繝ｼ繧ｹ繝舌Μ繧｢ (陦ｨ遉ｺ繝｢繝ｼ繝・-> 譖ｸ縺崎ｾｼ縺ｿ繝｢繝ｼ繝峨∈螟画峩)
	// 縺薙ｌ縺後↑縺・→GPU縺ｯ繝舌ャ繧ｯ繝舌ャ繝輔ぃ縺ｸ縺ｮ謠冗判繧堤┌隕悶＠縺ｾ縺・
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList_->ResourceBarrier(1, &barrier);

	// 1. 繝舌ャ繧ｯ繝舌ャ繝輔ぃ縺ｮ繝上Φ繝峨Ν繧貞叙蠕・
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	rtvHandle.ptr = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart().ptr +
		(backBufferIndex_ * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

	// 2. 謠冗判蜈医ｒ繧ｻ繝・ヨ
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

	// 笘・ｿｽ蜉: 逕ｻ髱｢繧ｯ繝ｪ繧｢ (ImGui縺ｮ閭梧勹繧・囮髢鍋畑)
	float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f }; // 髱定牡縺ｪ縺ｩ
	commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	// ImGui縺梧ｷｱ蠎ｦ繧剃ｽｿ縺・ｴ蜷医・繧ｯ繝ｪ繧｢縺悟ｿ・ｦ・
	commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// 3. 繝薙Η繝ｼ繝昴・繝医↑縺ｩ繧ら判髱｢繧ｵ繧､繧ｺ縺ｫ謌ｻ縺・
	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissorRect_);

	// 4. ImGui逕ｨ縺ｫ繝偵・繝励ｒ繧ｻ繝・ヨ縺礼峩縺・
	ID3D12DescriptorHeap* descriptorHeaps[] = { SRVManager::GetInstance()->GetDescriptorHeap() };
	commandList_->SetDescriptorHeaps(1, descriptorHeaps);
};

void DirectXCommon::CreateShadowMap() {
	// 1. 繝・せ繧ｯ繝ｪ繝励ち繝偵・繝励・菴懈・ (DSV逕ｨ)
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&shadowDsvHeap_));
	assert(SUCCEEDED(hr));

	// 2. 繝ｪ繧ｽ繝ｼ繧ｹ縺ｮ險ｭ螳・(DSV縺ｨSRV縺ｮ荳｡譁ｹ縺ｧ菴ｿ縺医ｋ繧医≧縺ｫ TYPELESS 縺ｫ縺吶ｋ)
	D3D12_RESOURCE_DESC resDesc{};
	resDesc.Width = kShadowMapWidth;
	resDesc.Height = kShadowMapHeight;
	resDesc.MipLevels = 1;
	resDesc.DepthOrArraySize = 1;
	resDesc.Format = DXGI_FORMAT_R32_TYPELESS; // 笘・㍾隕・ 蝙狗┌縺励ヵ繧ｩ繝ｼ繝槭ャ繝・
	resDesc.SampleDesc.Count = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	// 3. 繝偵・繝励・繝ｭ繝代ユ繧｣縺ｨ繧ｯ繝ｪ繧｢蛟､
	D3D12_HEAP_PROPERTIES heapProps{ D3D12_HEAP_TYPE_DEFAULT };
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = DXGI_FORMAT_D32_FLOAT; // 繧ｯ繝ｪ繧｢譎ゅ・豺ｱ蠎ｦ繝輔か繝ｼ繝槭ャ繝・
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	// 4. 繝ｪ繧ｽ繝ｼ繧ｹ縺ｮ逕滓・ (蛻晄悄迥ｶ諷九・隱ｭ縺ｿ霎ｼ縺ｿ蜿ｯ閭ｽ縺ｪ SRV 迥ｶ諷九↓縺励※縺翫￥)
	hr = device_->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
		IID_PPV_ARGS(&shadowMapResource_)
	);
	assert(SUCCEEDED(hr));

	// 5. DSV (Depth Stencil View) 縺ｮ菴懈・
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT; // 豺ｱ蠎ｦ縺ｨ縺励※隗｣驥・
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	device_->CreateDepthStencilView(shadowMapResource_.Get(), &dsvDesc, shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart());

	// 6. SRV (Shader Resource View) 縺ｮ菴懈・
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT; // 繝・け繧ｹ繝√Ε縺ｨ縺励※隱ｭ繧縺ｨ縺阪・Float
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;

	// SRVManager縺ｫ逋ｻ骭ｲ縺励※繝上Φ繝峨Ν繧偵ｂ繧峨≧
	shadowMapSrvHandle_ = SRVManager::GetInstance()->CreateSRV(shadowMapResource_.Get(), srvDesc);

	Log("Created Shadow Map successfully.\n");
}

void DirectXCommon::PreDrawShadow() {
	// 1. 繝舌Μ繧｢・夂判蜒上→縺励※隱ｭ繧繝｢繝ｼ繝・-> 豺ｱ蠎ｦ繧呈嶌縺崎ｾｼ繧繝｢繝ｼ繝峨∈
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = shadowMapResource_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	commandList_->ResourceBarrier(1, &barrier);

	// 2. 謠冗判蜈医ｒ縲後す繝｣繝峨え繝槭ャ繝励・DSV縲阪・縺ｿ縺ｫ險ｭ螳夲ｼ・TV縺ｯ辟｡縺暦ｼ・
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart();
	commandList_->OMSetRenderTargets(0, nullptr, false, &dsvHandle);

	// 3. 逕ｻ髱｢繧堤悄縺｣逋ｽ・域ｷｱ蠎ｦ1.0f・峨↓繧ｯ繝ｪ繧｢縺吶ｋ・・
	commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// 4. 繝薙Η繝ｼ繝昴・繝医→繧ｷ繧ｶ繝ｼ遏ｩ蠖｢繧偵す繝｣繝峨え繝槭ャ繝励・隗｣蜒丞ｺｦ縺ｫ蜷医ｏ縺帙ｋ
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
	// 1. 繝舌Μ繧｢・壽ｷｱ蠎ｦ繧呈嶌縺崎ｾｼ繧繝｢繝ｼ繝・-> 逕ｻ蜒上→縺励※隱ｭ繧繝｢繝ｼ繝峨↓謌ｻ縺・
	// ・医％繧後′縺ｪ縺・→ImGui繧・Γ繧､繝ｳ繧ｷ繧ｧ繝ｼ繝繝ｼ縺ｧ隱ｭ繧√↑縺上※繧ｨ繝ｩ繝ｼ縺ｫ縺ｪ繧翫∪縺呻ｼ・
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = shadowMapResource_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList_->ResourceBarrier(1, &barrier);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtRtvHeap_->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

	// 繝薙Η繝ｼ繝昴・繝医→繧ｷ繧ｶ繝ｼ遏ｩ蠖｢繧ゅΓ繧､繝ｳ逕ｻ髱｢逕ｨ縺ｫ謌ｻ縺・
	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissorRect_);
}

void DirectXCommon::StartGpuProfile() {
	// 繧ｳ繝槭Φ繝峨Μ繧ｹ繝医・蜈磯ｭ縺ｧ縲碁幕蟋区凾髢薙阪ｒ險倬鹸・医う繝ｳ繝・ャ繧ｯ繧ｹ0逡ｪ・・
	commandList_->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
}

void DirectXCommon::EndGpuProfile() {
	// 蜈ｨ縺ｦ縺ｮ謠冗判縺檎ｵゅｏ縺｣縺溷ｾ後↓縲檎ｵゆｺ・凾髢薙阪ｒ險倬鹸・医う繝ｳ繝・ャ繧ｯ繧ｹ1逡ｪ・・
	commandList_->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);

	// 險倬鹸縺励◆2縺､縺ｮ繧ｿ繧､繝繧ｹ繧ｿ繝ｳ繝励ｒ縲，PU縺瑚ｪｭ繧√ｋ繝舌ャ繝輔ぃ・・ueryResultBuffer_・峨↓繧ｳ繝斐・縺吶ｋ蜻ｽ莉､
	commandList_->ResolveQueryData(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, queryResultBuffer_.Get(), 0);
}

void DirectXCommon::ReadGpuProfile() {
	// 蜑榊屓縺ｮ繝輔Ξ繝ｼ繝縺ｧGPU縺梧嶌縺崎ｾｼ繧薙□邨先棡繧辰PU蛛ｴ縺ｧ隱ｭ縺ｿ蜿悶ｋ
	uint64_t* mappedData = nullptr;
	HRESULT hr = queryResultBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
	if (SUCCEEDED(hr)) {
		uint64_t startTime = mappedData[0];
		uint64_t endTime = mappedData[1];
		queryResultBuffer_->Unmap(0, nullptr);

		// 邨ゆｺ・凾髢薙°繧蛾幕蟋区凾髢薙ｒ蠑輔″縲∝捉豕｢謨ｰ縺ｧ蜑ｲ縺｣縺ｦ繝溘Μ遘抵ｼ・s・峨↓螟画鋤
		if (endTime > startTime && gpuFrequency_ > 0) {
			gpuDrawTimeMs_ = static_cast<float>(endTime - startTime) / static_cast<float>(gpuFrequency_) * 1000.0f;
		}
	}
}
void DirectXCommon::CreateDepthSrv() {
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 縺薙％縺ｧSRV繧剃ｽ懊ｋ・・
	depthSrvHandle_ = SRVManager::GetInstance()->CreateSRV(depthStencilResource_.Get(), srvDesc);
}

void DirectXCommon::PreDrawLocalFog() {
	// 繝舌Μ繧｢・壽ｷｱ蠎ｦ縲梧嶌縺崎ｾｼ縺ｿ繝｢繝ｼ繝峨・-> 縲瑚ｪｭ縺ｿ霎ｼ縺ｿ繝｢繝ｼ繝・(逕ｻ蜒・縲阪↓螟画鋤・・
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = depthStencilResource_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList_->ResourceBarrier(1, &barrier);
	// =================================================================
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtRtvHeap_->GetCPUDescriptorHandleForHeapStart();
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, nullptr); // 竊・DSV繧・nullptr 縺ｫ縺吶ｋ・・
}

void DirectXCommon::PostDrawLocalFog() {
	// 繝舌Μ繧｢・壹瑚ｪｭ縺ｿ霎ｼ縺ｿ繝｢繝ｼ繝峨・-> 蜈・・縲梧嶌縺崎ｾｼ縺ｿ繝｢繝ｼ繝峨阪↓謌ｻ縺呻ｼ・
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = depthStencilResource_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	commandList_->ResourceBarrier(1, &barrier);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtRtvHeap_->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle); // 竊・DSV繧貞ｾｩ豢ｻ・・
}

void DirectXCommon::UpdateGrabTexture() {
	// 繝舌Μ繧｢: RTV(謠冗判蜈・ -> COPY_SOURCE(繧ｳ繝斐・蜈・ 縺ｸ
	D3D12_RESOURCE_BARRIER barrier1{};
	barrier1.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier1.Transition.pResource = renderTexture_.Get();
	barrier1.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier1.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	commandList_->ResourceBarrier(1, &barrier1);

	// 繝舌Μ繧｢: SRV(逕ｻ蜒・ -> COPY_DEST(繧ｳ繝斐・蜈・ 縺ｸ
	D3D12_RESOURCE_BARRIER barrier2{};
	barrier2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier2.Transition.pResource = grabTexture_.Get();
	barrier2.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier2.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	commandList_->ResourceBarrier(1, &barrier2);

	// 笘・さ繝斐・螳溯｡鯉ｼ・迴ｾ蝨ｨ縺ｮ逕ｻ髱｢繧剃ｸｸ縺斐→菫晏ｭ・
	commandList_->CopyResource(grabTexture_.Get(), renderTexture_.Get());

	// 繝舌Μ繧｢繧貞・縺ｮ迥ｶ諷九↓謌ｻ縺・
	barrier1.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	barrier1.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList_->ResourceBarrier(1, &barrier1);

	barrier2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier2.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList_->ResourceBarrier(1, &barrier2);
}

void DirectXCommon::ResizeSwapChain(int32_t width, int32_t height) {
	// 1. GPU縺ｮ螳御ｺ・ｒ蠕・▽ (蜍輔＞縺ｦ縺・ｋ譛荳ｭ縺ｫ菴懊ｊ逶ｴ縺吶→繧ｯ繝ｩ繝・す繝･縺吶ｋ縺溘ａ)
	WaitForGPUAndReset();

	// 2. 莉頑戟縺｣縺ｦ縺・ｋ繝舌ャ繧ｯ繝舌ャ繝輔ぃ・・wapChainResources・峨ｒ荳蠎ｦ繝ｪ繧ｻ繝・ヨ縺吶ｋ
	for (size_t i = 0; i < backBufferCount_; ++i) {
		swapChainResources_[i].Reset();
	}
	// 豺ｱ蠎ｦ繝舌ャ繝輔ぃ繧ゅΜ繧ｻ繝・ヨ
	depthStencilResource_.Reset();

	// 3. 繧ｹ繝ｯ繝・・繝√ぉ繝ｼ繝ｳ閾ｪ菴薙・繧ｵ繧､繧ｺ繧貞､画峩
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

	// 5. DSV・域ｷｱ蠎ｦ繝舌ャ繝輔ぃ・峨ｒ譁ｰ縺励＞繧ｵ繧､繧ｺ縺ｧ蜀堺ｽ懈・
	depthStencilResource_ = CreateDepthStencilTextureResource(width, height);
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device_->CreateDepthStencilView(depthStencilResource_.Get(), &dsvDesc, dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());

	// 6. 繝薙Η繝ｼ繝昴・繝医→繧ｷ繧ｶ繝ｼ遏ｩ蠖｢繧よ峩譁ｰ
	viewport_.Width = (float)width;
	viewport_.Height = (float)height;
	scissorRect_.right = width;
	scissorRect_.bottom = height;


	CreateRenderTexture();
}
void DirectXCommon::ExecuteLoadCommands() {
	HRESULT hr = loadCommandList_->Close();
	assert(SUCCEEDED(hr));

	ID3D12CommandList* commandLists[] = { loadCommandList_.Get() };
	commandQueue_->ExecuteCommandLists(1, commandLists);

	fenceValue_++;
	commandQueue_->Signal(fence_.Get(), fenceValue_);
	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}

	hr = loadCommandAllocator_->Reset();
	assert(SUCCEEDED(hr));
	hr = loadCommandList_->Reset(loadCommandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));
}
