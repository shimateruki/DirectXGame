
#include "DirectXCommon.h"
#include "ColorSpace.h"
#include "engine/system/scene/LoadingProfiler.h"
#include "WinApp.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cctype>
#include <format>
#include <vector>
#include <dxcapi.h>
#include <fstream>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <thread>
#include "SrvManager.h"
#include "PostEffect.h"
#include"ImGuiManager.h"

// ログ出力用のヘルパー関数（グローバル）
// デバッグ出力へ文字列を流し、DirectX初期化やリソース生成の状態を追えるようにする。
void Log(const std::string& message) {
    // デバッグ出力へ送る前に文字列を加工せず、呼び出し元の改行も維持する。
    OutputDebugStringA(message.c_str());
}

std::string ConvertString(const std::wstring& str);

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")
DirectXCommon* DirectXCommon::GetInstance() {
    // DirectXCommonのシングルトンインスタンスを返す。
	static DirectXCommon instance;
	return &instance;
}

void DirectXCommon::Initialize(WinApp* winApp) {
    // DirectX12のデバイス、描画先、同期、シェーダーコンパイラ、GPU計測をまとめて初期化する。
	assert(winApp);
	winApp_ = winApp;

	InitializeFixedFps();

	// 各種初期化処理
	InitializeDXGIDevice();
	CreateCommand();
	CreateSwapChain();
	CreateRtv();
	CreateDsv();
	CreateFence();

	// ビューポートとシザー矩形の設定
	viewport_.Width = (float)WinApp::GetClientWidth();
	viewport_.Height = (float)WinApp::GetClientHeight();
	viewport_.TopLeftX = 0;
	viewport_.TopLeftY = 0;
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;
	scissorRect_.left = 0;
	scissorRect_.right = WinApp::GetClientWidth();
	scissorRect_.top = 0;
	scissorRect_.bottom = WinApp::GetClientHeight();

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
    // 記録済みのゲーム映像を最終表示するため、バックバッファを準備する。
	frameCount_++;
	EnsureCommandListReadyForDraw();

	// シーンと後処理の記録を保持したまま、最終出力先だけ切り替えます。
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

	// 最終合成は深度を使わず、ゲーム用の固定サイズDSVを接続しません。

	// 出力先のRTVだけを設定します。
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

	// ゲーム映像の外側を黒帯としてクリアします。
	const float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);



	// ビューポート（描画する画面内の領域）を設定します。
	D3D12_VIEWPORT outputViewport{0.0f, 0.0f, static_cast<float>(WinApp::GetWindowWidth()), static_cast<float>(WinApp::GetWindowHeight()), 0.0f, 1.0f};
	commandList_->RSSetViewports(1, &outputViewport);

	// シザー矩形（ピクセルを描画する範囲を限定する矩形）を設定します。
	D3D12_RECT outputScissor{0, 0, WinApp::GetWindowWidth(), WinApp::GetWindowHeight()};
	commandList_->RSSetScissorRects(1, &outputScissor);

	//SRVManagerからデスクリプタヒープを取得して設定
	ID3D12DescriptorHeap* descriptorHeaps[] = { SrvManager::GetInstance()->GetDescriptorHeap() };
	commandList_->SetDescriptorHeaps(1, descriptorHeaps);
}

// CPUから書き込めるUploadヒープのバッファリソースを作成する。

//
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
	if (FAILED(hr)) {
		Log(std::format("CreateBufferResource failed. size={}, hr=0x{:08X}\n", sizeInBytes, static_cast<unsigned int>(hr)));
		return nullptr;
	}

	return resource;
}

// 固定FPS制御の基準時刻を初期化する。


void DirectXCommon::InitializeFixedFps()
{
	// 現在の時刻を取得して記録
	reference_ = std::chrono::steady_clock::now();
}

// 1フレームが短すぎる場合に待機し、60FPS基準のフレーム間隔へ寄せる。

void DirectXCommon::UpdateFixedFps()
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

