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
#include <wrl.h>
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
#include<xaudio2.h>
#include <dinput.h>
#include "AudioPlayer.h"
#include "Math.h"
#include "debugCamera.h"
#include "InputManager.h"
#include "WinApp.h"
#include"DirectXCommon.h"
#include "D3DResouceLeakChecKer.h"
#include "Sprite.h"




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





[[nodiscard]]
Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureDeta(
	const Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
	const DirectX::ScratchImage& mipImages,
	const Microsoft::WRL::ComPtr<ID3D12Device>& device,
	const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList,
	DirectXCommon* dxCommon)
{
	std::vector<D3D12_SUBRESOURCE_DATA> subresouces;
	DirectX::PrepareUpload(device.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresouces);
	uint64_t intermeddiatesize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresouces.size()));

	// 引数で受け取ったdxCommonを使い、正しい引数(1つだけ)で呼び出す
	Microsoft::WRL::ComPtr<ID3D12Resource> intermeddiaetResouces = dxCommon->CreateBufferResource(intermeddiatesize);

	
	// もしここでプログラムが止まったら、CreateBufferResourceが失敗しています
	assert(intermeddiaetResouces != nullptr);

	UpdateSubresources(commandList.Get(), texture.Get(), intermeddiaetResouces.Get(), 0, 0, UINT(subresouces.size()), subresouces.data());

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

	D3DResourceLeakChecKer leakChecker;



	AudioPlayer* audioPlayer = new AudioPlayer();
	Math* math = new Math();
;




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
	WinApp winApp;
	winApp.Initialize(L"CG2", 1280, 720);

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	dxCommon->Initialize(&winApp);

	ID3D12Device* device = dxCommon->GetDevice();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

	// テクスチャをロードし、ハンドル(番号)を取得
	uint32_t uvCheckerTexHandle = 1; // 元のコードでuvChecker.pngがSRVの1番だったので

	// ★★★ 複数のスプライトを生成・初期化 ★★★
	std::vector<Sprite*> sprites;
	for (int i = 0; i < 5; ++i) {
		Sprite* sprite = new Sprite();
		sprite->Initialize(dxCommon, uvCheckerTexHandle);
		// 位置やサイズを少しずつずらす
		sprite->SetPosition({ 100.0f + i * 120.0f, 100.0f });
		sprite->SetSize({ 100.0f, 100.0f });
		sprites.push_back(sprite);
	}


