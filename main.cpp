#define DIRECTINPUT_VERSION 0x0800

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
#include "externals//DirectXTex/d3dx12.h"
#include <vector>
#include <sstream>
#include <wrl.h>
#include<xaudio2.h>
#include <dinput.h>
#include "AudioPlayer.h"
#include "Math.h"
#include "debugCamera.h"
#include "InputManager.h"



#include "externals/imgui/imgui.h"
#include"externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#include "externals/DirectXTex/DirectXTex.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);






#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")




struct Transform
{
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

struct VertexData
{
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

struct Material
{
	Vector4 color;
	int32_t enableLighting;
	float padding1[3];
	Matrix4x4 uvTransform;
    int32_t selectedLighting;
    float padding2[3];

};

struct TransformationMatrix
{
	Matrix4x4 WVP;
	Matrix4x4 world;
};

struct DirectionalLight
{
	Vector4 color;//ライトの色
	Vector3 direction;//ライトの向き
	float intensity;//光度
};

struct MateriaData
{
	std::string textureFilePath;
};
struct  ModelData
{
	std::vector<VertexData> vertices;
	MateriaData material;
};



struct D3DResourceLeakChecker {
	~D3DResourceLeakChecker() {


		//リソースチェック
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
		{
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);

		}
	}
};


static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception)
{
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = { 0 };
	CreateDirectory(L"./DUMPS", nullptr);
	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d%02d-%02d%02d.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
	HANDLE dumpFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{ 0 };
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

	// UTF-8からUTF-16への変換
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

//resouces作成の関数
const Microsoft::WRL::ComPtr<ID3D12Resource> createBufferResouces(const Microsoft::WRL::ComPtr<ID3D12Device>& device, size_t sizeInBytes)
{
	//頂点とリソース用のヒープ設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	//頂点リソースの設定
	D3D12_RESOURCE_DESC vertResoucesDesc{};
	//バッファーリソーステクスチャの場合は別の指定をする
	vertResoucesDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertResoucesDesc.Width = sizeInBytes * 3;
	//バッファの場合は1にする
	vertResoucesDesc.Height = 1;
	vertResoucesDesc.DepthOrArraySize = 1;
	vertResoucesDesc.MipLevels = 1;
	vertResoucesDesc.SampleDesc.Count = 1;
	//バッファの場合はこれをする決まり
	vertResoucesDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	//実際に頂点リソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResouces = nullptr;
	HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
		&vertResoucesDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResouces));
	assert(SUCCEEDED(hr));
	return vertexResouces;
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextResouces(const Microsoft::WRL::ComPtr<ID3D12Device>& device, int32_t width, int32_t height)
{
	D3D12_RESOURCE_DESC resoucesDesc{};
	resoucesDesc.Width = width;//textureの幅
	resoucesDesc.Height = height;//textrueの長さ
	resoucesDesc.MipLevels = 1;//wipmapの数
	resoucesDesc.DepthOrArraySize = 1;//奥行き or 配列textureの配列数
	resoucesDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;//Depthstencilとして使用可能なフォーマット
	resoucesDesc.SampleDesc.Count = 1;//サンプりングカウント 1 固定
	resoucesDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;// 2次元
	resoucesDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;//depthStenci1として使う通知

	//利用するheap設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;//vram上に作る

	//深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;//フォーマット resoucesに合わせる

	//resoucesの作成
	Microsoft::WRL::ComPtr<ID3D12Resource> resouces = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,//heapの設定
		D3D12_HEAP_FLAG_NONE,//heapの特殊設定
		&resoucesDesc,//resoucesの設定
		D3D12_RESOURCE_STATE_DEPTH_WRITE,//震度値ヲ書き込む状態にしておく
		&depthClearValue,//clear最適値
		IID_PPV_ARGS(&resouces));
	assert(SUCCEEDED(hr));
	return resouces;
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

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>CreateDescriptorHeap(
	const Microsoft::WRL::ComPtr<ID3D12Device>& device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible
)
{
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescriptors;
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	return descriptorHeap;
}






LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg,
	WPARAM wparam, LPARAM lparam)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
	{
		return true;
	}

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


Microsoft::WRL::ComPtr<IDxcBlob> CompileShander(
	const std::wstring& filePath,
	const wchar_t* profile,
const	Microsoft::WRL::ComPtr<IDxcUtils>& dxcUtils,
const	Microsoft::WRL::ComPtr<IDxcCompiler3>& dxCompiler,
const	Microsoft::WRL::ComPtr<IDxcIncludeHandler>& includeHandler,
	std::ostream& os)
{
	//１hlslファイルを読み込む
	Log(os, ConvertString(std::format(L"Begin CompileShader,path:{}, profile:{}\n", filePath, profile)));
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
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

	Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
	hr = dxCompiler->Compile(
		&shaderSoucesBuffer,
		argument,
		_countof(argument),
		includeHandler.Get(),
		IID_PPV_ARGS(&shaderResult));

	assert(SUCCEEDED(hr));
	//３警告エラーが出てないか確認する
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> shanderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shanderError), nullptr);
	if (shanderError != nullptr && shanderError->GetStringLength() != 0)
	{
		Log(os, shanderError->GetStringPointer());
		assert(false);

	}
	//４Compile結果を受けて返す
	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	Log(os, ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));
	return shaderBlob.Get();

}

DirectX::ScratchImage LoadTexture(const std::string& filePath)
{
	
	//テクスチャファイルを読み込んでプログラムで扱えるようにする
	DirectX::ScratchImage image{};
	DirectX::ScratchImage mipImages{};
	std::wstring filePathW = ConvertString(filePath);
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
		materialData.Dimension =D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		//白色のテクスチャを作成
		DirectX::Image whiteImage;
		whiteImage.width = 1;
		whiteImage.height = 1;
		whiteImage.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		whiteImage.rowPitch = 4;
		whiteImage.slicePitch = 4;

		uint8_t* pixels =  new uint8_t[4]{255, 255, 255, 255} ;
		whiteImage.pixels = pixels;

		image.InitializeFromImage(whiteImage);
		mipImages.InitializeFromImage(whiteImage);
		delete pixels;
		return mipImages;

	}

	//ミニマップの作成
	
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));
	//ミップマップのデータを返す
	return mipImages;
}

Microsoft::WRL::ComPtr<ID3D12Resource> createTextreResouces(const Microsoft::WRL::ComPtr<ID3D12Device>& device, const DirectX::TexMetadata& metadata)
{
	//metadataを軸にResoucesの設定
	D3D12_RESOURCE_DESC resouceDesc{};
	resouceDesc.Width = UINT(metadata.width);//textreの幅
	resouceDesc.Height = UINT(metadata.height);//textreの幅
	resouceDesc.MipLevels = UINT(metadata.mipLevels);//mipmapの数
	resouceDesc.DepthOrArraySize = UINT16(metadata.arraySize);//奥行き or 配列のtextreの配列数
	resouceDesc.Format = metadata.format;//textreのformat
	resouceDesc.SampleDesc.Count = 1;//サンプリングカウント1固定
	resouceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);//textreの次元数　普段使っているのは2次元

	//利用するheapの設定　非常に特殊な運用　02_04exで一般ケース版がある
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;//細かい設定お行う


	//resoucesの作成
	Microsoft::WRL::ComPtr<ID3D12Resource> resouce = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,//heapの設定
		D3D12_HEAP_FLAG_NONE,//heapの特殊設定
		&resouceDesc,//Resoucesの設定
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&resouce));//初回Resoucesstate textre破棄本読むだけ
	assert(SUCCEEDED(hr));
	return resouce;
}

void UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages)
{
	//meta情報
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

	//全mipdataについて
	for (size_t mipLevel = 0; mipLevel < metadata.mipLevels; mipLevel++)
	{
		const DirectX::Image* img = mipImages.GetImage(mipLevel, 0, 0);
		//textureに転送
		HRESULT hr = texture->WriteToSubresource
		(
			UINT(mipLevel),//全領域へコピー
			nullptr,
			img->pixels,//元データアドレス
			UINT(img->rowPitch),//1ラインサイズ
			UINT(img->slicePitch)//1枚サイズ

		);
		assert(SUCCEEDED(hr));
	}
}

[[nodiscard]]
Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureDeta(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages, const Microsoft::WRL::ComPtr<ID3D12Device>& device, const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList)
{
	std::vector<D3D12_SUBRESOURCE_DATA> subresouces;
	DirectX::PrepareUpload(device.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresouces);
	uint64_t intermeddiatesize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresouces.size()));
	Microsoft::WRL::ComPtr<ID3D12Resource> intermeddiaetResouces = createBufferResouces(device, intermeddiatesize);
	UpdateSubresources(commandList.Get(), texture.Get(), intermeddiaetResouces.Get(), 0, 0, UINT(subresouces.size()), subresouces.data());

	//textureへの転換後利用できるようにD3D12_RESOUCES_STATE_COPY_DESTからD3D12_RESOUCES_STATE_GENERIC_READへResoucesStateに変更する
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList->ResourceBarrier(1, &barrier);
	return intermeddiaetResouces;
}

// CPU GPU の関数化
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(const Microsoft::WRL::ComPtr <ID3D12DescriptorHeap>& descriptHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetGpudescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}


MateriaData LoadMaterialTemplatFile(const std::string& directoryPath, const std::string& filename)
{
	//中で必要となる変数宣言
	MateriaData materialData;
	std::string line;
	//ファイルを開く
	std::ifstream file(directoryPath + "/" + filename);//ファイルを開く
	assert(file.is_open());
	//実際にファイルをを読み　materialDAtaをこうつくしていく
	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		//identiflerに応じた処理
		if (identifier == "map_Kd")
		{
			std::string textureFilename;
			s >> textureFilename;
			//連結してファイルパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFilename;
		}
	}
	return materialData;
}

ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename)
{
	// モデルデータ
	ModelData modelData;

	// 一時データ格納用
	std::vector<Vector4> positions;
	std::vector<Vector3> normals;
	std::vector<Vector2> texcoords;
	std::string line;

	// .objファイルを開く
	std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());

	// 頂点トークンを解析して VertexData を構築するローカル関数
	auto ParseVertex = [&](const std::string& token) -> VertexData {
		VertexData vertex{};
		std::istringstream v(token);
		std::string indexStr;
		int indices[3] = { 0, 0, 0 };  // position/uv/normal
		int i = 0;

		while (std::getline(v, indexStr, '/') && i < 3) {
			if (!indexStr.empty()) {
				indices[i] = std::stoi(indexStr);
			}
			++i;
		}

		// position
		if (indices[0] > 0) {
			vertex.position = positions[indices[0] - 1];
		}

		// texcoord
		if (i > 1 && indices[1] > 0) {
			vertex.texcoord = texcoords[indices[1] - 1];
		} else {
			vertex.texcoord = { 0.0f, 0.0f };
		}

		// normal
		if (i > 2 && indices[2] > 0) {
			vertex.normal = normals[indices[2] - 1];
		} else {
			vertex.normal = { 0.0f, 1.0f, 0.0f };
		}

		return vertex;
		};

	// ファイル読み込みループ
	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if (identifier == "v") {
			Vector4 position{};
			s >> position.x >> position.y >> position.z;
			position.x *= -1.0f;
			position.w = 1.0f;
			positions.push_back(position);

		} else if (identifier == "vt") {
			Vector2 texcoord{};
			s >> texcoord.x >> texcoord.y;
			texcoord.y = 1.0f - texcoord.y;
			texcoords.push_back(texcoord);

		} else if (identifier == "vn") {
			Vector3 normal{};
			s >> normal.x >> normal.y >> normal.z;
			normal.x *= -1.0f;
			normals.push_back(normal);

		} else if (identifier == "f") {
			std::vector<std::string> vertexTokens;
			std::string vertexToken;

			// 1行にある頂点トークンをすべて取得
			while (s >> vertexToken) {
				vertexTokens.push_back(vertexToken);
			}

			// 三角形のみ対応、それ以外は無視
			if (vertexTokens.size() == 3) {
				// 頂点の順序を反転して右手系→左手系変換（必要なければ逆にしない）
				VertexData v0 = ParseVertex(vertexTokens[2]);
				VertexData v1 = ParseVertex(vertexTokens[1]);
				VertexData v2 = ParseVertex(vertexTokens[0]);

				modelData.vertices.push_back(v0);
				modelData.vertices.push_back(v1);
				modelData.vertices.push_back(v2);
			}

		} else if (identifier == "mtllib") {
			std::string materialFilename;
			s >> materialFilename;
			modelData.material = LoadMaterialTemplatFile(directoryPath, materialFilename);
		}
	}

	return modelData;
}

//初期化
Transform transformObj = { {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f},{0.0f, 0.0f, 0.0f} };

// 初期化
Transform transformSphire = { {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f},{1.0f, 0.0f, 0.0f} };



int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

	D3DResourceLeakChecker leakChecker;


	// DXの初期化とComPtrによるリソース生成
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory;
	Microsoft::WRL::ComPtr<ID3D12Device> device;

	AudioPlayer* audioPlayer = new AudioPlayer();
	Math* math = new Math();

	// COMライブラリの初期化 
	HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	// XAudio2 の初期化
	Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
	IXAudio2MasteringVoice* masteringVoice = nullptr;

	result = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);


	result = xAudio2->CreateMasteringVoice(&masteringVoice);

	
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
	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(true);
	}




#endif // !_DEBUG



	//関数が成功したかどうかSUCCEEDマクロで判定できる
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	assert(SUCCEEDED(hr));
	//仕様するアダプタ用生成の変数。最初にnullptrを入れておく
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAsapter = nullptr;
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


	//機能レベルとログの出力
	D3D_FEATURE_LEVEL featrueLevels[] =
	{ D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0 };
	const char* featrueLevelStrings[] = { "12.1", "12.1", "12.0" };
	for (size_t i = 0; i < _countof(featrueLevels); ++i)
	{
		//採用したアダプターでデバイスを生成
		hr = D3D12CreateDevice(useAsapter.Get(), featrueLevels[i], IID_PPV_ARGS(&device));
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

	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoqueue = nullptr;

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
		D3D12_MESSAGE_SEVERITY secerities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyids);
		filter.DenyList.pIDList = denyids;
		filter.DenyList.NumSeverities = _countof(secerities);
		filter.DenyList.pSeverityList = secerities;
		//指定したメッセージの表示を抑制する
		infoqueue->PushStorageFilter(&filter);


	}