// HLSLファイルをDXCでコンパイルし、生成されたシェーダーバイナリを返す。
namespace {
using ShaderDependency = std::pair<std::filesystem::path, std::filesystem::file_time_type>;

bool CollectShaderDependencies(const std::filesystem::path& source,
                               std::set<std::filesystem::path>& visited,
                               std::vector<ShaderDependency>& dependencies) {
    std::error_code error;
    const auto absolutePath = std::filesystem::absolute(source, error).lexically_normal();
    if (error)
        return false;
    if (!visited.insert(absolutePath).second)
        return true;

    const auto modified = std::filesystem::last_write_time(absolutePath, error);
    if (error)
        return false;
    dependencies.emplace_back(absolutePath, modified);

    std::ifstream file(absolutePath);
    if (!file)
        return false;
    std::string line;

    while (std::getline(file, line)) {
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line.compare(first, 8, "#include") != 0)
            continue;
        const size_t open = line.find_first_of("\"<", first + 8);
        if (open == std::string::npos)
            return false;
        const char closeChar = line[open] == '"' ? '"' : '>';
        const size_t close = line.find(closeChar, open + 1);
        if (close == std::string::npos)
            return false;
        const auto includeName = std::filesystem::path(line.substr(open + 1, close - open - 1));
        auto includePath = absolutePath.parent_path() / includeName;

        if (!std::filesystem::exists(includePath)) {
            includePath = std::filesystem::path("Resources/shader") / includeName;
        }

        // 依存先を特定できない場合はキャッシュせず、DXC本来のinclude解決に任せる。
        if (!std::filesystem::exists(includePath) ||
            !CollectShaderDependencies(includePath, visited, dependencies)) return false;
    }

    return true;
}
}

Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompileShader(
	const std::wstring& filePath, const wchar_t* profile, const wchar_t* entryPoint)
{
	// 同じシェーダーをシーンや粒子システムごとに再コンパイルしない。
	// 元ファイルとinclude先の更新時刻が変わればキャッシュを無効にする。
	using ShaderKey = std::tuple<std::wstring, std::wstring, std::wstring>;

	/// <summary>コンパイル済みシェーダーと依存ファイルを一緒に保持し、更新判定に使う。</summary>
	// 担当：依存ファイル情報とコンパイル済みシェーダーを保持する。
	// 関連：DirectXCommonの内部処理だけで使う補助型。
	struct CachedShader {
		std::vector<ShaderDependency> dependencies;
		Microsoft::WRL::ComPtr<IDxcBlob> blob;
	};

	static std::mutex cacheMutex;
	static std::map<ShaderKey, CachedShader> shaderCache;
	const ShaderKey key{filePath, profile, entryPoint};
	std::lock_guard<std::mutex> cacheLock(cacheMutex);
	const auto found = shaderCache.find(key);

	if (found != shaderCache.end()) {
		bool unchanged = true;

		for (const auto& [path, modified] : found->second.dependencies) {
			std::error_code error;

			if (std::filesystem::last_write_time(path, error) != modified || error) {
				unchanged = false;
				break;
			}
		}

		if (unchanged)
			return found->second.blob;
	}

#if defined(NDEBUG) && !defined(USE_IMGUI)
	// 配布セットの事前コンパイル済みDXILを優先する。Shaderとinclude先が更新されていれば使わない。
	const std::filesystem::path compiledPath = std::filesystem::path("StartupShaderCache") /
		std::filesystem::path(filePath + L"." + profile + L"." + entryPoint + L".cso");
	std::error_code cacheError;
	const auto compiledTime = std::filesystem::last_write_time(compiledPath, cacheError);

	if (!cacheError) {
		std::set<std::filesystem::path> visited;
		std::vector<ShaderDependency> dependencies;

		if (CollectShaderDependencies(filePath, visited, dependencies)) {
			const bool current = std::all_of(dependencies.begin(), dependencies.end(),
				[compiledTime](const ShaderDependency& dependency) {
					return dependency.second <= compiledTime;
				});
			if (current) {
				Microsoft::WRL::ComPtr<IDxcBlobEncoding> compiledFile;

				if (SUCCEEDED(dxcUtils_->LoadFile(compiledPath.c_str(), nullptr, &compiledFile))) {
					Microsoft::WRL::ComPtr<IDxcBlob> compiledBlob;

					if (SUCCEEDED(compiledFile.As(&compiledBlob))) {
						shaderCache[key] = {std::move(dependencies), compiledBlob};
						return compiledBlob;
					}
				}
			}
		}
	}
#endif

	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;

	// メンバ変数のdxcUtils_を使う
	HRESULT hr = dxcUtils_->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	assert(SUCCEEDED(hr));

	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;

	// 配布版はシェーダーのデバッグ情報を埋め込まず、コンパイルとPSO生成の負担を減らす。
#if defined(NDEBUG) && !defined(USE_IMGUI)
	LPCWSTR arguments[] = {
		filePath.c_str(), L"-E", entryPoint, L"-T", profile, L"-Od", L"-Zpr",
	};
#else
	LPCWSTR arguments[] = {
		filePath.c_str(), L"-E", entryPoint, L"-T", profile,
		L"-Zi", L"-Qembed_debug", L"-Od", L"-Zpr",
	};
#endif

	Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;

	// メンバ変数のdxcCompiler_, includeHandler_を使う
	hr = dxcCompiler_->Compile(
		&shaderSourceBuffer, arguments, _countof(arguments),
		includeHandler_.Get(), IID_PPV_ARGS(&shaderResult));
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);

	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		assert(false);
	}

	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));

	if (shaderBlob) {
		std::set<std::filesystem::path> visited;
		std::vector<ShaderDependency> dependencies;

		if (CollectShaderDependencies(filePath, visited, dependencies)) {
			shaderCache[key] = {std::move(dependencies), shaderBlob};
		} else {
			shaderCache.erase(key);
		}
	}

	return shaderBlob;
}

Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::TryCompileShader(
    const std::wstring& filePath, const wchar_t* profile, const wchar_t* entryPoint, std::string& errorMessage) {
    errorMessage.clear();
    if (!dxcUtils_ || !dxcCompiler_) {
        errorMessage = "DXCが初期化されていません";
        return nullptr;
    }
    Microsoft::WRL::ComPtr<IDxcBlobEncoding> source;
    if (FAILED(dxcUtils_->LoadFile(filePath.c_str(), nullptr, &source)) || !source) {
        errorMessage = "HLSLを読み込めません";
        return nullptr;
    }
    DxcBuffer buffer{source->GetBufferPointer(), source->GetBufferSize(), DXC_CP_UTF8};
    LPCWSTR arguments[] = {filePath.c_str(), L"-E", entryPoint, L"-T", profile, L"-Od", L"-Zpr"};
    Microsoft::WRL::ComPtr<IDxcResult> result;
    if (FAILED(dxcCompiler_->Compile(&buffer, arguments, _countof(arguments), includeHandler_.Get(), IID_PPV_ARGS(&result))) || !result) {
        errorMessage = "DXCの実行に失敗しました";
        return nullptr;
    }
    HRESULT status = E_FAIL;
    result->GetStatus(&status);
    if (FAILED(status)) {
        Microsoft::WRL::ComPtr<IDxcBlobUtf8> diagnostics;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&diagnostics), nullptr);
        errorMessage = diagnostics ? std::string(diagnostics->GetStringPointer(), diagnostics->GetStringLength()) : "HLSLのコンパイルに失敗しました";
        return nullptr;
    }
    Microsoft::WRL::ComPtr<IDxcBlob> shader;
    if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader), nullptr)) || !shader)
        errorMessage = "DXCからバイトコードを取得できません";
    return shader;
}

