#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
//時間を扱うライブラリ
#include <chrono>
//ファイルに書いてり読んだりするライブラリ
#include <format>

#include <fstream>
#include <d3d12.h>
#include <dxgi1_6.h>
#include<cassert>
#include <dbghelp.h>
#include <strsafe.h>
#include <dxgidebug.h>
#include <dxcapi.h>


#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

//BOOL MiniDumpWriteDump(
//	[in] HANDLE                            hProcess,
//	[in] DWORD                             ProcessId,
//	[in] HANDLE                            hFile,
//	[in] MINIDUMP_TYPE                     DumpType,
//	[in] PMINIDUMP_EXCEPTION_INFORMATION   ExceptionParam,
//	[in] PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
//	[in] PMINIDUMP_CALLBACK_INFORMATION    CallbackParam
//);

struct Vector4
{
	float x;
	float y;
	float z;
	float w;
};


static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception)
{
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = { 0 };
	CreateDirectory(L"./DUMPS", nullptr);
	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d%02d-%02d%02d.dmp",time.wYear,time.wMonth, time.wDay, time.wHour, time.wMinute );
	HANDLE dumpFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE,FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{0};
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;
	MiniDumpWriteDump(GetCurrentProcess(), processId, dumpFileHandle, MiniDumpNormal, &minidumpInformation, nullptr, nullptr);

	
	

	return EXCEPTION_EXECUTE_HANDLER;
}


std::wstring ConvertString(const std::string& str) {
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

std::string ConvertString(const std::wstring& str) {
	if (str.empty()) {
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) {
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}


//void Log(const std::string& message)
//{
//	OutputDebugStringA(message.c_str());
//}


void Log(std::ostream& os, const std::string& message)
{
	os << message << std::endl;
	OutputDebugStringA(message.c_str());
}


LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg,
	WPARAM wparam, LPARAM lparam)
{

	//メッセージに応じてゲーム固有の処理を行う
	switch (msg)
	{
		//windowsが放棄された
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	//標準メッセージを行う
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

IDxcBlob* CompileShander(
	const std::wstring& filePath,
	const wchar_t* profile,
	IDxcUtils* dxcUtils,
	IDxcCompiler3* dxCompiler,
	IDxcIncludeHandler* includeHandler,
	 std::ostream& os)
{
	//１hlslファイルを読み込む
	Log(os,ConvertString(std::format(L"Begin CompileShader,path:{}, profile:{}\n", filePath, profile)));
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	assert(SUCCEEDED(hr));

	DxcBuffer shaderSoucesBuffer;
	shaderSoucesBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSoucesBuffer.Size = shaderSource->GetBufferSize();
	shaderSoucesBuffer.Encoding = DXC_CP_UTF8;
	//２complieする
	LPCWSTR argument[] = { filePath.c_str(),
	L"-E",L"main",
	L"-T", profile,
	L"-Zi", L"-Qembed_debug",
	L"-Od",
	L"-Zpr" };

	IDxcResult* shaderResult = nullptr;
	hr = dxCompiler->Compile(
		&shaderSoucesBuffer,
		argument,
		_countof(argument),
		includeHandler,
		IID_PPV_ARGS(&shaderResult));

	assert(SUCCEEDED(hr));
	//３警告エラーが出てないか確認する
	IDxcBlobUtf8* shanderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shanderError), nullptr);
	if (shanderError != nullptr && shanderError->GetStringLength()!= 0)
	{
		Log(os,shanderError->GetStringPointer());
		assert(false);

	}
	//４Compile結果を受けて返す
	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	Log(os, ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));
	shaderSource->Release();
	shaderResult->Release();
	return shaderBlob;

}
	






