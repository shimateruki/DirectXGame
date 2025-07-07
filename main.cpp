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

//BOOL MiniDumpWriteDump(
//	[in] HANDLE                            hProcess,
//	[in] DWORD                             ProcessId,
//	[in] HANDLE                            hFile,
//	[in] MINIDUMP_TYPE                     DumpType,
//	[in] PMINIDUMP_EXCEPTION_INFORMATION   ExceptionParam,
//	[in] PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
//	[in] PMINIDUMP_CALLBACK_INFORMATION    CallbackParam
//);

struct Vector2
{
	float x;
	float y;
};

struct Vector3
{
	float x;
	float y;
	float z;
};


struct Vector4
{
	float x;
	float y;
	float z;
	float w;
};

struct Matrix3x3
{
	float m[3][3];
};


struct Matrix4x4
{
	float m[4][4];
};

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
	float padding[3];
	Matrix4x4 uvTransform;

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


struct ChunkHeader
{
	char id[4];
	int32_t size;
};

struct RiffHeader
{
	ChunkHeader chunk;
	char type[4];
};

struct FormatChunk
{
	ChunkHeader chunk;
	WAVEFORMATEX format;
};

struct SoundData
{
	WAVEFORMATEX wfex;
	BYTE* pBuffer;
	unsigned int bufferSize;
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

// 単位行列の作成
Matrix4x4 makeIdentity4x4()
{
	Matrix4x4 result = {};
	result.m[0][0] = 1.0f;
	result.m[0][1] = 0.0f;
	result.m[0][2] = 0.0f;
	result.m[0][3] = 0.0f;

	result.m[1][0] = 0.0f;
	result.m[1][1] = 1.0f;
	result.m[1][2] = 0.0f;
	result.m[1][3] = 0.0f;

	result.m[2][0] = 0.0f;
	result.m[2][1] = 0.0f;
	result.m[2][2] = 1.0f;
	result.m[2][3] = 0.0f;

	result.m[3][0] = 0.0f;
	result.m[3][1] = 0.0f;
	result.m[3][2] = 0.0f;
	result.m[3][3] = 1.0f;
	return result;
};

Matrix4x4 MakeScaleMatrix(const Vector3& scale) {

	Matrix4x4 result{ scale.x, 0.0f, 0.0f, 0.0f, 0.0f, scale.y, 0.0f, 0.0f, 0.0f, 0.0f, scale.z, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

	return result;
}

Matrix4x4 MakeRotateXMatrix(float theta) {
	float sin = std::sin(theta);
	float cos = std::cos(theta);

	Matrix4x4 result{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, cos, sin, 0.0f, 0.0f, -sin, cos, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

	return result;
}

Matrix4x4 MakeRotateYMatrix(float theta) {
	float sin = std::sin(theta);
	float cos = std::cos(theta);

	Matrix4x4 result{ cos, 0.0f, -sin, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, sin, 0.0f, cos, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

	return result;
}

Matrix4x4 MakeRotateZMatrix(float theta) {
	float sin = std::sin(theta);
	float cos = std::cos(theta);

	Matrix4x4 result{ cos, sin, 0.0f, 0.0f, -sin, cos, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

	return result;
}

Matrix4x4 MakeTranslateMatrix(const Vector3& translate) {
	Matrix4x4 result{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, translate.x, translate.y, translate.z, 1.0f };

	return result;
}



Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result = {};
	for (int row = 0; row < 4; ++row) {
		for (int col = 0; col < 4; ++col) {
			result.m[row][col] =
				m1.m[row][0] * m2.m[0][col] +
				m1.m[row][1] * m2.m[1][col] +
				m1.m[row][2] * m2.m[2][col] +
				m1.m[row][3] * m2.m[3][col];
		}
	}
	return result;
}

//逆行列
Matrix4x4 Inverse(const Matrix4x4& m)
{
	Matrix4x4 result = {};


	float a11 = m.m[0][0], a12 = m.m[0][1], a13 = m.m[0][2], a14 = m.m[0][3];
	float a21 = m.m[1][0], a22 = m.m[1][1], a23 = m.m[1][2], a24 = m.m[1][3];
	float a31 = m.m[2][0], a32 = m.m[2][1], a33 = m.m[2][2], a34 = m.m[2][3];
	float a41 = m.m[3][0], a42 = m.m[3][1], a43 = m.m[3][2], a44 = m.m[3][3];


	float det = a11 * a22 * a33 * a44 + a11 * a23 * a34 * a42 + a11 * a24 * a32 * a43
		- a11 * a24 * a33 * a42 - a11 * a23 * a32 * a44 - a11 * a22 * a34 * a43
		- a12 * a21 * a33 * a44 - a13 * a21 * a34 * a42 - a14 * a21 * a32 * a43
		+ a14 * a21 * a33 * a42 + a13 * a21 * a32 * a44 + a12 * a21 * a34 * a43
		+ a12 * a23 * a31 * a44 + a13 * a24 * a31 * a42 + a14 * a22 * a31 * a43
		- a14 * a23 * a31 * a42 - a13 * a22 * a31 * a44 - a12 * a24 * a31 * a43
		- a12 * a23 * a34 * a41 - a13 * a24 * a32 * a41 - a14 * a22 * a33 * a41
		+ a14 * a23 * a32 * a41 + a13 * a22 * a34 * a41 + a12 * a24 * a33 * a41;
	if (det == 0.0f) {
		// 逆行列が存在しない（行列式が0）
		return result;
	}

	float invDet = 1.0f / det;

	// 以下、各要素に対応する余因子を手動計算して代入（転置あり）

	// 1行目
	result.m[0][0] = (a22 * (a33 * a44 - a34 * a43) - a23 * (a32 * a44 - a34 * a42) + a24 * (a32 * a43 - a33 * a42)) * invDet;
	result.m[0][1] = -(a12 * (a33 * a44 - a34 * a43) - a13 * (a32 * a44 - a34 * a42) + a14 * (a32 * a43 - a33 * a42)) * invDet;
	result.m[0][2] = (a12 * (a23 * a44 - a24 * a43) - a13 * (a22 * a44 - a24 * a42) + a14 * (a22 * a43 - a23 * a42)) * invDet;
	result.m[0][3] = -(a12 * (a23 * a34 - a24 * a33) - a13 * (a22 * a34 - a24 * a32) + a14 * (a22 * a33 - a23 * a32)) * invDet;

	// 2行目
	result.m[1][0] = -(a21 * (a33 * a44 - a34 * a43) - a23 * (a31 * a44 - a34 * a41) + a24 * (a31 * a43 - a33 * a41)) * invDet;
	result.m[1][1] = (a11 * (a33 * a44 - a34 * a43) - a13 * (a31 * a44 - a34 * a41) + a14 * (a31 * a43 - a33 * a41)) * invDet;
	result.m[1][2] = -(a11 * (a23 * a44 - a24 * a43) - a13 * (a21 * a44 - a24 * a41) + a14 * (a21 * a43 - a23 * a41)) * invDet;
	result.m[1][3] = (a11 * (a23 * a34 - a24 * a33) - a13 * (a21 * a34 - a24 * a31) + a14 * (a21 * a33 - a23 * a31)) * invDet;

	// 3行目
	result.m[2][0] = (a21 * (a32 * a44 - a34 * a42) - a22 * (a31 * a44 - a34 * a41) + a24 * (a31 * a42 - a32 * a41)) * invDet;
	result.m[2][1] = -(a11 * (a32 * a44 - a34 * a42) - a12 * (a31 * a44 - a34 * a41) + a14 * (a31 * a42 - a32 * a41)) * invDet;
	result.m[2][2] = (a11 * (a22 * a44 - a24 * a42) - a12 * (a21 * a44 - a24 * a41) + a14 * (a21 * a42 - a22 * a41)) * invDet;
	result.m[2][3] = -(a11 * (a22 * a34 - a24 * a32) - a12 * (a21 * a34 - a24 * a31) + a14 * (a21 * a32 - a22 * a31)) * invDet;

	// 4行目
	result.m[3][0] = -(a21 * (a32 * a43 - a33 * a42) - a22 * (a31 * a43 - a33 * a41) + a23 * (a31 * a42 - a32 * a41)) * invDet;
	result.m[3][1] = (a11 * (a32 * a43 - a33 * a42) - a12 * (a31 * a43 - a33 * a41) + a13 * (a31 * a42 - a32 * a41)) * invDet;
	result.m[3][2] = -(a11 * (a22 * a43 - a23 * a42) - a12 * (a21 * a43 - a23 * a41) + a13 * (a21 * a42 - a22 * a41)) * invDet;
	result.m[3][3] = (a11 * (a22 * a33 - a23 * a32) - a12 * (a21 * a33 - a23 * a31) + a13 * (a21 * a32 - a22 * a31)) * invDet;

	return result;
}

//透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip)
{
	Matrix4x4 result = {};
	float f = 1.0f / tanf(fovY / 2.0f);
	result.m[0][0] = f / aspectRatio;
	result.m[0][1] = 0.0f;
	result.m[0][2] = 0.0f;
	result.m[0][3] = 0.0f;

	result.m[1][0] = 0.0f;
	result.m[1][1] = f;
	result.m[1][2] = 0.0f;
	result.m[1][3] = 0.0f;

	result.m[2][0] = 0.0f;
	result.m[2][1] = 0.0f;
	result.m[2][2] = farClip / (farClip - nearClip);
	result.m[2][3] = 1.0f;

	result.m[3][0] = 0.0f;
	result.m[3][1] = 0.0f;
	result.m[3][2] = -nearClip * farClip / (farClip - nearClip);
	result.m[3][3] = 0.0f;
	return result;
}



//正射影行列
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right,
	float bottom, float nearClip, float farClip)
{
	Matrix4x4 result{};
	result.m[0][0] = 2.0f / (right - left);
	result.m[1][1] = 2.0f / (top - bottom);
	result.m[2][2] = 1.0f / (farClip - nearClip);
	result.m[3][0] = (left + right) / (left - right);
	result.m[3][1] = (top + bottom) / (bottom - top);
	result.m[3][2] = nearClip / (nearClip - farClip);
	result.m[3][3] = 1.0f;
	return result;
}

Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);
	Matrix4x4 rotateMatrix = Multiply(Multiply(rotateXMatrix, rotateYMatrix), rotateZMatrix);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

	Matrix4x4 worldMatrix = Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);