void DirectXCommon::Finalize() {
    // GPU処理の完了を待ってから、DirectX関連リソースを安全に解放する。
	if (!commandQueue_ || !fence_)
		return;

	// --- GPUの完了を確実に待つ ---
	fenceValue_++;
	HRESULT hr = commandQueue_->Signal(fence_.Get(), fenceValue_);
	assert(SUCCEEDED(hr));

	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		const auto waitStart = std::chrono::steady_clock::now();
		WaitForSingleObject(fenceEvent_, INFINITE);
		const auto waitMs = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - waitStart).count();
		LogSlowLoadingOperation("DirectXCommon GPU fence wait", waitMs);
	}

	// --- コマンドリストが開いていたら安全に閉じる ---
    if (commandListOpen_) {
        hr = commandList_->Close();
        assert(SUCCEEDED(hr));
        commandListOpen_ = false;
    }

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

// 描画後のバリア、クエリ解決、コマンド実行、Present、GPU同期をまとめて行う。


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
	commandListOpen_ = false;

	// コマンドリストの配列を作成します
	ID3D12CommandList* commandLists[] = { commandList_.Get() };

	// コマンドキューにコマンドリストを投入し、GPUに実行を指示します。
	commandQueue_->ExecuteCommandLists(1, commandLists);
	const auto presentStart = std::chrono::steady_clock::now();
	swapChain_->Present(1, 0);
	static bool firstPresentLogged = false;

	if (!firstPresentLogged) {
		// 最初のタイトルフレームを表示した時点を起動時間の終点とする。
		LogStartupProfileCheckpoint("first Present");
		firstPresentLogged = true;
	}

	const double presentMs = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - presentStart).count();
	if (presentMs >= 40.0) {
		LogSlowLoadingOperation("DirectXCommon Present", presentMs);
	}

	// フェンスの目標値をインクリメントします。
	fenceValue_++;

	// GPUがコマンドキューのここまで処理を終えたら、フェンスに新しい値を書き込むよう指示します。
	commandQueue_->Signal(fence_.Get(), fenceValue_);

	// GPUの処理がまだ完了していないかチェックします。
	if (fence_->GetCompletedValue() < fenceValue_) {
		// GPUが指定したフェンス値に達したときに発火するイベントを設定します。
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		const auto waitStart = std::chrono::steady_clock::now();
		WaitForSingleObject(fenceEvent_, INFINITE);
		const double waitMs = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - waitStart).count();
		if (waitMs >= 40.0) {
			LogSlowLoadingOperation("DirectXCommon frame GPU fence wait", waitMs);
		}
	}
}