int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	SetUnhandledExceptionFilter(ExportDump);
	
	



	//ログのフォルダ作成
	std::filesystem::create_directory("logs");
	//現在時刻を取得
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	//ログファイルの名前にコンマはいらないので削って秒にする
	std::chrono::time_point < std::chrono::system_clock, std::chrono::seconds >
	nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
	//日本時間に変換
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };
	//年月日時分秒の文字列の取得
	std::string dateStrings = std::format("{:%Y%m%d_%H%M%S}", localTime);
	//ファイル名
	std::string  logFilePath = std::format("logs/") + dateStrings + "log";
	std::ofstream logStrem(logFilePath);
	WNDCLASS wc{};
	//windowプロシージャ
	wc.lpfnWndProc = WindowProc;
	//windowクラス名
	wc.lpszClassName = L"CG2WindowClass";
	//インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	//カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	//windowクラスを登録する
	RegisterClass(&wc);

	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;
	RECT wrc = { 0, 0, kClientWidth, kClientHeight };
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);
	//windowsの生成
	HWND hwnd = CreateWindow(
		wc.lpszClassName, // 利用するクラス名
		L"CG2", // タイトルバーの文字
		WS_OVERLAPPEDWINDOW, // よく見るウィンドウタイトル
		CW_USEDEFAULT, // 表示X座標
		CW_USEDEFAULT, // 表示Y座標
		wrc.right - wrc.left, // ウィンドウ横幅
		wrc.bottom - wrc.top, // ウィンドウ縦幅
		nullptr, // 親ウィンドウハンドル
		nullptr, // メニューハンドル
		wc.hInstance, // インスタンスハンドル
		nullptr); // オプション

	ShowWindow(hwnd, SW_SHOW);


	#ifdef _DEBUG
		ID3D12Debug1* debugController = nullptr;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			debugController->SetEnableGPUBasedValidation(true);
		}


	

#endif // !_DEBUG


	//DXGIファクトリーの生成
	IDXGIFactory7* dxgiFactory = nullptr;
	//関数が成功したかどうかSUCCEEDマクロで判定できる
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	assert(SUCCEEDED(hr));
	//仕様するアダプタ用生成の変数。最初にnullptrを入れておく
	IDXGIAdapter4* useAsapter = nullptr;
	//良い順でアダプタを読む
	for (int i = 0; dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAsapter)) !=
		DXGI_ERROR_NOT_FOUND; ++i) 
	{//アダプタの情報を取得する
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAsapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));//取得できないのは一大事
		//ソフトウェアアダプタでなければ採用
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE))
		{
			//採用したアダプタ情報をログに出力 wstringの方なので注意
			Log(logStrem, ConvertString(std::format(L"use Adapater:{}\n", adapterDesc.Description)));
			break;
		}
		useAsapter = nullptr;//ソフトウェアアダプタのばあいは見なかったことにする
	}
	//見つからなかったので起動できない
	assert(useAsapter != nullptr);
		
	ID3D12Device* device = nullptr;
	//機能レベルとログの出力
	D3D_FEATURE_LEVEL featrueLevels[] =
	{ D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0 };
	const char* featrueLevelStrings[] = { "12.1", "12.1", "12.0" };
	for (size_t i = 0; i < _countof(featrueLevels); ++i)
	{
		//採用したアダプターでデバイスを生成
		hr = D3D12CreateDevice(useAsapter, featrueLevels[i], IID_PPV_ARGS(&device));
		//指定した機能レベルでデバイスが生成できたか確認
		if (SUCCEEDED(hr))
		{
			//静瀬尾できたのでループを抜ける
			Log(logStrem, (std::format("FeatrueLevel", featrueLevelStrings[i])));

				break;
		}
	}
	assert(device != nullptr);
	Log(logStrem, "complate crate D3D12Device!!!\n");//初期化ログを出す

#ifdef _DEBUG

	ID3D12InfoQueue* infoqueue = nullptr;

	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoqueue))))
	{
		//やばいときにエラーで止まる
		infoqueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		//エラー時に止まる
		infoqueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		//緊急時に止まる
		infoqueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
		D3D12_MESSAGE_ID denyids[] = 
		{
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};
		//抑制するレベル
		D3D12_MESSAGE_SEVERITY secerities[] = {D3D12_MESSAGE_SEVERITY_INFO};
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyids);
		filter.DenyList.pIDList = denyids;
		filter.DenyList.NumSeverities = _countof(secerities);
		filter.DenyList.pSeverityList = secerities;
		//指定したメッセージの表示を抑制する
		infoqueue->PushStorageFilter(&filter);

		//解放
		infoqueue->Release();
	}