	return worldMatrix;
}






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

void ShowSRTWindow(Transform& transform)
{
#ifdef _DEBUG
	ImGui::Begin("SRT Settings (Debug Only)");

	ImGui::SliderFloat3("Scale", &transform.scale.x, 0.1f, 10.0f);
	ImGui::SliderFloat3("Rotate", &transform.rotate.x, -3.14159f, 3.14159f);
	ImGui::SliderFloat3("Translate", &transform.translate.x, -100.0f, 100.0f);

	ImGui::End();
#endif
}

// Vector3 の正規化関数
Vector3 Normalize(const Vector3& v) {
	float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);

	// ゼロ除算を避けるために、長さが非常に小さい場合はゼロベクトルを返す
	// FLT_EPSILON は <cfloat> または <limits> に定義されています
	// もしくは、独自に小さな定数 (例: 1e-6f) を定義しても良い
	if (length < 0.000001f) { // 適切な epsilon 値を設定してください
		return { 0.0f, 0.0f, 0.0f };
	}

	return { v.x / length, v.y / length, v.z / length };
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


IDxcBlob* CompileShander(
	const std::wstring& filePath,
	const wchar_t* profile,
	IDxcUtils* dxcUtils,
	IDxcCompiler3* dxCompiler,
	IDxcIncludeHandler* includeHandler,
	std::ostream& os)
{
	//１hlslファイルを読み込む
	Log(os, ConvertString(std::format(L"Begin CompileShader,path:{}, profile:{}\n", filePath, profile)));
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
	if (shanderError != nullptr && shanderError->GetStringLength() != 0)
	{
		Log(os, shanderError->GetStringPointer());
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

DirectX::ScratchImage LoadTexture(const std::string& filePath)
{
	//テクスチャファイルを読み込んでプログラムで扱えるようにする
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));
	//ミニマップの作成
	DirectX::ScratchImage mipImages{};
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
{//中で必要となる変数宣言
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
	//必要な変数の宣言
	ModelData modelData;//構築するmodelData
	std::vector<Vector4> positions;//位置
	std::vector<Vector3> normals;//法線
	std::vector<Vector2> texcoords;//テクスチャ座標
	std::string line;//ファイルから読んだ一行を格納するもの
	//ファイルを開く
	std::ifstream file(directoryPath + "/" + filename);//ファイルを開く
	assert(file.is_open());
	//ファイルを読み込み　 modelDataを構築
	while (std::getline(file, line)) {
		std::string identfier;
		std::istringstream s(line);
		s >> identfier;
		//頂点情報を読む
		if (identfier == "v")
		{
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.w = 1.0f;
			positions.push_back(position);
		} else if (identfier == "vt")
		{
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;
			texcoord.y = 1.0f - texcoord.y;
			texcoords.push_back(texcoord);
		} else if (identfier == "vn")
		{
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);
		} else if (identfier == "f")
		{
			VertexData triangle[3];
			//面は三角形限定　その他は未対応
			for (int32_t faceVertex = 0; faceVertex < 3; faceVertex++)
			{
				std::string vertexDefinition;
				s >> vertexDefinition;
				//頂点の要素へのindexは {位置/UV/法線} 出格納されているので　分割してindexを取得する
				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3];

				for (int32_t element = 0; element < 3; ++element)
				{
					std::string index;
					std::getline(v, index, '/');
					elementIndices[element] = std::stoi(index);
				}
				//要素へindexから、実際の要素の値を取得して頂点を構築する
				Vector4 position = positions[elementIndices[0] - 1];
				Vector2 texcoode = texcoords[elementIndices[1] - 1];
				Vector3 normal = normals[elementIndices[2] - 1];
				/*	VertexData vertex = { position, texcoode, normal };
					modelData.vertices.push_back(vertex);*/
				triangle[faceVertex] = { position, texcoode, normal };
			}
			//頂点を逆順することで周り順を逆にする
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);
		} else if (identfier == "mtllib")
		{
			std::string materialFilename;
			s >> materialFilename;
			modelData.material = LoadMaterialTemplatFile(directoryPath, materialFilename);
		}
	}
	return modelData;
}