// DXGIファクトリと高性能GPUアダプタを選び、D3D12デバイスを作成する。
void DirectXCommon::InitializeDXGIDevice() {

#ifdef _DEBUG
	// Debug構成だけD3D12診断レイヤーを有効にする。
	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController;

	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		debugController->EnableDebugLayer();
	}
#endif

	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory_));
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter;

	// ソフトウェアアダプタを避け、高性能GPUから順に実デバイスを選ぶ。
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

	// 上位の機能レベルが使えない環境では対応可能なレベルまで下げる。
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
    // コマンドキュー、アロケータ、コマンドリストを作成する。
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	HRESULT hr = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
	assert(SUCCEEDED(hr));
	hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
	assert(SUCCEEDED(hr));
	hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_));
	assert(SUCCEEDED(hr));
	commandListOpen_ = SUCCEEDED(hr);
}

void DirectXCommon::EnsureCommandListReadyForDraw() {
	// 初回はCreateCommandで開かれたリストを使い、同じフレーム内では二重リセットしない。
	if (commandListOpen_) {
		return;
	}

	HRESULT hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));
	commandListOpen_ = SUCCEEDED(hr);
}

void DirectXCommon::CreateSwapChain() {
    // ウィンドウへ表示するためのスワップチェーンを作成する。
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = WinApp::GetWindowWidth();
	swapChainDesc.Height = WinApp::GetWindowHeight();
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = (UINT)backBufferCount_;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	HRESULT hr = dxgiFactory_->CreateSwapChainForHwnd(commandQueue_.Get(), winApp_->GetHwnd(), &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain_.GetAddressOf()));
	assert(SUCCEEDED(hr));
}

void DirectXCommon::CreateRtv() {
    // スワップチェーンの各バックバッファに対応するRTVを作成する。
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

void DirectXCommon::CreateDsv() {
    // 深度ステンシル用のヒープと深度バッファを作成し、DSVへ登録する。
	dsvDescriptorHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false);
	depthStencilResource_ = CreateDepthStencilTextureResource(WinApp::GetClientWidth(), WinApp::GetClientHeight());

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device_->CreateDepthStencilView(depthStencilResource_.Get(), &dsvDesc, dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());

    // 画像参照中も深度テストを続けられるよう、2番目に読み取り専用DSVを置く。
    dsvDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL;
    D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsv = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    readOnlyDsv.ptr += device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    device_->CreateDepthStencilView(depthStencilResource_.Get(), &dsvDesc, readOnlyDsv);


}

// GPU同期に使うフェンスとイベントを作成する。
void DirectXCommon::CreateFence() {
	// CPUがGPUの処理完了を待てるよう、フェンスと通知イベントを対で作る。
	HRESULT hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	assert(SUCCEEDED(hr));
	fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent_ != nullptr);
	fenceValue_ = 0;
}

// 指定サイズの深度ステンシルテクスチャを作成する。


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

// 読み込んだ画像メタデータに合わせてGPUテクスチャリソースを作成する。

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateTextureResource(
    const DirectX::TexMetadata& metadata, D3D12_RESOURCE_STATES initialState)
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
		initialState,
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

// 画像ファイルをDirectXTexで読み込み、必要なミップマップや色空間変換を行う。