#endif // !_DEBUG

	//コマンドキューを生成する
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc,
		IID_PPV_ARGS(&commandQueue));

	assert(SUCCEEDED(hr));

	//コマンドアロケータを生成する
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	assert(SUCCEEDED(hr));

	//コマンドリストを作成する
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr,
		IID_PPV_ARGS(&commandList));
	assert(SUCCEEDED(hr));

	//スラップチェインを作成する
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;//画面幅
	swapChainDesc.Height = kClientHeight;//画面高さ
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;//色の形式
	swapChainDesc.SampleDesc.Count = 1;//マルチサンプルしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));
	assert(SUCCEEDED(hr));

	//DEscriptSizeを取得しておく
	const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);


	//rtv用ののヒーブでくりぷの数は２RTV はShader内で触るものではないのでShaderVisbleはfalse
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescrriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
	//SRV用の
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescrriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	//DSV用のヒープでの数は1 DSVShader内で触るまではないので ShaderVisiはfalse
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);




	//swapChainからREsoucesを引っ張ってくる
	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResouces[2] = { nullptr };
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResouces[0]));
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResouces[1]));
	assert(SUCCEEDED(hr));
	//rtvの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;//出力結果をSRGBに変換して書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;//2dテクスチャとして書き込み


	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescrriptorHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	//まず一つ目を作る
	// 0番目のRTVハンドルを取得
	rtvHandles[0] = GetCPUDescriptorHandle(rtvDescrriptorHeap.Get(), descriptorSizeRTV, 0);
	device->CreateRenderTargetView(swapChainResouces[0].Get(), &rtvDesc, rtvHandles[0]);

	// 1番目のRTVハンドルを取得
	rtvHandles[1] = GetCPUDescriptorHandle(rtvDescrriptorHeap.Get(), descriptorSizeRTV, 1);
	device->CreateRenderTargetView(swapChainResouces[1].Get(), &rtvDesc, rtvHandles[1]);




	//初期値で0Fenceを作る
	Microsoft::WRL::ComPtr<ID3D12Fence> fence = nullptr;
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));


	//fenceのsignalを持つためのイベントを作成する
	HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);

	MSG msg{};

	//dxcompilerを初期化
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils = nullptr;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler = nullptr;
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


	//descriptorRangeによる一括設定
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;//0から始まる
	descriptorRange[0].NumDescriptors = 1;//数は一つ
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;//srvを使う
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;//自動計算

	//Rootparameter作成　複数設定できるので配列 今回は結果が一つだけなので長さが1の配列
	D3D12_ROOT_PARAMETER rootParmeters[4] = {};
	rootParmeters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;//CBVを使う
	rootParmeters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;//pixeShaderで使う
	rootParmeters[0].Descriptor.ShaderRegister = 0;//レジスタ番号と0バインド

	rootParmeters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;//CVBを使う
	rootParmeters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;//vertexShaerで使う
	rootParmeters[1].Descriptor.ShaderRegister = 0;//レジスタ番号0を使う

	rootParmeters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;//descriptorTableを使う
	rootParmeters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParmeters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParmeters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);//tableで利用する数

	rootParmeters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParmeters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParmeters[3].Descriptor.ShaderRegister = 1;


	descriptionRootSignatrue.pParameters = rootParmeters;//ルートパラメーターへのポインタ
	descriptionRootSignatrue.NumParameters = _countof(rootParmeters);//配列の長さ

	//samplerの設定お行う
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;//バイリニアフィルター
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;//0 ~1の範囲外をリピート 
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;//比較
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[0].ShaderRegister = 0;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	descriptionRootSignatrue.pStaticSamplers = staticSamplers;
	descriptionRootSignatrue.NumStaticSamplers = _countof(staticSamplers);



	//シアライズしてばいなりにする>
	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignatrue, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr))
	{
		Log(logStrem, reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootsignatrue = nullptr;
	hr = device->CreateRootSignature(0,
		signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootsignatrue));
	assert(SUCCEEDED(hr));
	//inputLayout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[2].SemanticName = "normal";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;



	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);
	//BlendStateの設定
	D3D12_BLEND_DESC blendDescs{};
	blendDescs.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDescs.RenderTarget[0].BlendEnable = true;
	blendDescs.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDescs.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDescs.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blendDescs.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDescs.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDescs.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

	//rasiterzerstateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};

	//裏面時計回りに表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;

	//三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	//shaderをコンパイルする
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShander(L"Object3d.VS.hlsl",
		L"vs_6_0", dxcUtils, dxcCompiler, includeHander, logStrem);
	assert(vertexShaderBlob != nullptr);
	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShander(L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHander, logStrem);
	assert(pixelShaderBlob != nullptr);

	//psoを作成する
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootsignatrue.Get();
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

	//DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	//Depthの機能を有効かする
	depthStencilDesc.DepthEnable = true;
	//書き込みする
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	//比較関数はlessWqual つまり近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	//DeptStencilの設定
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	//実際に生成
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));


	//頂点とリソース用のヒープ設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;


	const uint32_t kSubdivision = 16; // 分割数
	// 球体用の頂点数を定義
	const uint32_t kSphereVertexCount = kSubdivision * kSubdivision * 6;
	const float kPi = 3.14159265359f;
	const float kLonEvery = kPi * 2.0f / static_cast<float>(kSubdivision); // 経度方向の角度
	const float kLatEvery = kPi / static_cast<float>(kSubdivision);       // 緯度方向の角度

	// ★Sphereの頂点とインデックスを格納するvectorを宣言
	std::vector<VertexData> sphereVertices;
	std::vector<uint32_t> sphereIndices;

	// 事前にメモリを確保してパフォーマンスを向上させる
	sphereVertices.reserve((kSubdivision + 1) * (kSubdivision + 1));
	sphereIndices.reserve(kSubdivision * kSubdivision * 6); // 各四角形に2つの三角形、1つの三角形に3つのインデックス

	// ユニークな頂点を計算してvectorに格納
	for (uint32_t latIndex = 0; latIndex <= kSubdivision; ++latIndex) {
		float lat = -kPi / 2.0f + kLatEvery * latIndex; // 現在の緯度
		for (uint32_t lonIndex = 0; lonIndex <= kSubdivision; ++lonIndex) {
			float lon = lonIndex * kLonEvery; // 現在の経度

			VertexData vertex;
			vertex.position = {
				std::cos(lat) * std::cos(lon),
				std::sin(lat),
				std::cos(lat) * std::sin(lon),
				1.0f
			};
			vertex.texcoord = {
				static_cast<float>(lonIndex) / kSubdivision,
				1.0f - static_cast<float>(latIndex) / kSubdivision
			};
			vertex.normal = math-> Normalize({ vertex.position.x, vertex.position.y, vertex.position.z });
			sphereVertices.push_back(vertex); // 計算した頂点データを動的配列に追加します。
		}
	}

	// インデックスを計算してvectorに格納
	uint32_t numVerticesAlongLat = kSubdivision + 1;// 緯度方向の一列あたりの頂点数を定義します。


	for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
			uint32_t i0 = latIndex * numVerticesAlongLat + lonIndex;
			uint32_t i1 = latIndex * numVerticesAlongLat + lonIndex + 1;
			uint32_t i2 = (latIndex + 1) * numVerticesAlongLat + lonIndex;
			uint32_t i3 = (latIndex + 1) * numVerticesAlongLat + lonIndex + 1;

			//indexの追加
			sphereIndices.push_back(i0);
			sphereIndices.push_back(i2);
			sphereIndices.push_back(i1);

			sphereIndices.push_back(i1);
			sphereIndices.push_back(i2);
			sphereIndices.push_back(i3);
		}
	}

	//// ----------------------------