#endif // !_DEBUG
	//コマンドキューを生成する
	ID3D12CommandQueue* commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc,
		IID_PPV_ARGS(&commandQueue));

	assert(SUCCEEDED(hr));
	
	//コマンドアロケータを生成する
	ID3D12CommandAllocator* commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	assert(SUCCEEDED(hr));

	//コマンドリストを作成する
	ID3D12GraphicsCommandList* commandList = nullptr;
	hr = device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr,
		IID_PPV_ARGS(&commandList));
	assert(SUCCEEDED(hr));

	//スラップチェインを作成する
	IDXGISwapChain4* swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;//画面幅
	swapChainDesc.Height = kClientHeight;//画面高さ
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;//色の形式
	swapChainDesc.SampleDesc.Count = 1;//マルチサンプルしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(&swapChain));
		assert(SUCCEEDED(hr));

		ID3D12DescriptorHeap* rtvDescrriptorHelp = nullptr;
		D3D12_DESCRIPTOR_HEAP_DESC rtvDescrriptorDesc{};
		rtvDescrriptorDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvDescrriptorDesc.NumDescriptors = 2;
		hr = device->CreateDescriptorHeap(&rtvDescrriptorDesc, IID_PPV_ARGS(&rtvDescrriptorHelp));
		assert(SUCCEEDED(hr));

		//swapChainからREsoucesを引っ張ってくる
		ID3D12Resource* swapChainResouces[2] = { nullptr };
		hr = swapChain->GetBuffer(0,IID_PPV_ARGS(&swapChainResouces[0]));
		assert(SUCCEEDED(hr));
		hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResouces[1]));
		assert(SUCCEEDED(hr));
		//rtvの設定
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;//出力結果をSRGBに変換して書き込む
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;//2dテクスチャとして書き込み

		D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescrriptorHelp->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
		//まず一つ目を作る
		rtvHandles[0] = rtvStartHandle;
		device->CreateRenderTargetView(swapChainResouces[0], &rtvDesc, rtvHandles[0]);
		rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		//二つ目を作る
		device->CreateRenderTargetView(swapChainResouces[1],& rtvDesc,rtvHandles[1]);

	

		//初期値で0Fenceを作る
		ID3D12Fence* fence = nullptr;
		uint64_t fenceValue = 0;
		hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
		assert(SUCCEEDED(hr));
	

		//fenceのsignalを持つためのイベントを作成する
		HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		assert(fenceEvent != nullptr);

	MSG msg{};

	//dxcompilerを初期化
	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils)); 
	assert(SUCCEEDED(hr));
	hr = hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));
	//現地点のincに対応するための設定が行っておく
	IDxcIncludeHandler* includeHander = nullptr;
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHander);
	assert(SUCCEEDED(hr));

	//rootsignantrue作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignatrue{};

	descriptionRootSignatrue.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	//シアライズしてばいなりにする
	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignatrue, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr))
	{
		Log(logStrem, reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}
	ID3D12RootSignature* rootsignatrue = nullptr;
	hr = device->CreateRootSignature(0,
		signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootsignatrue));
	assert(SUCCEEDED(hr));
	//inputLayout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[1] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);
	//BlendStateの設定
	D3D12_BLEND_DESC blendDescs{};
	blendDescs.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;
	//rasiterzerstateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	//裏面時計回りに表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	//三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	//shaderをコンパイルする
	IDxcBlob* vertexShaderBlob = CompileShander( L"Object3d.VS.hlsl",
		L"vs_6_0", dxcUtils, dxcCompiler,includeHander, logStrem);
	assert(vertexShaderBlob != nullptr);
	IDxcBlob* pixelShaderBlob = CompileShander(L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHander, logStrem);
	assert(pixelShaderBlob != nullptr);
	
	//psoを作成する
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootsignatrue;
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
	graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(),
	vertexShaderBlob->GetBufferSize() };
	graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(),
	pixelShaderBlob->GetBufferSize() };
	graphicsPipelineStateDesc.BlendState = blendDescs;
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;

	//巻き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	//利用するトロポジの情報のタイプ
	graphicsPipelineStateDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	
	//どのように画面に色を打ち込むのか設定
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	//実際に生成
	ID3D12PipelineState* graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));

	//頂点とリソース用のヒープ設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	//頂点リソースの設定
	D3D12_RESOURCE_DESC vertResoucesDesc{};
	//バッファーリソーステクスチャの場合は別の指定をする
	vertResoucesDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertResoucesDesc.Width = sizeof(Vector4) * 3;
	//バッファの場合は1にする
	vertResoucesDesc.Height = 1;
	vertResoucesDesc.DepthOrArraySize = 1;
	vertResoucesDesc.MipLevels = 1;
	vertResoucesDesc.SampleDesc.Count = 1;
	//バッファの場合はこれをする決まり
	vertResoucesDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	//実際に頂点リソースを作る
	ID3D12Resource* vertexResouces = nullptr;
	hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
		&vertResoucesDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResouces));
	assert(SUCCEEDED(hr));
	//頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	//リソースの先端アドレスから使う
	vertexBufferView.BufferLocation = vertexResouces->GetGPUVirtualAddress();
	//使用するリソースのサイズは頂点3つ分サイズ
	vertexBufferView.SizeInBytes = sizeof(Vector4) * 3;
	//1頂点当たりのサイズ
	vertexBufferView.StrideInBytes = sizeof(Vector4);
	
	//頂点リソースサイズデータに書き込む
	Vector4* vertexData = nullptr;
	//書き込むためのアドレスの取得
	vertexResouces->Map(0, nullptr, reinterpret_cast<VOID**>(&vertexData));
	//左上
	vertexData[0] = { -0.5f, -0.5f,0.0f,1.0f };
	//上
	vertexData[1] = { 0.0f, 0.5f, 0.0f, 1.0f };
	//右上
	vertexData[2] = { 0.5f, -0.5f, 0.0f, 1.0f };
	//ビューボート
	D3D12_VIEWPORT viewport{};
	//クライアント領域のサイズと一緒にして画面全体に表示
	viewport.Width = kClientWidth;
	viewport.Height = kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	//シザー矩形
	D3D12_RECT scissorRect{};
	scissorRect.left = 0;
	scissorRect.right = kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = kClientHeight;



			


	//windowの×ボタンが押されるまでループ
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else
		{
			//ゲームの処理

			//画面のクリア処理

			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();
			//TransitonBarrierの設定
			D3D12_RESOURCE_BARRIER barrier{};
			//今回のバリアはtransition
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			//Noneにしておく
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			//バリアを張る対象のリソース現在のバックバッファに対して行う
			barrier.Transition.pResource = swapChainResouces[backBufferIndex];
			//転移前の(現在)のResoucesStare
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			//転移後のResourceState
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			//transSitiionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			//画面先のrtvを設定する
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, nullptr);
			float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex],clearColor, 0, nullptr );
			//コマンドを積む
			commandList->RSSetViewports(1, &viewport);
			commandList->RSSetScissorRects(1, &scissorRect);
			//rootsignaltrueを設定　psoに設定しているけど別途設定が必要
			commandList->SetGraphicsRootSignature(rootsignatrue);
			commandList->SetPipelineState(graphicsPipelineState);
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
			//形状を設定psoに設定しているものとはまた別　同じものを設定するトロ考えておけば良い
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			//作画
			commandList->DrawInstanced(3, 1, 0, 0);

			//renderTarGetからPresentにする
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			//transSitiionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);
			
			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			ID3D12CommandList* commandLists[] = { commandList };
			commandQueue->ExecuteCommandLists(1, commandLists);
			swapChain->Present(1, 0);
		

			//Fenceの更新
			fenceValue++;
			//GPUがそこまでたどり着いた時に Fenceの値を指定した値に代入するようにsignalを送る
			commandQueue->Signal(fence, fenceValue);

			if (fence->GetCompletedValue() < fenceValue)
			{
				//指定したsignal似たとりついていないのでたどり着くまでに待つようにイベントを指定する
				fence->SetEventOnCompletion(fenceValue, fenceEvent);
				//イベントを待つ
				WaitForSingleObject(fenceEvent, INFINITE);
			}

			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));
			hr = commandList->Reset(commandAllocator, nullptr);
			assert(SUCCEEDED(hr));
			
		
		}


	}


	Log(logStrem,"Hello,DirectX!\n");
	Log(logStrem,
		ConvertString(
		std::format(
			L"clientSize:{} {}\n",
			kClientWidth,
			kClientHeight)));

	//解放処理
	CloseHandle(fenceEvent);
	fence->Release();
	rtvDescrriptorHelp->Release();
	swapChainResouces[0]->Release();
	swapChainResouces[1]->Release();
	swapChain->Release();
	commandList->Release();
	commandAllocator->Release();
	commandQueue->Release();
	device->Release();
	useAsapter->Release();
	dxgiFactory->Release();
	vertexResouces->Release();
	graphicsPipelineState->Release();
	signatureBlob->Release();
	if (errorBlob)
	{
		errorBlob->Release();
	}
	rootsignatrue->Release();
	pixelShaderBlob->Release();
	vertexShaderBlob->Release();
#ifdef _DEBUG

	debugController->Release();
#endif // _DEBUG
	CloseWindow(hwnd);

	//リソースチェック
	IDXGIDebug* debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
	{
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}
	infoqueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
	return 0;
}