DirectX::ScratchImage DirectXCommon::LoadTexture(
    const std::string& filePath, bool forceSRGB, bool* loadedFromFile) {
    if (loadedFromFile) {
        *loadedFromFile = false;
    }

    // WICやミップ生成後も、DDSと同じ指定色空間でGPUへ登録する。
    const auto applyColorSpace = [forceSRGB](DirectX::ScratchImage& texture) {
        texture.OverrideFormat(forceSRGB ? DirectX::MakeSRGB(texture.GetMetadata().format)
                                        : DirectX::MakeLinear(texture.GetMetadata().format));
    };

    // 失敗を呼出元へ伝えてから白画像を返す。正常な1×1画像と混同しない。
    const auto makeWhiteTexture = [] {
        DirectX::ScratchImage white;
        white.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 1, 1, 1, 1);
        std::fill_n(white.GetPixels(), white.GetPixelsSize(), uint8_t{255});
        return white;
    };

    DirectX::ScratchImage image;
    const std::wstring filePathW = ConvertString(filePath);
    std::string extension = filePath.size() >= 4 ? filePath.substr(filePath.size() - 4) : std::string{};
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (extension == ".dds") {
        const HRESULT result = DirectX::LoadFromDDSFile(
            filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
        if (FAILED(result)) {
            return makeWhiteTexture();
        }
        // DDS内のミップを再利用し、用途に応じた色空間だけを適用する。
        applyColorSpace(image);
        if (loadedFromFile) {
            *loadedFromFile = true;
        }
        return image;
    }

    const auto wicFlags = forceSRGB ? DirectX::WIC_FLAGS_FORCE_SRGB : DirectX::WIC_FLAGS_IGNORE_SRGB;
    if (FAILED(DirectX::LoadFromWICFile(filePathW.c_str(), wicFlags, nullptr, image))) {
        return makeWhiteTexture();
    }

    applyColorSpace(image);

    // 1×1画像には下位ミップが無いため、デコードできた画像をそのまま返す。
    if (image.GetMetadata().width == 1 && image.GetMetadata().height == 1) {
        if (loadedFromFile) {
            *loadedFromFile = true;
        }
        return image;
    }

    DirectX::ScratchImage mipImages;
    const auto filterFlags = forceSRGB ? DirectX::TEX_FILTER_SRGB : DirectX::TEX_FILTER_DEFAULT;
    if (FAILED(DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(),
                                      image.GetMetadata(), filterFlags, 0, mipImages))) {
        return makeWhiteTexture();
    }
    applyColorSpace(mipImages);
    if (loadedFromFile) {
        *loadedFromFile = true;
    }
    return mipImages;
}

// UTF-8文字列をワイド文字列へ変換し、Win32やDXCなどのWCHAR APIへ渡せる形にする。
std::wstring DirectXCommon::ConvertString(const std::string& str) {
	// DXCなどのワイド文字APIへ渡すため、入力をUTF-8として変換する。
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

// コマンドキューへフェンスを打ち、必要に応じてGPU完了まで待つ。
void DirectXCommon::FlushCommandQueue(bool reset) {
	// 記録中のリストだけを閉じて実行する。PostDraw後の閉じたリストは再投入しない。
	HRESULT hr = S_OK;
	if (commandListOpen_) {
		hr = commandList_->Close();
		assert(SUCCEEDED(hr));
		commandListOpen_ = false;
		ID3D12CommandList* commandLists[] = { commandList_.Get() };
		commandQueue_->ExecuteCommandLists(_countof(commandLists), commandLists);
	}

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
		commandListOpen_ = SUCCEEDED(hr);
	}
}

// 指定種類と数のディスクリプタヒープを作成する。

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
DXGI_FORMAT DirectXCommon::GetDsvFormat() const {
	// 深度とステンシルを共用するビューの固定フォーマットを返す。

	return DXGI_FORMAT_D24_UNORM_S8_UINT;
}

// GPU完了待ち後にコマンドアロケータとコマンドリストを再利用できる状態へ戻す。
void DirectXCommon::WaitForGpuAndReset() {
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
		const auto waitStart = std::chrono::steady_clock::now();
		WaitForSingleObject(fenceEvent_, INFINITE);
		const auto waitMs = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - waitStart).count();
		LogSlowLoadingOperation("DirectXCommon GPU fence wait", waitMs);
	}

	// 記録中のリストは保持し、閉じている場合だけ次の描画用に開く。
	EnsureCommandListReadyForDraw();
}