// Plane モデル読み込み
// ----------------------------
	ModelData modelPlaneData = LoadObjFile("resouces", "plane.obj");

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexplaneResource =
		createBufferResouces(device, sizeof(VertexData) * modelPlaneData.vertices.size());

	D3D12_VERTEX_BUFFER_VIEW vertexPlaneBufferView{};
	vertexPlaneBufferView.BufferLocation = vertexplaneResource->GetGPUVirtualAddress();
	vertexPlaneBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelPlaneData.vertices.size());
	vertexPlaneBufferView.StrideInBytes = sizeof(VertexData);

	VertexData* vertexPlaneData = nullptr;
	vertexplaneResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexPlaneData));
	std::memcpy(vertexPlaneData, modelPlaneData.vertices.data(), sizeof(VertexData) * modelPlaneData.vertices.size());
	vertexplaneResource->Unmap(0, nullptr);

	// ----------------------------
	// Teapot モデル読み込み
	// ----------------------------

	ModelData modelTeapotData = LoadObjFile("resouces", "teapot.obj");

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexTeapotResource =
		createBufferResouces(device, sizeof(VertexData) * modelTeapotData.vertices.size());

	D3D12_VERTEX_BUFFER_VIEW vertexTeapotBufferView{};
	vertexTeapotBufferView.BufferLocation = vertexTeapotResource->GetGPUVirtualAddress();
	vertexTeapotBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelTeapotData.vertices.size());
	vertexTeapotBufferView.StrideInBytes = sizeof(VertexData);

	VertexData* vertexTeapotData = nullptr;
	vertexTeapotResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexTeapotData));
	std::memcpy(vertexTeapotData, modelTeapotData.vertices.data(), sizeof(VertexData) * modelTeapotData.vertices.size());
	vertexTeapotResource->Unmap(0, nullptr);

	// ----------------------------
	// Bunny モデル読み込み
	// ----------------------------
	ModelData modelbunnyData = LoadObjFile("resouces", "bunny.obj");

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexbunnyResource =
		createBufferResouces(device, sizeof(VertexData) * modelbunnyData.vertices.size());

	D3D12_VERTEX_BUFFER_VIEW vertexbunnyBufferView{};
	vertexbunnyBufferView.BufferLocation = vertexbunnyResource->GetGPUVirtualAddress();
	vertexbunnyBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelbunnyData.vertices.size());
	vertexbunnyBufferView.StrideInBytes = sizeof(VertexData);

	VertexData* vertexbunnyData = nullptr;
	vertexbunnyResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexbunnyData));
	std::memcpy(vertexbunnyData, modelbunnyData.vertices.data(), sizeof(VertexData) * modelbunnyData.vertices.size());
	vertexbunnyResource->Unmap(0, nullptr);

	// ----------------------------
	// MultiMesh モデル読み込み
	// ----------------------------
	ModelData modelMultiMeshData = LoadObjFile("resouces", "multiMesh.obj");

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexMultiMeshResource =
		createBufferResouces(device, sizeof(VertexData) * modelMultiMeshData.vertices.size());

	D3D12_VERTEX_BUFFER_VIEW vertexMultiMeshBufferView{};
	vertexMultiMeshBufferView.BufferLocation = vertexMultiMeshResource->GetGPUVirtualAddress();
	vertexMultiMeshBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelMultiMeshData.vertices.size());
	vertexMultiMeshBufferView.StrideInBytes = sizeof(VertexData);

	VertexData* vertexMultiMeshData = nullptr;
	vertexMultiMeshResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexMultiMeshData));
	std::memcpy(vertexMultiMeshData, modelMultiMeshData.vertices.data(), sizeof(VertexData) * modelMultiMeshData.vertices.size());
	vertexMultiMeshResource->Unmap(0, nullptr);

	// ----------------------------
	// MultiMaterial モデル読み込み
	// ----------------------------
	ModelData modelMultiMaterialData = LoadObjFile("resouces", "multiMaterial.obj");

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexMultiMaterialResource =
		createBufferResouces(device, sizeof(VertexData) * modelMultiMaterialData.vertices.size());

	D3D12_VERTEX_BUFFER_VIEW vertexMultiMaterialBufferView{};
	vertexMultiMaterialBufferView.BufferLocation = vertexMultiMaterialResource->GetGPUVirtualAddress();
	vertexMultiMaterialBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelMultiMaterialData.vertices.size());
	vertexMultiMaterialBufferView.StrideInBytes = sizeof(VertexData);

	VertexData* vertexMultiMaterialData = nullptr;
	vertexMultiMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexMultiMaterialData));
	std::memcpy(vertexMultiMaterialData, modelMultiMaterialData.vertices.data(), sizeof(VertexData) * modelMultiMaterialData.vertices.size());
	vertexMultiMaterialResource->Unmap(0, nullptr);

	// ----------------------------
	// Suzanne モデル読み込み
	// ----------------------------
	ModelData modelSuzanneData = LoadObjFile("resouces", "suzanne.obj");

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexSuzanneResource =
		createBufferResouces(device, sizeof(VertexData) * modelSuzanneData.vertices.size());

	D3D12_VERTEX_BUFFER_VIEW vertexSuzanneBufferView{};
	vertexSuzanneBufferView.BufferLocation = vertexSuzanneResource->GetGPUVirtualAddress();
	vertexSuzanneBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelSuzanneData.vertices.size());
	vertexSuzanneBufferView.StrideInBytes = sizeof(VertexData);

	VertexData* vertexSuzanneData = nullptr;
	vertexSuzanneResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexSuzanneData));
	std::memcpy(vertexSuzanneData, modelSuzanneData.vertices.data(), sizeof(VertexData) * modelSuzanneData.vertices.size());
	vertexSuzanneResource->Unmap(0, nullptr);

	// ----------------------------
	// Sphere モデル生成
	// ----------------------------
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSphere =
		createBufferResouces(device, sizeof(VertexData) * sphereVertices.size());

	VertexData* mappedVertexDataSphere = nullptr;
	vertexResourceSphere->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertexDataSphere));
	std::copy(sphereVertices.begin(), sphereVertices.end(), mappedVertexDataSphere);
	vertexResourceSphere->Unmap(0, nullptr);

	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSphere{};
	vertexBufferViewSphere.BufferLocation = vertexResourceSphere->GetGPUVirtualAddress();
	vertexBufferViewSphere.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * sphereVertices.size());
	vertexBufferViewSphere.StrideInBytes = sizeof(VertexData);

	// インデックスバッファ
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSphere =
		createBufferResouces(device, sizeof(uint32_t) * sphereIndices.size());

	uint32_t* mappedIndexDataSphere = nullptr;
	indexResourceSphere->Map(0, nullptr, reinterpret_cast<void**>(&mappedIndexDataSphere));
	std::copy(sphereIndices.begin(), sphereIndices.end(), mappedIndexDataSphere);
	indexResourceSphere->Unmap(0, nullptr);

	D3D12_INDEX_BUFFER_VIEW indexBufferViewSphere{};
	indexBufferViewSphere.BufferLocation = indexResourceSphere->GetGPUVirtualAddress();
	indexBufferViewSphere.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * sphereIndices.size());
	indexBufferViewSphere.Format = DXGI_FORMAT_R32_UINT;

	// ----------------------------
	// Sprite用頂点リソース作成
	// ----------------------------
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResoucesSptite =
		createBufferResouces(device, sizeof(VertexData) * 4);

	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
	vertexBufferViewSprite.BufferLocation = vertexResoucesSptite->GetGPUVirtualAddress();
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 4;  // 6頂点（三角形2つ分）想定
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	

	// =============================
	// ▼ Obj用 WVP リソースの作成
	// =============================
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpObjResouces = createBufferResouces(device, sizeof(TransformationMatrix));
	TransformationMatrix* wvpObjData = nullptr;
	wvpObjResouces->Map(0, nullptr, reinterpret_cast<void**>(&wvpObjData));
	wvpObjData->WVP = math->makeIdentity4x4();
	wvpObjData->world = math->MakeAffineMatrix(transformObj.scale, transformObj.rotate, transformObj.translate);

	// =============================
	// ▼ 球体用 WVP リソースの作成
	// =============================
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpSphireResouces = createBufferResouces(device, sizeof(TransformationMatrix));
	TransformationMatrix* wvpSphireData = nullptr;
	wvpSphireResouces->Map(0, nullptr, reinterpret_cast<void**>(&wvpSphireData));
	wvpSphireData->WVP = math->makeIdentity4x4();
	wvpSphireData->world = math->MakeAffineMatrix(transformSphire.scale, transformSphire.rotate, transformSphire.translate);

	// =============================
	// ▼ スプライト用マテリアルリソース
	// =============================
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResoucesSprite = createBufferResouces(device, sizeof(Material));
	Material* materialDataSprite = nullptr;
	materialResoucesSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
	materialDataSprite->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialDataSprite->enableLighting = false;  // スプライトはライティング無し
	materialDataSprite->uvTransform = math->makeIdentity4x4();
	materialResoucesSprite->Unmap(0, nullptr);

	// =============================
	// ▼ ディレクショナルライト リソース
	// =============================
	Microsoft::WRL::ComPtr<ID3D12Resource> DirectionalLightResouces = createBufferResouces(device, sizeof(DirectionalLight));
	DirectionalLight* directLightData = nullptr;
	DirectionalLightResouces->Map(0, nullptr, reinterpret_cast<void**>(&directLightData));
	directLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directLightData->direction = { 0.0f, -1.0f, 0.0f };
	directLightData->direction = math->Normalize(directLightData->direction);
	directLightData->intensity = 1.0f;
	DirectionalLightResouces->Unmap(0, nullptr);

	// =============================
	// ▼ 通常マテリアルリソース（モデル/オブジェクト用）
	// =============================

	//マテリアル用のリソースを作る　今回はcolor一つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResouces = createBufferResouces(device, sizeof(Material));
	Material* materialData = nullptr;
	materialResouces->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData->enableLighting = true;
	materialData->selectedLighting = 0;
	materialData->uvTransform = math->makeIdentity4x4();
	materialResouces->Unmap(0, nullptr);

	// =============================
	// ▼ スプライト用インデックスバッファの作成
	// =============================
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResoucesSprite = createBufferResouces(device, sizeof(uint32_t) * 6);
	D3D12_INDEX_BUFFER_VIEW indexBufferView{};
	indexBufferView.BufferLocation = indexResoucesSprite->GetGPUVirtualAddress();
	indexBufferView.SizeInBytes = sizeof(uint32_t) * 6;
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;

	// ▼ インデックスデータ書き込み（2枚の三角形）
	uint32_t* indexDataSprite = nullptr;
	indexResoucesSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0; indexDataSprite[1] = 1; indexDataSprite[2] = 2;
	indexDataSprite[3] = 1; indexDataSprite[4] = 3; indexDataSprite[5] = 2;

	// =============================
	// ▼ スプライト用頂点バッファデータ設定
	// =============================
	VertexData* vertexDataSprite = nullptr;
	vertexResoucesSptite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

	// ▲ 左上 (0, 0)
	vertexDataSprite[0].position = { 0.0f, 0.0f, 0.0f, 1.0f };
	vertexDataSprite[0].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[0].normal = { 0.0f, 0.0f, -1.0f };

	// ▲ 右上 (640, 0)
	vertexDataSprite[1].position = { 640.0f, 0.0f, 0.0f, 1.0f };
	vertexDataSprite[1].texcoord = { 1.0f, 0.0f };
	vertexDataSprite[1].normal = { 0.0f, 0.0f, -1.0f };

	// ▲ 左下 (0, 360)
	vertexDataSprite[2].position = { 0.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[2].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[2].normal = { 0.0f, 0.0f, -1.0f };

	// ▲ 右下 (640, 360)
	vertexDataSprite[3].position = { 640.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[3].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[3].normal = { 0.0f, 0.0f, -1.0f };

	// =============================
	// ▼ スプライト用 WVP（2D行列）リソース
	// =============================
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResoucesSprite = createBufferResouces(device, sizeof(Matrix4x4));
	Matrix4x4* transformationMatrixDataSprite = nullptr;
	transformationMatrixResoucesSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	*transformationMatrixDataSprite = math->makeIdentity4x4();  // 初期値は単位行列

	// =============================
	// ▼ ビューポートとシザー矩形の設定
	// =============================
	D3D12_VIEWPORT viewport{};
	viewport.Width = kClientWidth;
	viewport.Height = kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	D3D12_RECT scissorRect{};
	scissorRect.left = 0;
	scissorRect.right = kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = kClientHeight;

	// =============================
	// ▼ ImGui 初期化
	// =============================
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(
		device.Get(),
		swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescrriptorHeap.Get(),
		srvDescrriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescrriptorHeap->GetGPUDescriptorHandleForHeapStart()
	);

	    //----------------------------
		// テクスチャ1: uvChecker.png 読み込み & リソース作成
		// ----------------------------
	DirectX::ScratchImage mipImages = LoadTexture("resouces/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResouces = createTextreResouces(device, metadata);

	// ----------------------------
	// Planeモデルのテクスチャ
	// ----------------------------
	DirectX::ScratchImage mipImages2 = LoadTexture(modelPlaneData.material.textureFilePath);
	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureRouces2 = createTextreResouces(device, metadata2);

	// ----------------------------
	// Teapotモデルのテクスチャ
	// ----------------------------
	DirectX::ScratchImage mipImages3 = LoadTexture(modelTeapotData.material.textureFilePath);
	const DirectX::TexMetadata& metadata3 = mipImages3.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureRouces3 = createTextreResouces(device, metadata3);

	// ----------------------------
	// Bunnyモデルのテクスチャ
	// ----------------------------
	DirectX::ScratchImage mipImages4 = LoadTexture(modelbunnyData.material.textureFilePath);
	const DirectX::TexMetadata& metadata4 = mipImages4.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureRouces4 = createTextreResouces(device, metadata4);

	// ----------------------------
	// MultiMeshモデルのテクスチャ
	// ----------------------------
	DirectX::ScratchImage mipImages5 = LoadTexture(modelMultiMeshData.material.textureFilePath);
	const DirectX::TexMetadata& metadata5 = mipImages5.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureRouces5 = createTextreResouces(device, metadata5);

	// ----------------------------
	// MultiMaterialモデルのテクスチャ
	// ----------------------------
	DirectX::ScratchImage mipImages6 = LoadTexture(modelMultiMaterialData.material.textureFilePath);
	const DirectX::TexMetadata& metadata6 = mipImages6.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureRouces6 = createTextreResouces(device, metadata6);

	// ----------------------------
	// Suzanneモデルのテクスチャ
	// ----------------------------
	DirectX::ScratchImage mipImages7 = LoadTexture(modelSuzanneData.material.textureFilePath);
	const DirectX::TexMetadata& metadata7 = mipImages7.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureRouces7 = createTextreResouces(device, metadata7);

	// ----------------------------
	// 各テクスチャのSRV設定
	// ----------------------------
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
	srvDesc2.Format = metadata2.format;
	srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc3{};
	srvDesc3.Format = metadata3.format;
	srvDesc3.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc3.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc3.Texture2D.MipLevels = UINT(metadata3.mipLevels);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc4{};
	srvDesc4.Format = metadata4.format;
	srvDesc4.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc4.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc4.Texture2D.MipLevels = UINT(metadata4.mipLevels);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc5{};
	srvDesc5.Format = metadata5.format;
	srvDesc5.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc5.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc5.Texture2D.MipLevels = UINT(metadata5.mipLevels);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc6{};
	srvDesc6.Format = metadata6.format;
	srvDesc6.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc6.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc6.Texture2D.MipLevels = UINT(metadata6.mipLevels);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc7{};
	srvDesc7.Format = metadata7.format;
	srvDesc7.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc7.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc7.Texture2D.MipLevels = UINT(metadata7.mipLevels);

	// ----------------------------
	// SRVハンドルの取得（SRVヒープのスロット1～7に配置）
	// ----------------------------
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGpudescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 1);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGpudescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 2);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU3 = GetCPUDescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 3);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU3 = GetGpudescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 3);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU4 = GetCPUDescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 4);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU4 = GetGpudescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 4);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU5 = GetCPUDescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 5);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU5 = GetGpudescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 5);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU6 = GetCPUDescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 6);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU6 = GetGpudescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 6);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU7 = GetCPUDescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 7);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU7 = GetGpudescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 7);

	// ----------------------------
	// SRVを作成
	// ----------------------------
	device->CreateShaderResourceView(textureResouces.Get(), &srvDesc, textureSrvHandleCPU);
	device->CreateShaderResourceView(textureRouces2.Get(), &srvDesc2, textureSrvHandleCPU2);
	device->CreateShaderResourceView(textureRouces3.Get(), &srvDesc3, textureSrvHandleCPU3);
	device->CreateShaderResourceView(textureRouces4.Get(), &srvDesc4, textureSrvHandleCPU4);
	device->CreateShaderResourceView(textureRouces5.Get(), &srvDesc5, textureSrvHandleCPU5);
	device->CreateShaderResourceView(textureRouces6.Get(), &srvDesc6, textureSrvHandleCPU6);
	device->CreateShaderResourceView(textureRouces7.Get(), &srvDesc7, textureSrvHandleCPU7);

	// ----------------------------
	// DepthStencil用テクスチャ作成
	// ----------------------------
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStenscilResouces =
		CreateDepthStencilTextResouces(device, kClientWidth, kClientHeight);

	// DSV設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device->CreateDepthStencilView(depthStenscilResouces.Get(), &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// ----------------------------
	// テクスチャデータをGPUにアップロード
	// ----------------------------
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResouces = UploadTextureDeta(textureResouces, mipImages, device, commandList);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResouces2 = UploadTextureDeta(textureRouces2, mipImages2, device, commandList);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResouces3 = UploadTextureDeta(textureRouces3, mipImages3, device, commandList);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResouces4 = UploadTextureDeta(textureRouces4, mipImages4, device, commandList);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResouces5 = UploadTextureDeta(textureRouces5, mipImages5, device, commandList);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResoucse6 = UploadTextureDeta(textureRouces6, mipImages6, device, commandList);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResoucse7 = UploadTextureDeta(textureRouces7, mipImages7, device, commandList);
	//初期化

		// ----------------------------
	// サウンド読み込み
	// ----------------------------
	SoundData soundData1 = audioPlayer->SoundLoadWave("resouces/Alarm02.wav");

	Transform cameraTransform = { {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f},{0.0f, 0.0f, -10.0f} };
	Transform transformSprite = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
	Transform uvTransformSprite = { {1.0f, 1.0f,1.0f},{0.0f,0.0f, 0.0f},{0.0f,0.0f,0.0f} };
	bool useMonsterBall = true;

	// ★追加する行
	bool audioPlayedOnce = false; // 音声が一度再生されたかどうかのフラグ
	bool isDebaugCamera = false;; // デバッグカメラの使用フラグ

	static int selectedLightingUI = 0;


	// --- 初期化 ---
	InputManager* inputManager = new InputManager();
	inputManager->Initialize(hwnd);
	DebugCamera* debugCamera = new DebugCamera();;
	debugCamera->Initialize();
	debugCamera->SetInputManager(inputManager);

	//ResoucesObject depthStencilResouces = CreateDepthStencilTextResouces(device, kClientWidth, kClientHeight);
	//windowの×ボタンが押されるまでループ
	while (msg.message != WM_QUIT)
	{




		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else
		{
			////画面の更新処理
			//keyboardDevice->Acquire();

			//BYTE key[256] = {};
			//keyboardDevice->GetDeviceState(sizeof(key), key);

			// 入力更新
			inputManager->Update();
			debugCamera->Update();

			// 音声再生を一度だけにする制御
			if (!audioPlayedOnce) {
				audioPlayer->SoundPlayWave(xAudio2.Get(), soundData1);
				audioPlayedOnce = true; // フラグを立てて、二度と再生しないようにする
			}
		
			bool temp_enableLighting = (materialData->enableLighting != 0);

			// ---------------- ImGui フレーム開始 ----------------
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			ImGui::Begin("Setting");

			// ▼ 描画切り替えメニュー
			static int selected = 0;
			const char* items[] = { "Plane & sprite", "sprite", "sphire", "sphire & obj", "Teapot", "bunny", "MultiMesh", "Suzanne" };
			ImGui::Combo("View Select", &selected, items, IM_ARRAYSIZE(items));

			// ▼ ライティング方式選択
			const char* enableLighting[] = { "None", "enableLighting", "enableLightingHaef" };
			ImGui::Combo("Lighting", &materialData->selectedLighting, enableLighting, IM_ARRAYSIZE(enableLighting));

			ImGui::Checkbox("use Debag Camera", &isDebaugCamera);
			ImGui::ColorEdit4("color", &materialData->color.x);
			// ▼ Obj 変換設定
			if (ImGui::CollapsingHeader("Obj Object")) {
				ImGui::DragFloat3("Translate##Obj", &transformObj.translate.x, 0.001f);
				ImGui::DragFloat3("Rotate##Obj", &transformObj.rotate.x, 0.001f);
				ImGui::DragFloat3("Scale##Obj", &transformObj.scale.x, 0.001f);

			}

			// ▼ ライティング設定
			if (ImGui::CollapsingHeader("Lighting Settings##obj")) {
				ImGui::DragFloat3("direction", &directLightData->direction.x, 0.001f);
				ImGui::DragFloat("intensity", &directLightData->intensity, 0.001f);
				ImGui::ColorEdit4("color", &directLightData->color.x);
			}

			// ▼ スプライトのマテリアル設定
			if (ImGui::CollapsingHeader("SpriteObject##Sprite")) {
				ImGui::DragFloat3("Translate##Sprite", &transformSprite.translate.x, 1.0f);
				ImGui::DragFloat3("Rotate##Sprite", &transformSprite.rotate.x, 0.001f);
				ImGui::DragFloat3("Scale##Sprite", &transformSprite.scale.x, 0.001f);

				if (ImGui::CollapsingHeader("Material##sprite")) {
					ImGui::DragFloat2("UVTranslate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
					ImGui::DragFloat2("UVScale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
					ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z, 0.001f);
					ImGui::ColorEdit4("color", &materialDataSprite->color.x);
				}

			}
			
			// ▼ 球体
			if (ImGui::CollapsingHeader("Sphere Object##sphire")) {
				ImGui::DragFloat3("SphireTranslate", &transformSphire.translate.x, 0.001f);
				ImGui::DragFloat3("SphireRotate", &transformSphire.rotate.x, 0.001f);
				ImGui::DragFloat3("SphireScale", &transformSphire.scale.x, 0.001f);
			}

			ImGui::End();


			
			// ---------------- 行列の更新 ----------------
			if (isDebaugCamera)
			{
				// Obj用デバックカメラ版
				Matrix4x4 worldMatrixObj = math->MakeAffineMatrix(transformObj.scale, transformObj.rotate, transformObj.translate);
				Matrix4x4 cameraMatrixObj = math->MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
				Matrix4x4 viewMatrixObj = debugCamera->GetViewMatrix();
				Matrix4x4 projectionMatrixObj = debugCamera->GetProjectionMatrix();
				Matrix4x4 worldViewProjectionMatrixObj = math->Multiply(worldMatrixObj, math->Multiply(viewMatrixObj, projectionMatrixObj));
				wvpObjData->WVP = worldViewProjectionMatrixObj;
				wvpObjData->world = worldMatrixObj;

				// Sphire用デバックカメラ
				Matrix4x4 worldMatrixSphire = math->MakeAffineMatrix(transformSphire.scale, transformSphire.rotate, transformSphire.translate);
				Matrix4x4 worldViewProjectionMatrixSphire = math->Multiply(worldMatrixSphire, math->Multiply(viewMatrixObj, projectionMatrixObj));
				wvpSphireData->WVP = worldViewProjectionMatrixSphire;
				wvpSphireData->world = worldMatrixSphire;
			}
			else
			{
				// Obj用通常版
				Matrix4x4 worldMatrixObj = math->MakeAffineMatrix(transformObj.scale, transformObj.rotate, transformObj.translate);
				Matrix4x4 cameraMatrixObj = math->MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
				Matrix4x4 viewMatrixObj = math->Inverse(cameraMatrixObj);
				Matrix4x4 projectionMatrixObj = math->MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
				Matrix4x4 worldViewProjectionMatrixObj = math->Multiply(worldMatrixObj, math->Multiply(viewMatrixObj, projectionMatrixObj));
				wvpObjData->WVP = worldViewProjectionMatrixObj;
				wvpObjData->world = worldMatrixObj;

				//shire用
				Matrix4x4 worldMatrixSphire = math->MakeAffineMatrix(transformSphire.scale, transformSphire.rotate, transformSphire.translate);
				Matrix4x4 cameraMatrixSphire = math->MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
				Matrix4x4 viewMatrixSphire = math->Inverse(cameraMatrixObj);
				Matrix4x4 projectionMatrixSphire = math->MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
				Matrix4x4 worldViewProjectionMatrixSphire = math->Multiply(worldMatrixSphire, math->Multiply(viewMatrixSphire, projectionMatrixSphire));
				wvpSphireData->WVP = worldViewProjectionMatrixSphire;
				wvpSphireData->world = worldMatrixSphire;

			}
		
			// Sprite用
			Matrix4x4 worldMatrixSprite = math->MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 viewMatrixSprite = math->makeIdentity4x4();
			Matrix4x4 projectionMatrixSprite = math->MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);
			Matrix4x4 worldViewProjectionMatrixSprite = math->Multiply(worldMatrixSprite, math->Multiply(viewMatrixSprite, projectionMatrixSprite));

			*transformationMatrixDataSprite = worldViewProjectionMatrixSprite;

			// UV変換
			Matrix4x4 uvTransformMatrix = math->MakeScaleMatrix(uvTransformSprite.scale);
			uvTransformMatrix = math->Multiply(uvTransformMatrix, math->MakeRotateZMatrix(uvTransformSprite.rotate.z));
			uvTransformMatrix = math->Multiply(uvTransformMatrix, math->MakeTranslateMatrix(uvTransformSprite.translate));
			materialDataSprite->uvTransform = uvTransformMatrix;


			//画面のクリア処理
			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();
			//TransitonBarrierの設定
			D3D12_RESOURCE_BARRIER barrier{};
			//今回のバリアはtransition
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			//Noneにしておく
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			//バリアを張る対象のリソース現在のバックバッファに対して行う
			barrier.Transition.pResource = swapChainResouces[backBufferIndex].Get();
			//転移前の(現在)のResoucesStare
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			//転移後のResourceState
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			//transSitiionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			//画面先のrtvを設定する
				//描画先のRTVとDSVを設定する
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);
			float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);

			//指定した度で深度で画面全体をクリアする
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			//描画用のDscriptorHeapの設定
			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeaps[] = { srvDescrriptorHeap.Get() };
			commandList->SetDescriptorHeaps(1, descriptorHeaps->GetAddressOf());

			//コマンドを積む
			commandList->RSSetViewports(1, &viewport);
			commandList->RSSetScissorRects(1, &scissorRect);

			//rootsignaltrueを設定　psoに設定しているけど別途設定が必要
			commandList->SetGraphicsRootSignature(rootsignatrue.Get());
			commandList->SetPipelineState(graphicsPipelineState.Get());
			

			//形状を設定psoに設定しているものとはまた別　同じものを設定するトロ考えておけば良い
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);



			//描画をimGui

			switch (selected)
			{
			case 0: // Plane & sprite
				// Plane

				commandList->SetGraphicsRootConstantBufferView(0, materialResouces->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(1, wvpObjResouces->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(3, DirectionalLightResouces->GetGPUVirtualAddress());
				commandList->IASetVertexBuffers(0, 1, &vertexPlaneBufferView);
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU2);
				commandList->DrawInstanced(static_cast<UINT>(modelPlaneData.vertices.size()), 1, 0, 0);

				// Sprite
				commandList->SetGraphicsRootConstantBufferView(0, materialResoucesSprite->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResoucesSprite->GetGPUVirtualAddress());
				commandList->IASetIndexBuffer(&indexBufferView);
				commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
				commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
				break;

			case 1:
				//sorite
				commandList->SetGraphicsRootConstantBufferView(0, materialResoucesSprite->GetGPUVirtualAddress());
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
				commandList->IASetIndexBuffer(&indexBufferView);
				commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
				commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResoucesSprite->GetGPUVirtualAddress());
				commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
				// 描画処理2
				break;
			case 2:
			
				//球
				//マテリアルcBubufferの場所設定
				commandList->SetGraphicsRootConstantBufferView(0, materialResouces->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(1, wvpSphireResouces->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(3, DirectionalLightResouces->GetGPUVirtualAddress());
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
				commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSphere); 
				commandList->IASetIndexBuffer(&indexBufferViewSphere);        
				//作画
				commandList->DrawIndexedInstanced(static_cast<UINT>(sphereIndices.size()), 1, 0, 0, 0);



				break;

			case 3:

				//obj 平面
				//マテリアルcBubufferの場所設定
				commandList->SetGraphicsRootConstantBufferView(0, materialResouces->GetGPUVirtualAddress());
				//wvpのcBufferの場所設定
				commandList->SetGraphicsRootConstantBufferView(1, wvpObjResouces->GetGPUVirtualAddress());
				//directionalLightのcBufferの場所設定
				commandList->SetGraphicsRootConstantBufferView(3, DirectionalLightResouces->GetGPUVirtualAddress());
				commandList->IASetVertexBuffers(0, 1, &vertexPlaneBufferView);
				//テクスチャのSRVの場所設定
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU2);
				commandList->DrawInstanced(static_cast<UINT>(modelPlaneData.vertices.size()), 1, 0, 0);


				//球
				//マテリアルcBubufferの場所設定
				commandList->SetGraphicsRootConstantBufferView(0, materialResouces->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(3, DirectionalLightResouces->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(1, wvpSphireResouces->GetGPUVirtualAddress());
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
				commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSphere);
				commandList->IASetIndexBuffer(&indexBufferViewSphere);
				//作画
				commandList->DrawIndexedInstanced(static_cast<UINT>(sphereIndices.size()), 1, 0, 0, 0);



				break;
			case 4:
				//teapot
					//マテリアルcBubufferの場所設定
					commandList->SetGraphicsRootConstantBufferView(0, materialResouces->GetGPUVirtualAddress());
					//wvpのcBufferの場所設定
					commandList->SetGraphicsRootConstantBufferView(1, wvpObjResouces->GetGPUVirtualAddress());
					//directionalLightのcBufferの場所設定
					commandList->SetGraphicsRootConstantBufferView(3, DirectionalLightResouces->GetGPUVirtualAddress());

					//テクスチャのSRVの場所設定
					commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU3);
					commandList->IASetVertexBuffers(0, 1, &vertexTeapotBufferView);

					commandList->DrawInstanced(static_cast<UINT>(modelTeapotData.vertices.size()), 1, 0, 0);
					break;
			case 5:
				//bunny
				//マテリアルcBubufferの場所設定
				commandList->SetGraphicsRootConstantBufferView(0, materialResouces->GetGPUVirtualAddress());
				//wvpのcBufferの場所設定
				commandList->SetGraphicsRootConstantBufferView(1, wvpObjResouces->GetGPUVirtualAddress());
				//directionalLightのcBufferの場所設定
				commandList->SetGraphicsRootConstantBufferView(3, DirectionalLightResouces->GetGPUVirtualAddress());

				//テクスチャのSRVの場所設定
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU4);
				commandList->IASetVertexBuffers(0, 1, &vertexbunnyBufferView);

				commandList->DrawInstanced(static_cast<UINT>(modelbunnyData.vertices.size()), 1, 0, 0);
				break;
			case 6:
				//MultiMesh
				//マテリアルcBubufferの場所設定
				commandList->SetGraphicsRootConstantBufferView(0, materialResouces->GetGPUVirtualAddress());
				//wvpのcBufferの場所設定
				commandList->SetGraphicsRootConstantBufferView(1, wvpObjResouces->GetGPUVirtualAddress());
				//directionalLightのcBufferの場所設定
				commandList->SetGraphicsRootConstantBufferView(3, DirectionalLightResouces->GetGPUVirtualAddress());

				//テクスチャのSRVの場所設定
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU5);
				commandList->IASetVertexBuffers(0, 1, &vertexMultiMeshBufferView);

				commandList->DrawInstanced(static_cast<UINT>(modelMultiMeshData.vertices.size()), 1, 0, 0);
				break;
		
			case 7: 
				//Suzanne
				//マテリアルcBubufferの場所設定
				commandList->SetGraphicsRootConstantBufferView(0, materialResouces->GetGPUVirtualAddress());
				//wvpのcBufferの場所設定
				commandList->SetGraphicsRootConstantBufferView(1, wvpObjResouces->GetGPUVirtualAddress());
				//directionalLightのcBufferの場所設定
				commandList->SetGraphicsRootConstantBufferView(3, DirectionalLightResouces->GetGPUVirtualAddress());

				//テクスチャのSRVの場所設定
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU7);
				commandList->IASetVertexBuffers(0, 1, &vertexSuzanneBufferView);

				commandList->DrawInstanced(static_cast<UINT>(modelSuzanneData.vertices.size()), 1, 0, 0);
				break;
			}

			ImGui::Render();

			//実際のcommmandList残り時間imguiの描画コマンドを積む
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
			//renderTarGetからPresentにする
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			//transSitiionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);
			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			Microsoft::WRL::ComPtr<ID3D12CommandList> commandLists[] = { commandList.Get() };
			commandQueue->ExecuteCommandLists(1, commandLists->GetAddressOf());
			swapChain->Present(1, 0);


			//Fenceの更新
			fenceValue++;
			//GPUがそこまでたどり着いた時に Fenceの値を指定した値に代入するようにsignalを送る
			commandQueue->Signal(fence.Get(), fenceValue);

			if (fence->GetCompletedValue() < fenceValue)
			{
				//指定したsignal似たとりついていないのでたどり着くまでに待つようにイベントを指定する
				fence->SetEventOnCompletion(fenceValue, fenceEvent);
				//イベントを待つ
				WaitForSingleObject(fenceEvent, INFINITE);
			}


			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));
			hr = commandList->Reset(commandAllocator.Get(), nullptr);
			assert(SUCCEEDED(hr));
		}


	}

	CloseWindow(hwnd);
	//サウンドのRelese
	audioPlayer-> SoundUnload(&soundData1);
	xAudio2.Reset();

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	//comの終了時
	CoUninitialize();

	return 0;
}