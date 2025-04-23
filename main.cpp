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

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Dbghelp.lib")

//BOOL MiniDumpWriteDump(
//	[in] HANDLE                            hProcess,
//	[in] DWORD                             ProcessId,
//	[in] HANDLE                            hFile,
//	[in] MINIDUMP_TYPE                     DumpType,
//	[in] PMINIDUMP_EXCEPTION_INFORMATION   ExceptionParam,
//	[in] PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
//	[in] PMINIDUMP_CALLBACK_INFORMATION    CallbackParam
//);


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
		rtvHandles[0] = rtvStartHandle;
		device->CreateRenderTargetView(swapChainResouces[0], &rtvDesc, rtvHandles[0]);
		rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		device->CreateRenderTargetView(swapChainResouces[1],& rtvDesc,rtvHandles[1]);



	MSG msg{};
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

		}

	}


	Log(logStrem,"Hello,DirectX!\n");
	Log(logStrem,
		ConvertString(
		std::format(
			L"clientSize:{} {}\n",
			kClientWidth,
			kClientHeight)));

	return 0;
}