void DirectXCommon::WaitForGpuIdle() {
    // 現在キューに積まれているGPU処理が完了するまでCPU側で待機する。
	if (!fence_ || !commandQueue_) {
		return;
	}

	fenceValue_++;
	HRESULT hr = commandQueue_->Signal(fence_.Get(), fenceValue_);
	assert(SUCCEEDED(hr));

	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		const auto waitStart = std::chrono::steady_clock::now();
		WaitForSingleObject(fenceEvent_, INFINITE);
		const auto waitMs = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - waitStart).count();
		LogSlowLoadingOperation("DirectXCommon WaitForGPUIdle", waitMs);
	}
}

void DirectXCommon::CreateRenderTexture() {
    // ゲーム画面やポストエフェクト用のオフスクリーン描画テクスチャを作成する。
	// 1. リソース設定
	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.Width = WinApp::GetClientWidth();
	resDesc.Height = WinApp::GetClientHeight();
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
	Microsoft::WRL::ComPtr<ID3D12Resource> newRenderTexture;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
		IID_PPV_ARGS(&newRenderTexture)
	);

	if (FAILED(hr) || !newRenderTexture) {
		Log("CreateRenderTexture failed: render texture resource could not be created.\n");
		return;
	}

	// 4. RTV (Render Target View) の作成
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 1;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> newRtRtvHeap;
	hr = device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&newRtRtvHeap));

	if (FAILED(hr) || !newRtRtvHeap) {
		Log("CreateRenderTexture failed: RTV heap could not be created.\n");
		return;
	}

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device_->CreateRenderTargetView(newRenderTexture.Get(), &rtvDesc, newRtRtvHeap->GetCPUDescriptorHandleForHeapStart());

	// 5. SRV (Shader Resource View) の作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	uint32_t newRenderTextureSrvHandle = renderTextureSrvHandle_;

	if (newRenderTextureSrvHandle != 0) {
		SrvManager::GetInstance()->CreateSrvForResource(newRenderTextureSrvHandle, newRenderTexture.Get(), srvDesc);
	} else {
		newRenderTextureSrvHandle = SrvManager::GetInstance()->CreateSrv(newRenderTexture.Get(), srvDesc);
	}

	// =======================================================
	// ★追加: ディストーション用の背景コピーテクスチャ (GrabTexture) の生成
	// =======================================================
	// 6. GrabTexture本体の生成 (設定は renderTexture_ と全く同じ)
	Microsoft::WRL::ComPtr<ID3D12Resource> newGrabTexture;
	HRESULT hrGrab = device_->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
		IID_PPV_ARGS(&newGrabTexture)
	);

	if (FAILED(hrGrab) || !newGrabTexture) {
		Log("CreateRenderTexture failed: grab texture resource could not be created.\n");
		return;
	}

	// 7. GrabTexture をシェーダーで読むための SRV 作成
	uint32_t newGrabSrvHandle = grabSrvHandle_;

	if (newGrabSrvHandle != 0) {
		SrvManager::GetInstance()->CreateSrvForResource(newGrabSrvHandle, newGrabTexture.Get(), srvDesc);
	} else {
		newGrabSrvHandle = SrvManager::GetInstance()->CreateSrv(newGrabTexture.Get(), srvDesc);
	}

	renderTexture_ = newRenderTexture;
	rtRtvHeap_ = newRtRtvHeap;
	renderTextureSrvHandle_ = newRenderTextureSrvHandle;
	grabTexture_ = newGrabTexture;
	grabSrvHandle_ = newGrabSrvHandle;
}

void DirectXCommon::PreDrawRenderTexture() {
    // シーンをレンダーテクスチャへ描画する前に、RTV状態と描画先を準備する。
	if (!renderTexture_ || !rtRtvHeap_ || !depthStencilResource_) {
		return;
	}

	EnsureCommandListReadyForDraw();

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
	ID3D12DescriptorHeap* descriptorHeaps[] = { SrvManager::GetInstance()->GetDescriptorHeap() };
	commandList_->SetDescriptorHeaps(1, descriptorHeaps);
}

// 描画終了：ImGuiが読めるように戻します
// レンダーテクスチャをシェーダーから読めるSRV状態へ戻す。
void DirectXCommon::PostDrawRenderTexture() {
	if (!renderTexture_) {
		return;
	}

	// バリア：描くモード -> 読むモード
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = renderTexture_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList_->ResourceBarrier(1, &barrier);
}