#
	HRESULT hr = S_OK;
	//DEscriptSizeを取得しておく
	ID3D12DescriptorHeap* srvDescriptorHeap = dxCommon->GetSrvDescriptorHeap();
	const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// (DSV関連も同様に取得しておきます)
	const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);



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

	//rasiterzerstateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};

	//裏面時計回りに表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;

	//三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	//shaderをコンパイルする
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = dxCommon->CompileShader(L"Object3d.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);
	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = dxCommon->CompileShader(L"Object3d.PS.hlsl", L"ps_6_0");
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
			vertex.normal = math->Normalize({ vertex.position.x, vertex.position.y, vertex.position.z });
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
		dxCommon->CreateBufferResource(sizeof(VertexData) * modelPlaneData.vertices.size());

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
		dxCommon->CreateBufferResource(sizeof(VertexData) * modelTeapotData.vertices.size());

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
		dxCommon->CreateBufferResource( sizeof(VertexData) * modelbunnyData.vertices.size());

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
		dxCommon->CreateBufferResource( sizeof(VertexData) * modelMultiMeshData.vertices.size());

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
		dxCommon->CreateBufferResource( sizeof(VertexData) * modelMultiMaterialData.vertices.size());

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
		dxCommon->CreateBufferResource( sizeof(VertexData) * modelSuzanneData.vertices.size());

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
		dxCommon->CreateBufferResource(sizeof(VertexData) * sphereVertices.size());

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
		dxCommon->CreateBufferResource(sizeof(uint32_t) * sphereIndices.size());

	uint32_t* mappedIndexDataSphere = nullptr;
	indexResourceSphere->Map(0, nullptr, reinterpret_cast<void**>(&mappedIndexDataSphere));
	std::copy(sphereIndices.begin(), sphereIndices.end(), mappedIndexDataSphere);
	indexResourceSphere->Unmap(0, nullptr);

	D3D12_INDEX_BUFFER_VIEW indexBufferViewSphere{};
	indexBufferViewSphere.BufferLocation = indexResourceSphere->GetGPUVirtualAddress();
	indexBufferViewSphere.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * sphereIndices.size());
	indexBufferViewSphere.Format = DXGI_FORMAT_R32_UINT;



	// =============================
	// ▼ Obj用 WVP リソースの作成
	// =============================
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpObjResouces = dxCommon->CreateBufferResource( sizeof(TransformationMatrix));
	TransformationMatrix* wvpObjData = nullptr;
	wvpObjResouces->Map(0, nullptr, reinterpret_cast<void**>(&wvpObjData));
	wvpObjData->WVP = math->makeIdentity4x4();
	wvpObjData->world = math->MakeAffineMatrix(transformObj.scale, transformObj.rotate, transformObj.translate);

	// =============================
	// ▼ 球体用 WVP リソースの作成
	// =============================
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpSphireResouces = dxCommon->CreateBufferResource( sizeof(TransformationMatrix));
	TransformationMatrix* wvpSphireData = nullptr;
	wvpSphireResouces->Map(0, nullptr, reinterpret_cast<void**>(&wvpSphireData));
	wvpSphireData->WVP = math->makeIdentity4x4();
	wvpSphireData->world = math->MakeAffineMatrix(transformSphire.scale, transformSphire.rotate, transformSphire.translate);



	// =============================
	// ▼ ディレクショナルライト リソース
	// =============================
	Microsoft::WRL::ComPtr<ID3D12Resource> DirectionalLightResouces = dxCommon->CreateBufferResource( sizeof(DirectionalLight));
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
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResouces = dxCommon->CreateBufferResource( sizeof(Material));
	Material* materialData = nullptr;
	materialResouces->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData->enableLighting = true;
	materialData->selectedLighting = 0;
	materialData->uvTransform = math->makeIdentity4x4();
	materialResouces->Unmap(0, nullptr);

	//----------------------------
	// テクスチャ1: uvChecker.png 読み込み & リソース作成
	// ----------------------------
	DirectX::ScratchImage mipImages = DirectXCommon::LoadTexture("resouces/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResouces = dxCommon->CreateTextureResource(metadata);

	// ----------------------------
	// Planeモデルのテクスチャ
	// ----------------------------
	DirectX::ScratchImage mipImages2 = DirectXCommon::LoadTexture(modelPlaneData.material.textureFilePath);
	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureRouces2 = dxCommon->CreateTextureResource(metadata2);

	// ----------------------------
	// Teapotモデルのテクスチャ
	// ----------------------------
	DirectX::ScratchImage mipImages3 = DirectXCommon::LoadTexture(modelTeapotData.material.textureFilePath);
	const DirectX::TexMetadata& metadata3 = mipImages3.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureRouces3 = dxCommon->CreateTextureResource(metadata3);

	// ----------------------------
	// Bunnyモデルのテクスチャ
	// ----------------------------
	DirectX::ScratchImage mipImages4 = DirectXCommon::LoadTexture(modelbunnyData.material.textureFilePath);
	const DirectX::TexMetadata& metadata4 = mipImages4.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureRouces4 = dxCommon->CreateTextureResource(metadata4);

	// ----------------------------
	// MultiMeshモデルのテクスチャ
	// ----------------------------
	DirectX::ScratchImage mipImages5 = DirectXCommon::LoadTexture(modelMultiMeshData.material.textureFilePath);
	const DirectX::TexMetadata& metadata5 = mipImages5.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureRouces5 = dxCommon->CreateTextureResource(metadata5);

	// ----------------------------
	// MultiMaterialモデルのテクスチャ
	// ----------------------------
	DirectX::ScratchImage mipImages6 = DirectXCommon::LoadTexture(modelMultiMaterialData.material.textureFilePath);
	const DirectX::TexMetadata& metadata6 = mipImages6.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureRouces6 = dxCommon->CreateTextureResource(metadata6);

	// ----------------------------
	// Suzanneモデルのテクスチャ
	// ----------------------------
	DirectX::ScratchImage mipImages7 = DirectXCommon::LoadTexture(modelSuzanneData.material.textureFilePath);
	const DirectX::TexMetadata& metadata7 = mipImages7.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureRouces7 = dxCommon->CreateTextureResource(metadata7);;

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
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGpudescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 1);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGpudescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 2);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU3 = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 3);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU3 = GetGpudescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 3);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU4 = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 4);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU4 = GetGpudescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 4);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU5 = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 5);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU5 = GetGpudescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 5);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU6 = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 6);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU6 = GetGpudescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 6);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU7 = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 7);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU7 = GetGpudescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 7);

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
	// テクスチャデータをGPUにアップロード
	// ----------------------------
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResouces = UploadTextureDeta(textureResouces, mipImages, device, commandList,dxCommon);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResouces2 = UploadTextureDeta(textureRouces2, mipImages2, device, commandList,dxCommon);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResouces3 = UploadTextureDeta(textureRouces3, mipImages3, device, commandList,dxCommon);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResouces4 = UploadTextureDeta(textureRouces4, mipImages4, device, commandList,dxCommon);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResouces5 = UploadTextureDeta(textureRouces5, mipImages5, device, commandList,dxCommon);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResoucse6 = UploadTextureDeta(textureRouces6, mipImages6, device, commandList,dxCommon);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResoucse7 = UploadTextureDeta(textureRouces7, mipImages7, device, commandList,dxCommon);
	//初期化

		// ----------------------------
	// サウンド読み込み
	// ----------------------------
	SoundData soundData1 = audioPlayer->SoundLoadWave("resouces/Alarm02.wav");

	Transform cameraTransform = { {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f},{0.0f, 0.0f, -10.0f} };

	bool useMonsterBall = true;

	// ★追加する行
	bool audioPlayedOnce = false; // 音声が一度再生されたかどうかのフラグ
	bool isDebaugCamera = true; // デバッグカメラの使用フラグ

	static int selectedLightingUI = 0;


	// --- 初期化 ---
	InputManager* inputManager = new InputManager();
	inputManager->Initialize(winApp.GetHwnd());
	DebugCamera* debugCamera = new DebugCamera();;
	debugCamera->Initialize();
	debugCamera->SetInputManager(inputManager);



	//ResoucesObject depthStencilResouces = CreateDepthStencilTextResouces(device, kClientWidth, kClientHeight);
	//windowの×ボタンが押されるまでループ
	while (winApp.Update()==false)
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

			// ★★★ ImGuiで先頭のスプライトを操作 ★★★
			if (!sprites.empty()) {
				Sprite* firstSprite = sprites[0];
				if (ImGui::CollapsingHeader("Sprite Object")) {
					Vector2 pos = firstSprite->GetPosition();
					if (ImGui::DragFloat2("Position", &pos.x, 1.0f)) {
						firstSprite->SetPosition(pos);
					}
					float rot = firstSprite->GetRotation();
					if (ImGui::SliderAngle("Rotation", &rot)) {
						firstSprite->SetRotation(rot);
					}
					Vector2 size = firstSprite->GetSize();
					if (ImGui::DragFloat2("Size", &size.x, 1.0f)) {
						firstSprite->SetSize(size);
					}
					Vector4 color = firstSprite->GetColor();
					if (ImGui::ColorEdit4("Color", &color.x)) {
						firstSprite->SetColor(color);
					}
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
			} else
			{
				// Obj用通常版
				Matrix4x4 worldMatrixObj = math->MakeAffineMatrix(transformObj.scale, transformObj.rotate, transformObj.translate);
				Matrix4x4 cameraMatrixObj = math->MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
				Matrix4x4 viewMatrixObj = math->Inverse(cameraMatrixObj);
				Matrix4x4 projectionMatrixObj = math->MakePerspectiveFovMatrix(0.45f, float(WinApp::kClientWidth) / float(WinApp::kClientHeight), 0.1f, 100.0f);
				Matrix4x4 worldViewProjectionMatrixObj = math->Multiply(worldMatrixObj, math->Multiply(viewMatrixObj, projectionMatrixObj));
				wvpObjData->WVP = worldViewProjectionMatrixObj;
				wvpObjData->world = worldMatrixObj;

				//shire用
				Matrix4x4 worldMatrixSphire = math->MakeAffineMatrix(transformSphire.scale, transformSphire.rotate, transformSphire.translate);
				Matrix4x4 cameraMatrixSphire = math->MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
				Matrix4x4 viewMatrixSphire = math->Inverse(cameraMatrixObj);
				Matrix4x4 projectionMatrixSphire = math->MakePerspectiveFovMatrix(0.45f, float(WinApp::kClientWidth) / float(WinApp::kClientHeight), 0.1f, 100.0f);
				Matrix4x4 worldViewProjectionMatrixSphire = math->Multiply(worldMatrixSphire, math->Multiply(viewMatrixSphire, projectionMatrixSphire));
				wvpSphireData->WVP = worldViewProjectionMatrixSphire;
				wvpSphireData->world = worldMatrixSphire;

			}

			for (Sprite* sprite : sprites) {
				sprite->Update();
			}


			dxCommon->PreDraw();


			commandList->SetGraphicsRootSignature(rootsignatrue.Get());
			commandList->SetPipelineState(graphicsPipelineState.Get());
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			//描画をimGui

			switch (selected)
			{
			case 0: { // Plane & sprite
				// Plane

				commandList->SetGraphicsRootConstantBufferView(0, materialResouces->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(1, wvpObjResouces->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(3, DirectionalLightResouces->GetGPUVirtualAddress());
				commandList->IASetVertexBuffers(0, 1, &vertexPlaneBufferView);
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU2);
				commandList->DrawInstanced(static_cast<UINT>(modelPlaneData.vertices.size()), 1, 0, 0);

				for (Sprite* sprite : sprites) {
					sprite->SetColor({ 1,1,1,1 });
					sprite->Draw(commandList);

				};
			}
				break;

			case 1:
				//sorite
				for (Sprite* sprite : sprites) {
					sprite->Draw(commandList);
				}
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

			dxCommon->PostDraw();
		}


	

	CloseWindow(winApp.GetHwnd());
	//サウンドのRelese
	audioPlayer->SoundUnload(&soundData1);
	xAudio2.Reset();

	dxCommon->Finalize();


	//comの終了時
	CoUninitialize();

	return 0;
}