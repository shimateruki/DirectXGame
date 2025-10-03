#pragma once
#include "DirectXCommon.h"
#include <string>
#include <vector>
#include <wrl.h>

class TextureManager {
public:
	static TextureManager* GetInstance();
	void Initialize(DirectXCommon* dxCommon);

	/// <summary>
	/// テクスチャを読み込む。すでに読み込み済みの場合はそのハンドルを返す。
	/// </summary>
	uint32_t Load(const std::string& filePath);

	/// <summary>
	/// GPUハンドルを取得する
	/// </summary>
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(uint32_t textureIndex);

private:
	TextureManager() = default;
	~TextureManager() = default;
	TextureManager(const TextureManager&) = delete;
	const TextureManager& operator=(const TextureManager&) = delete;

	static std::wstring ConvertString(const std::string& str);

	struct TextureData {
		std::string filePath;
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU;
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU;
	};

private:
	DirectXCommon* dxCommon_ = nullptr;
	std::vector<TextureData> textureDatas_;
	static const uint32_t kSRVIndexTop = 1;
};