void DirectXCommon::PreDrawBackBuffer() {
    // ImGuiなどを最終表示するため、バックバッファを描画可能な状態へ切り替える。
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

	// 2. 描画先をセット
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

	// SetRenderClearColorで指定された色でクリアします。
	float clearColor[4];
	{
		std::lock_guard<std::mutex> lock(clearColorMutex_);
		clearColor[0] = clearColor_[0];
		clearColor[1] = clearColor_[1];
		clearColor[2] = clearColor_[2];
		clearColor[3] = clearColor_[3];
	}

	commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);



	// 3. ビューポートなども画面サイズに戻す
	D3D12_VIEWPORT outputViewport{0.0f, 0.0f, static_cast<float>(WinApp::GetWindowWidth()), static_cast<float>(WinApp::GetWindowHeight()), 0.0f, 1.0f};
	commandList_->RSSetViewports(1, &outputViewport);
	D3D12_RECT outputScissor{0, 0, WinApp::GetWindowWidth(), WinApp::GetWindowHeight()};
	commandList_->RSSetScissorRects(1, &outputScissor);

	// 4. ImGui用にヒープをセットし直す
	ID3D12DescriptorHeap* descriptorHeaps[] = { SrvManager::GetInstance()->GetDescriptorHeap() };
	commandList_->SetDescriptorHeaps(1, descriptorHeaps);
}

// シャドウマップ用の深度テクスチャ、DSV、SRVを作成する。


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
	resDesc.Width = shadowMapResolution_;
	resDesc.Height = shadowMapResolution_;
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

	// 解像度変更時は既存のSRV番号を再利用し、Descriptorの消費を増やさない。
	if (shadowMapSrvHandle_ != 0) {
		SrvManager::GetInstance()->CreateSrvForResource(shadowMapSrvHandle_, shadowMapResource_.Get(), srvDesc);
	}
	else {
		shadowMapSrvHandle_ = SrvManager::GetInstance()->CreateSrv(shadowMapResource_.Get(), srvDesc);
	}

	Log("Created Shadow Map successfully.\n");
}

void DirectXCommon::SetShadowMapResolution(int resolution) {
	// Power of two以外がJSONへ入っていても、対応する品質段階へ正規化する。
	int normalizedResolution = 2048;

	if (resolution <= 768) {
		normalizedResolution = 512;
	}
	else if (resolution <= 1536) {
		normalizedResolution = 1024;
	}
	else if (resolution <= 3072) {
		normalizedResolution = 2048;
	}
	else {
		normalizedResolution = 4096;
	}

	if (shadowMapResolution_ == normalizedResolution && shadowMapResource_) {
		return;
	}

	// 前フレームが参照している可能性があるため、解放前にGPU完了を保証する。
	WaitForGpuIdle();
	shadowMapResolution_ = normalizedResolution;
	shadowMapResource_.Reset();
	shadowDsvHeap_.Reset();
	CreateShadowMap();
}

void DirectXCommon::PreDrawShadow() {
    // シャドウマップへ深度を書き込むため、描画先とビューポートを影用に切り替える。
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
	viewport.Width = static_cast<float>(shadowMapResolution_);
	viewport.Height = static_cast<float>(shadowMapResolution_);
	viewport.MaxDepth = 1.0f;
	commandList_->RSSetViewports(1, &viewport);

	D3D12_RECT scissorRect{};
	scissorRect.right = shadowMapResolution_;
	scissorRect.bottom = shadowMapResolution_;
	commandList_->RSSetScissorRects(1, &scissorRect);
}

void DirectXCommon::PostDrawShadow() {
    // シャドウマップをSRV状態へ戻し、通常描画用の描画先とビューポートを復帰する。
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
    // GPU計測区間の開始タイムスタンプを書き込む。
    // まだ登録されていない名前なら、新しいインデックス（開始・終了ペア）を割り当てる
    if (!gpuProfileMap_.contains(name)) {
        if (nextQueryIndex_ + 2 > kMaxGpuQueries)
            return;
        gpuProfileMap_[name] = nextQueryIndex_;
        nextQueryIndex_ += 2;
    }

    uint32_t startIndex = gpuProfileMap_[name];
    commandList_->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, startIndex);
}