SoundData SoundLoadWave(const char* filename)
{

	//ファイルオープン
	std::ifstream file;
	file.open(filename, std::ios::binary);
	assert(file.is_open());
	//wavデータ読み込み
	RiffHeader riff;
	file.read((char*)&riff, sizeof(riff));
	if (strncmp(riff.chunk.id, "RIFF", 4) != 0)
	{
		assert(0);
	}
	if (strncmp(riff.type, "WAVE", 4) != 0)
	{
		assert(0);
	}
	//fmtチャンク読み込み
	FormatChunk format = {};
	file.read((char*)&format, sizeof(ChunkHeader));
	if (strncmp(format.chunk.id, "fmt ", 4) != 0)
	{
		assert(0);
	}
	//チャンク本体の読み込み
	assert(format.chunk.size <= sizeof(format.format));
	file.read((char*)&format.format, format.chunk.size);
	//dataチャンクの読み込み
	ChunkHeader data = {};
	file.read((char*)&data, sizeof(data));
	if (strncmp(data.id, "JUNK", 4) == 0)
	{

		file.seekg(data.size, std::ios_base::cur);
		file.read((char*)&data, sizeof(data));
	}
	if (strncmp(data.id, "data", 4) != 0)
	{
		assert(0);
	}
	char* pBuffer = new char[data.size];
	file.read(pBuffer, data.size);

	file.close();
	SoundData soundData = {};
	soundData.wfex = format.format;
	soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
	soundData.bufferSize = data.size;
	return soundData;
}