void DirectXCommon::EndGpuProfile(const std::string& name) {
    // GPU計測区間の終了タイムスタンプを書き込む。
    if (!gpuProfileMap_.contains(name))
        return;

    uint32_t startIndex = gpuProfileMap_[name];
    uint32_t endIndex = startIndex + 1;
    commandList_->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, endIndex);

    // ※ 個別に Resolve せず、フレームの最後で一括で行うように変更
}

void DirectXCommon::ResetGpuProfiles() {
    // 登録済みGPU計測区間をクリアし、次フレームの計測に備える。
    gpuProfileMap_.clear();
    nextQueryIndex_ = 0;
}

#include "ProfilerManager.h"
void DirectXCommon::ReadAllGpuProfiles() {
    // GPUタイムスタンプ結果を読み戻し、ProfilerManagerへミリ秒単位で記録する。
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

// 深度バッファをシェーダーから参照するためのSRVを作成または更新する。
void DirectXCommon::CreateDepthSrv() {
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// ここでSRVを作る！
	if (depthSrvHandle_ != 0) {
		SrvManager::GetInstance()->CreateSrvForResource(depthSrvHandle_, depthStencilResource_.Get(), srvDesc);
	} else {
		depthSrvHandle_ = SrvManager::GetInstance()->CreateSrv(depthStencilResource_.Get(), srvDesc);
	}
}

void DirectXCommon::PreDrawLocalFog() {
    // 書き込み用DSVを外してから、深度テストと画像参照が可能な状態へ切り替える。
	commandList_->OMSetRenderTargets(0, nullptr, false, nullptr);
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = depthStencilResource_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList_->ResourceBarrier(1, &barrier);

	// 深度SRVと読み取り専用DSVを同時に使い、特殊材質の深度テストを維持する。
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtRtvHeap_->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsv = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	readOnlyDsv.ptr += device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, &readOnlyDsv);
}

void DirectXCommon::PostDrawLocalFog() {
    // 読み取り専用DSVを外し、通常の深度書き込み状態へ戻す。
	commandList_->OMSetRenderTargets(0, nullptr, false, nullptr);
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = depthStencilResource_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	commandList_->ResourceBarrier(1, &barrier);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtRtvHeap_->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);
}

void DirectXCommon::UpdateGrabTexture() {
    // 現在のレンダーテクスチャ内容をGrab用テクスチャへコピーする。
	if (!renderTexture_ || !grabTexture_) {
		return;
	}

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
    // バックバッファだけ実ウィンドウへ追従させ、ゲームの座標・深度・後処理は保持します。
    if (width <= 0 || height <= 0)
        return;
    DXGI_SWAP_CHAIN_DESC1 desc{};
    swapChain_->GetDesc1(&desc);
    if (desc.Width == static_cast<UINT>(width) && desc.Height == static_cast<UINT>(height))
        return;
    WaitForGpuIdle();
    for (auto& resource : swapChainResources_)
        resource.Reset();
    HRESULT hr = swapChain_->ResizeBuffers(static_cast<UINT>(backBufferCount_),
        static_cast<UINT>(width), static_cast<UINT>(height), desc.Format, desc.Flags);
    assert(SUCCEEDED(hr));
    WinApp::SetWindowSize(width, height);
    CreateRtv();
}
void DirectXCommon::RequestResize(int32_t width, int32_t height) {
    // WM_SIZEなどから受け取ったサイズ変更要求を、次の安全なタイミングまで保留する。
	if (width <= 0 || height <= 0) {
		return;
	}

	pendingResizeWidth_ = width;
	pendingResizeHeight_ = height;
	pendingResize_ = true;
}

void DirectXCommon::ProcessPendingResize() {
    // 保留中のサイズ変更要求があれば、描画ループ側の安全なタイミングで反映する。
	if (!pendingResize_) {
		return;
	}

	const int32_t width = pendingResizeWidth_;
	const int32_t height = pendingResizeHeight_;
	pendingResize_ = false;

	ResizeSwapChain(width, height);
}

void DirectXCommon::SetRenderClearColor(float r, float g, float b, float a) {
    // 編集用の色を作業色空間へ変換し、描画スレッドと競合しないよう更新する。
    const engine::Vector4 workingColor = engine::ColorSpace::AuthoringToWorking({ r, g, b, a });
    std::lock_guard<std::mutex> lock(clearColorMutex_);
    clearColor_[0] = workingColor.x;
    clearColor_[1] = workingColor.y;
    clearColor_[2] = workingColor.z;
    clearColor_[3] = workingColor.w;
}