//専用データ解放
void SoundUnload(SoundData* soundData)
{

	delete[] soundData->pBuffer;
	soundData->pBuffer = 0;

	soundData->bufferSize = 0;
	soundData->wfex = {};


}

void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData) {

	HRESULT result;
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	result = xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);

	XAUDIO2_BUFFER buffer = {};
	buffer.pAudioData = soundData.pBuffer; // 音声データのポインタ
	buffer.AudioBytes = soundData.bufferSize; // 音声データのサイズ
	buffer.Flags = XAUDIO2_END_OF_STREAM; // ストリームの終端を示すフラグ

	result = pSourceVoice->SubmitSourceBuffer(&buffer);
	result = pSourceVoice->Start(); // 音声の再生を開始

}

//初期化
Transform transform = { {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f},{0.0f, 0.0f, 0.0f} };



int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
#ifdef _DEBUG
	D3DResourceLeakChecker leakChecker;
#endif

	// DXの初期化とComPtrによるリソース生成
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory;
	Microsoft::WRL::ComPtr<ID3D12Device> device;


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

		//解放
		infoqueue->Release();
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


	//マテリアル用のリソースを作る　今回はcolor一つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResouces = createBufferResouces(device, sizeof(Material));

	//マテリアル用のデータを書き込む
	Material* materrialData = nullptr;

	//書き込むためのアドレスを取得
	materialResouces->Map(0, nullptr, reinterpret_cast<void**>(&materrialData));

	//今回は赤を書き込んでみる
	materrialData->color = Vector4(1.0f, 0.0f, 0.0f, 1.0f);
	materrialData->uvTransform = makeIdentity4x4();





	//シアライズしてばいなりにする
	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
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
	IDxcBlob* vertexShaderBlob = CompileShander(L"Object3d.VS.hlsl",
		L"vs_6_0", dxcUtils, dxcCompiler, includeHander, logStrem);
	assert(vertexShaderBlob != nullptr);
	IDxcBlob* pixelShaderBlob = CompileShander(L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHander, logStrem);
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
			vertex.normal = Normalize({ vertex.position.x, vertex.position.y, vertex.position.z });
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
	//モデル読み込み
	ModelData modelData = LoadObjFile("resouces", "plane.obj");

	// ★球体用の頂点リソースの作成とデータ転送 (vertexResourceSphereを使用)
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = createBufferResouces(device, sizeof(VertexData) * modelData.vertices.size());


	// ★球体用の頂点バッファビューの設定 (vertexBufferViewSphereを使用)
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelData.vertices.size());
	vertexBufferView.StrideInBytes = sizeof(VertexData);




	VertexData* vertexData = nullptr;
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());
	vertexResource->Unmap(0, nullptr);



	//リソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResoucesSptite = createBufferResouces(device, sizeof(VertexData) * 6);



	//頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};

	//リソースの先端アドレスから使う
	vertexBufferViewSprite.BufferLocation = vertexResoucesSptite->GetGPUVirtualAddress();

	//使用するリソースのサイズは頂点3つ分サイズ
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 6;

	//1頂点当たりのサイズ
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	//Sprite用のマテリアルリソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResoucesSprite = createBufferResouces(device, sizeof(Material));

	//Sprite用のマテリアルリソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResoucesSphire = createBufferResouces(device, sizeof(Material));


	//Sprite用のマテリアルリソースを作る
	Microsoft::WRL::ComPtr <ID3D12Resource> DirectionalLightResoucesSprite = createBufferResouces(device, sizeof(DirectionalLight));

	Microsoft::WRL::ComPtr <ID3D12Resource> indexResoucesSprite = createBufferResouces(device, sizeof(uint32_t) * 6);




	//WVPのリソースを作る matrix4*4 一つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResouces = createBufferResouces(device, sizeof(TransformationMatrix));


	//データ書き込む
	TransformationMatrix* wvpData = nullptr;

	//書き込むためのアドレス
	wvpResouces->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
	wvpData->WVP = makeIdentity4x4();
	wvpData->world = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);




	//データ書き込む
	Material* materialDataSprite = nullptr;
	//mapしてデータを書き込む
	materialResoucesSprite->Map(0, nullptr, reinterpret_cast<VOID**>(&materialDataSprite));
	materialDataSprite->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialDataSprite->enableLighting = false;
	materialResoucesSprite->Unmap(0, nullptr);

	DirectionalLight* directLightData{};
	DirectionalLightResoucesSprite->Map(0, nullptr, reinterpret_cast<VOID**>(&directLightData));
	directLightData->color = { 1.0f, 1.0f,1.0f, 1.0f };
	directLightData->direction = { 0.0f, 0.0f, 0.0f };
	directLightData->direction = Normalize(directLightData->direction);
	directLightData->intensity = 1.0f;
	DirectionalLightResoucesSprite->Unmap(0, nullptr);


	//データ書き込む
	Material* materialData = nullptr;
	//mapしてデータを書き込む
	materialResoucesSphire->Map(0, nullptr, reinterpret_cast<VOID**>(&materialData));
	materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	//Spriteはlightingしないのでfalse;
	materialData->enableLighting = true;
	materialData->uvTransform = makeIdentity4x4();
	materialResoucesSphire->Unmap(0, nullptr);



	//インデックス用データ書き込み
	D3D12_INDEX_BUFFER_VIEW indexBufferView{};
	//リソースの戦闘アドレスから使う
	indexBufferView.BufferLocation = indexResoucesSprite->GetGPUVirtualAddress();
	//使用するリソースのズ.はインデックス6つ分のサイズ
	indexBufferView.SizeInBytes = sizeof(uint32_t) * 6;
	//インデックスはint32.とする
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;

	//インデックスリソースにデータを書き込む
	uint32_t* indexDataSprite = nullptr;
	indexResoucesSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0; indexDataSprite[1] = 1; indexDataSprite[2] = 2;
	indexDataSprite[3] = 1; indexDataSprite[4] = 3; indexDataSprite[5] = 2;




	//頂点リソースサイズデータに書き込む
	VertexData* vertexDataSprite = nullptr;

	//書き込むためのアドレスの取得

	vertexResoucesSptite->Map(0, nullptr, reinterpret_cast<VOID**>(&vertexDataSprite));

	//一枚目の三角形
	vertexDataSprite[0].position = { 0.0f, 0.0f, 0.0f, 1.0f };
	vertexDataSprite[0].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[0].normal = { 0.0f, 0.0f, -1.0f }; // Z方向奥向き

	// vertexDataSprite[1]: 右上 (width, 0)
	vertexDataSprite[1].position = { 640.0f, 0.0f, 0.0f, 1.0f };
	vertexDataSprite[1].texcoord = { 1.0f, 0.0f };
	vertexDataSprite[1].normal = { 0.0f, 0.0f, -1.0f };

	// vertexDataSprite[2]: 左下 (0, height)
	vertexDataSprite[2].position = { 0.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[2].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[2].normal = { 0.0f, 0.0f, -1.0f };

	// vertexDataSprite[3]: 右下 (width, height)
	vertexDataSprite[3].position = { 640.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[3].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[3].normal = { 0.0f, 0.0f, -1.0f };







	//TransForm周りを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResoucesSprite = createBufferResouces(device, sizeof(Matrix4x4));
	//データを書き込む
	Matrix4x4* transformationMatrixDataSprite = nullptr;
	//書き込むアドレスを取得
	transformationMatrixResoucesSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	//単位行列を書き込んでおく
	*transformationMatrixDataSprite = makeIdentity4x4();



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

	//imguiの初期化
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(device.Get(),
		swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescrriptorHeap.Get(),
		srvDescrriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescrriptorHeap->GetGPUDescriptorHandleForHeapStart());


	// DirectInput の初期化
	IDirectInput8* directInput = nullptr;
	result = DirectInput8Create(
		GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, reinterpret_cast<void**>(&directInput), nullptr);
	assert(SUCCEEDED(result));





	//キーボートデバイスの生成
	IDirectInputDevice8* keyboardDevice = nullptr;
	result = directInput->CreateDevice(GUID_SysKeyboard, &keyboardDevice, nullptr);
	assert(SUCCEEDED(result));

	//入力データ形式のセット
	result = keyboardDevice->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(result));
	//キーボードの排他モードを設定
	result = keyboardDevice->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	assert(SUCCEEDED(result));



	//textureを読んで転送する
	DirectX::ScratchImage mipImages = LoadTexture("resouces/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResouces = createTextreResouces(device, metadata);


	//2枚目のtextureを張る
	DirectX::ScratchImage mipImages2 = LoadTexture(modelData.material.textureFilePath);
	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureRouces2 = createTextreResouces(device, metadata2);


	SoundData soundData1 = SoundLoadWave("resouces/Alarm02.wav");



	//mataDataを基にSRVの設定1
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2dテクスチャ
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	//mataDataを基にSRVの設定2
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
	srvDesc2.Format = metadata2.format;
	srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2dテクスチャ
	srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);

	//SRVを作成するDescriptorHeapの場所を求める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 1);

	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGpudescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 1);
	// ImGuiがSRVヒープの0番目を使うため、テクスチャは1番インデックスに配置する
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGpudescriptorHandle(srvDescrriptorHeap, descriptorSizeSRV, 2);



	//テクスチャのSRVを作成する
	device->CreateShaderResourceView(textureResouces.Get(), &srvDesc, textureSrvHandleCPU);
	device->CreateShaderResourceView(textureRouces2.Get(), &srvDesc2, textureSrvHandleCPU2);

	//DepthStenclitextureをwindowのサイズを作成
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStenscilResouces = CreateDepthStencilTextResouces(device, kClientWidth, kClientHeight);

	//dsvの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;// format 基本的にはresoucesに合わせる
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device->CreateDepthStencilView(depthStenscilResouces.Get(), &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());


	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResouces = UploadTextureDeta(textureResouces, mipImages, device, commandList);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediteResouces2 = UploadTextureDeta(textureRouces2, mipImages2, device, commandList);
	//初期化



	Transform cameraTransform = { {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f},{0.0f, 0.0f, -10.0f} };
	Transform transformSprite = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
	Transform uvTransformSprite = { {1.0f, 1.0f,1.0f},{0.0f,0.0f, 0.0f},{0.0f,0.0f,0.0f} };
	bool useMonsterBall = true;

	// ★追加する行
	bool audioPlayedOnce = false; // 音声が一度再生されたかどうかのフラグ

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
			//画面の更新処理
			keyboardDevice->Acquire();

			BYTE key[256] = {};
			keyboardDevice->GetDeviceState(sizeof(key), key);

			//キーの状態を取得
			if (key[DIK_0])
			{
				OutputDebugStringA("HIT 0\n");
			}


			// 音声再生を一度だけにする制御
			if (!audioPlayedOnce) {
				SoundPlayWave(xAudio2.Get(), soundData1);
				audioPlayedOnce = true; // フラグを立てて、二度と再生しないようにする
			}

			bool temp_enableLighting = (materialData->enableLighting != 0);
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			ImGui::ShowDemoWindow();
			//ゲームの処理
			ImGui::Begin("MaterialColor");
			ImGui::ColorEdit4("color", &materrialData->color.x);
			ImGui::Checkbox("useMonsterBall", &useMonsterBall);
			if (ImGui::Checkbox("enableLighting", &temp_enableLighting)) {

				materialData->enableLighting = temp_enableLighting ? 1 : 0;
			}

			ImGui::ColorEdit3("Spritecolor", &directLightData->color.x);
			ImGui::DragFloat3("direction", &directLightData->direction.x);
			ImGui::DragFloat("intensity", &directLightData->intensity);
			ImGui::DragFloat2("UVTranslate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat2("UVScale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
			ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z);
			ImGui::End();

			ShowSRTWindow(transform);


			;
			Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
			//カメラの処理
			Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
			Matrix4x4 viewMatrix = Inverse(cameraMatrix);
			Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
			Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
			wvpData->WVP = worldViewProjectionMatrix;
			wvpData->world = worldMatrix;

			Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 viewMatrixSprite = makeIdentity4x4();
			Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);
			Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));

			*transformationMatrixDataSprite = worldViewProjectionMatrixSprite;

			//編集と行列の作成
			Matrix4x4 uvTransformMatrix = MakeScaleMatrix(uvTransformSprite.scale);
			uvTransformMatrix = Multiply(uvTransformMatrix, MakeRotateZMatrix(uvTransformSprite.rotate.z));
			uvTransformMatrix = Multiply(uvTransformMatrix, MakeTranslateMatrix(uvTransformSprite.translate));
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
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

			//形状を設定psoに設定しているものとはまた別　同じものを設定するトロ考えておけば良い
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			//マテリアルcBubufferの場所設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResouces->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(0, materialResoucesSphire->GetGPUVirtualAddress());
			//wvpのcBufferの場所設定
			commandList->SetGraphicsRootConstantBufferView(1, wvpResouces->GetGPUVirtualAddress());
			//directionalLightのcBufferの場所設定
			commandList->SetGraphicsRootConstantBufferView(3, DirectionalLightResoucesSprite->GetGPUVirtualAddress());

			//テクスチャのSRVの場所設定
			commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU);

			commandList->DrawInstanced(static_cast<UINT>(modelData.vertices.size()), 1, 0, 0);


			ImGui::Render();


		
					commandList->SetGraphicsRootConstantBufferView(0, materialResoucesSprite->GetGPUVirtualAddress());
					commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
					commandList->IASetIndexBuffer(&indexBufferView);
					commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
					commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResoucesSprite->GetGPUVirtualAddress());

					commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
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


	Log(logStrem, "Hello,DirectX!\n");
	Log(logStrem,
		ConvertString(
			std::format(
				L"clientSize:{} {}\n",
				kClientWidth,
				kClientHeight)));

	CloseWindow(hwnd);







	//vertexResouces->Release();
		//解放処理
	CloseHandle(fenceEvent);
	signatureBlob->Release();
	if (errorBlob)
	{
		errorBlob->Release();
	}

	pixelShaderBlob->Release();
	vertexShaderBlob->Release();
	xAudio2.Reset();
	SoundUnload(&soundData1);








	//
	//	fence->Release();
	//	rtvDescrriptorHeap->Release();
	//	srvDescrriptorHeap->Release();
	//	swapChainResouces[0]->Release();
	//	swapChainResouces[1]->Release();
	//	swapChain->Release();
	//	commandList->Release();
	//	commandAllocator->Release();
	//	commandQueue->Release();
	//	device->Release();
	//	useAsapter->Release();
	//	dxgiFactory->Release();
	//
	//	//vertexResouces->Release();
	//	graphicsPipelineState->Release();
	//	signatureBlob->Release();
	//	if (errorBlob)
	//	{
	//		errorBlob->Release();
	//	}
	//	rootsignatrue->Release();
	//	pixelShaderBlob->Release();
	//	vertexShaderBlob->Release();
	//	materialResouces->Release();
	//	wvpResouces->Release();
	//	textureResouces->Release();
	//	intermediteResouces->Release();
	//	depthStenscilResouces->Release();
	//	dsvDescriptorHeap->Release();
	//	vertexResoucesSptite->Release();
	//	transformationMatrixResoucesSprite->Release();
	//	textureRouces2->Release();
	//	intermediteResouces2->Release();
	//	materialResoucesSprite->Release();
	//	materialResoucesSphire->Release();
	//	DirectionalLightResoucesSprite->Release();
	//	vertexResource->Release();
	//	//indexResourceSphere->Release();
	//	indexResoucesSprite->Release();
	//
	//
	//
	//
	//
	//#ifdef _DEBUG
	//
	//	debugController->Release();
	//#endif // _DEBUG








	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	//comの終了時
	CoUninitialize();




	return 0;
}