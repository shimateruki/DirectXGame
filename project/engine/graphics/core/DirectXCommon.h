#pragma once

// ======== DirectX縺ｮ蝓ｺ譛ｬ讖溯・縺ｫ蠢・ｦ√↑繝倥ャ繝繝ｼ繝輔ぃ繧､繝ｫ ========
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h> // ComPtr繧剃ｽｿ縺・・縺ｫ蠢・ｦ・
#include <string>
#include <chrono>

// ======== 螟夜Κ繝ｩ繧､繝悶Λ繝ｪ縺ｮ繝倥ャ繝繝ｼ繝輔ぃ繧､繝ｫ ========
#include <dxcapi.h> // 繧ｷ繧ｧ繝ｼ繝繝ｼ繧ｳ繝ｳ繝代う繝ｫ縺ｫ蠢・ｦ・
#include "DirectXTex.h"

// 蜑肴婿螳｣險 
class WinApp;

// =================================================================
// DirectX縺ｮ讒倥・↑蛻晄悄蛹悶ｄ讖溯・繧帝寔邏・＠縺溘√・繝ｭ繧ｰ繝ｩ繝縺ｮ蝓ｺ逶､縺ｨ縺ｪ繧九け繝ｩ繧ｹ
// Singleton繝代ち繝ｼ繝ｳ縺ｧ縲√・繝ｭ繧ｰ繝ｩ繝蜈ｨ菴薙〒蜚ｯ荳縺ｮ繧､繝ｳ繧ｹ繧ｿ繝ｳ繧ｹ繧貞・譛峨☆繧・
// =================================================================
class DirectXCommon {
public:
	// ======== public縺ｪ繝｡繝ｳ繝宣未謨ｰ・亥､夜Κ縺九ｉ蜻ｼ縺ｳ蜃ｺ縺励※菴ｿ縺・ｩ溯・・・========

	/// <summary>
	/// Singleton繧､繝ｳ繧ｹ繧ｿ繝ｳ繧ｹ縺ｮ蜿門ｾ・
	/// </summary>
	static DirectXCommon* GetInstance();

	/// <summary>
	/// DirectX縺ｮ蜷・ｨｮ蛻晄悄蛹門・逅・ｒ縺ｾ縺ｨ繧√◆髢｢謨ｰ
	/// </summary>
	void Initialize(WinApp* winApp);

	/// <summary>
	/// 邨ゆｺ・・逅・(ImGui縺ｮ邨ゆｺ・↑縺ｩ)
	/// </summary>
	void Finalize();

	/// <summary>
	/// 豈弱ヵ繝ｬ繝ｼ繝縺ｮ謠冗判蜑阪↓陦後≧蜃ｦ逅・
	/// </summary>
	void PreDraw();

	/// <summary>
	/// 豈弱ヵ繝ｬ繝ｼ繝縺ｮ謠冗判蠕後↓陦後≧蜃ｦ逅・
	/// </summary>
	void PostDraw();


	// --- 繧ｲ繝・ち繝ｼ髢｢謨ｰ・・rivate縺ｪ繝｡繝ｳ繝仙､画焚繧貞､夜Κ縺九ｉ螳牙・縺ｫ蜿門ｾ励☆繧具ｼ・---

	ID3D12Device* GetDevice() const { return device_.Get(); }
	ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }
	DXGI_FORMAT GetRTVFormat() const { return rtvFormat_; }
	size_t GetBackBufferCount() const { return backBufferCount_; }

	// 譛螟ｧSRV謨ｰ・域怙螟ｧ繝・け繧ｹ繝√Ε譫壽焚・・
	static const uint32_t kMaxSRVCount = 512;
	void InitalaizeFixFPS();
	void UpdateFixFPS();

	// --- 繝ｦ繝ｼ繝・ぅ繝ｪ繝・ぅ髢｢謨ｰ・井ｾｿ蛻ｩ縺ｪ繝倥Ν繝代・讖溯・・・---

	/// <summary>
	/// HLSL繧ｷ繧ｧ繝ｼ繝繝ｼ繝輔ぃ繧､繝ｫ繧偵さ繝ｳ繝代う繝ｫ縺吶ｋ
	/// </summary>
	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profileconst , const wchar_t* entryPoint = L"main");

	/// <summary>
	/// 豎守畑逧・↑繝舌ャ繝輔ぃ繝ｪ繧ｽ繝ｼ繧ｹ繧剃ｽ懈・縺吶ｋ
	/// </summary>
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);

	/// <summary>
	/// 繝・け繧ｹ繝√Ε繝ｪ繧ｽ繝ｼ繧ｹ繧剃ｽ懈・縺吶ｋ
	/// </summary>
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);

	/// <summary>
	/// 繝・け繧ｹ繝√Ε繝・・繧ｿ繧偵Μ繧ｽ繝ｼ繧ｹ縺ｫ繧｢繝・・繝ｭ繝ｼ繝峨☆繧・(WriteToSubresource迚・
	/// </summary>
	void UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages);

	/// <summary>
	/// 繝・け繧ｹ繝√Ε繝輔ぃ繧､繝ｫ繧定ｪｭ縺ｿ霎ｼ繧 (static縺ｪ縺ｮ縺ｧ繧､繝ｳ繧ｹ繧ｿ繝ｳ繧ｹ荳崎ｦ・
	/// </summary>
	static DirectX::ScratchImage LoadTexture(const std::string& filePath);

	/// <summary>
	/// string 繧・wstring 縺ｫ螟画鋤縺吶ｋ (static縺ｪ縺ｮ縺ｧ繧､繝ｳ繧ｹ繧ｿ繝ｳ繧ｹ荳崎ｦ・
	/// </summary>
	static std::wstring ConvertString(const std::string& str);
	void FlushCommandQueue(bool reset = true);

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);


	/// <summary>
	/// 讓呎ｺ也噪縺ｪ豺ｱ蠎ｦ繧ｹ繝・Φ繧ｷ繝ｫ繝・せ繧ｯ繝ｪ繝励ち繧貞叙蠕励☆繧・
	/// </summary>
	D3D12_DEPTH_STENCIL_DESC GetDefaultDepthStencilDesc() const;

	/// <summary>
	/// 豺ｱ蠎ｦ繧ｹ繝・Φ繧ｷ繝ｫ繝薙Η繝ｼ縺ｮ繝輔か繝ｼ繝槭ャ繝医ｒ蜿門ｾ励☆繧・
	/// </summary>
	DXGI_FORMAT GetDSVFormat() const;



	void WaitForGPUAndReset();



	ID3D12CommandAllocator* GetCommandAllocator() const { return commandAllocator_.Get(); }
	ID3D12CommandQueue* GetCommandQueue() const { return commandQueue_.Get(); }
	ID3D12Fence* GetFence() const { return fence_.Get(); }
	uint64_t GetFenceValue() const { return fenceValue_; }
	HANDLE GetFenceEvent() const { return fenceEvent_; }

	//  繝ｬ繝ｳ繝繝ｼ繝・け繧ｹ繝√Ε縺ｮ蛻晄悄蛹・
	void CreateRenderTexture();

	//  謠冗判蜈医ｒ縲後Ξ繝ｳ繝繝ｼ繝・け繧ｹ繝√Ε縲阪↓蛻・ｊ譖ｿ縺医ｋ (GameView謠冗判髢句ｧ・
	void PreDrawRenderTexture();

	//  謠冗判蜈医ｒ縲檎判髱｢(繝舌ャ繧ｯ繝舌ャ繝輔ぃ)縲阪↓謌ｻ縺・(GameView謠冗判邨ゆｺ・
	void PostDrawRenderTexture();

	//  ImGui縺ｧ陦ｨ遉ｺ縺吶ｋ縺溘ａ縺ｮ SRV繝上Φ繝峨Ν繧貞叙蠕・
	uint32_t GetRenderTextureSrvHandle() const { return renderTextureSrvHandle_; }
	void PreDrawBackBuffer(); // 謠冗判蜈医ｒ繝舌ャ繧ｯ繝舌ャ繝輔ぃ縺ｫ謌ｻ縺呻ｼ医Μ繧ｻ繝・ヨ縺ｪ縺暦ｼ・
	void CreateShadowMap();
	uint32_t GetShadowMapSrvHandle() const { return shadowMapSrvHandle_; }
	void PreDrawShadow();
	void PostDrawShadow();
	float GetGpuDrawTimeMs() const { return gpuDrawTimeMs_; }
	// GPU繝励Ο繝輔ぃ繧､繝ｩ謫堺ｽ懃畑
	void StartGpuProfile();
	void EndGpuProfile();
	void ReadGpuProfile();
	void CreateDepthSrv();
	uint32_t GetDepthSrvHandle() const { return depthSrvHandle_; }

	void PreDrawLocalFog();
	void PostDrawLocalFog();
	uint32_t GetGrabSrvHandle() const { return grabSrvHandle_; }
	void UpdateGrabTexture();
	void ResizeSwapChain(int32_t width, int32_t height);
	ID3D12GraphicsCommandList* GetLoadCommandList() const { return loadCommandList_.Get(); }
	void ExecuteLoadCommands();
private:
	// ======== private縺ｪ繝｡繝ｳ繝宣未謨ｰ・医％縺ｮ繧ｯ繝ｩ繧ｹ縺ｮ蜀・Κ縺ｧ縺ｮ縺ｿ菴ｿ縺・ｩ溯・・・========

	// Singleton縺ｫ縺吶ｋ縺溘ａ縺ｮ繧ｳ繝ｳ繧ｹ繝医Λ繧ｯ繧ｿ遲峨・private蛹・
	DirectXCommon() = default;
	~DirectXCommon() = default;
	DirectXCommon(const DirectXCommon&) = delete;
	const DirectXCommon& operator=(const DirectXCommon&) = delete;

	// 蜷・ｨｮ蛻晄悄蛹門・逅・
	void InitializeDXGIDevice(); // DXGI繝・ヰ繧､繧ｹ縺ｮ蛻晄悄蛹・
	void CreateCommand();        // 繧ｳ繝槭Φ繝蛾未騾｣縺ｮ蛻晄悄蛹・
	void CreateSwapChain();      // 繧ｹ繝ｯ繝・・繝√ぉ繧､繝ｳ縺ｮ菴懈・
	void CreateRTV();            // 繝ｬ繝ｳ繝繝ｼ繧ｿ繝ｼ繧ｲ繝・ヨ繝薙Η繝ｼ縺ｮ菴懈・
	void CreateDSV();            // 豺ｱ蠎ｦ繧ｹ繝・Φ繧ｷ繝ｫ繝薙Η繝ｼ縺ｮ菴懈・
	void CreateFence();          // 繝輔ぉ繝ｳ繧ｹ縺ｮ菴懈・

	// DSV逕ｨ縺ｮ繝・け繧ｹ繝√Ε繝ｪ繧ｽ繝ｼ繧ｹ繧剃ｽ懈・縺吶ｋ繝倥Ν繝代・
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(int32_t width, int32_t height);



private:
	// ======== private縺ｪ繝｡繝ｳ繝仙､画焚・医％縺ｮ繧ｯ繝ｩ繧ｹ縺悟・驛ｨ縺ｧ菫晄戟縺吶ｋ繝・・繧ｿ・・========

	WinApp* winApp_ = nullptr; // WindowsAPI繧ｯ繝ｩ繧ｹ

	//險倬鹸譎る俣
	std::chrono::steady_clock::time_point reference_;

	// --- DirectX繧ｪ繝悶ず繧ｧ繧ｯ繝・---
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Device> device_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_ = nullptr;
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources_[2] = {};
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_ = nullptr;
	uint32_t depthSrvHandle_ = 0; 
	// --- GPU縺ｨ縺ｮ蜷梧悄逕ｨ ---
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_ = nullptr;
	uint64_t fenceValue_ = 0;
	HANDLE fenceEvent_ = nullptr;

	// --- 謠冗判鬆伜沺 ---
	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissorRect_{};

	// --- 縺昴・莉・---
	DXGI_FORMAT rtvFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	UINT backBufferIndex_ = 0;
	const size_t backBufferCount_ = 2;

	// --- 繧ｷ繧ｧ繝ｼ繝繝ｼ繧ｳ繝ｳ繝代う繝ｫ逕ｨ ---
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_ = nullptr;
	//  繝ｬ繝ｳ繝繝ｼ繝・け繧ｹ繝√Ε縺ｮ繝ｪ繧ｽ繝ｼ繧ｹ
	Microsoft::WRL::ComPtr<ID3D12Resource> renderTexture_;

	//  繝ｬ繝ｳ繝繝ｼ繝・け繧ｹ繝√Ε蟆ら畑縺ｮRTV繝偵・繝・(縺薙％縺ｫ謠冗判縺吶ｋ縺溘ａ縺ｮ繝上Φ繝峨Ν)
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtRtvHeap_;

	//  SRVManager荳翫・繧､繝ｳ繝・ャ繧ｯ繧ｹ逡ｪ蜿ｷ
	uint32_t renderTextureSrvHandle_ = 0;

	//  GameView縺ｮ繧ｯ繝ｪ繧｢繧ｫ繝ｩ繝ｼ (蜍穂ｽ懃｢ｺ隱咲畑縺ｫ縲檎ｷ代阪↓縺励※縺翫″縺ｾ縺・
	float clearColor_[4] = { 0.0f, 1.0f, 0.0f, 1.0f };

	Microsoft::WRL::ComPtr<ID3D12Resource> shadowMapResource_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shadowDsvHeap_ = nullptr;
	uint32_t shadowMapSrvHandle_ = 0;
	// 繧ｷ繝｣繝峨え繝槭ャ繝励・隗｣蜒丞ｺｦ・医→繧翫≠縺医★1024x1024縲らｶｺ鮗励↓縺励◆縺・↑繧・048繧・096縺ｫ・・
	static const int kShadowMapWidth = 2048;
	static const int kShadowMapHeight = 2048;
	//Gpu縺ｮ譎る俣險域ｸｬ逕ｨ
	Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
	Microsoft::WRL::ComPtr<ID3D12Resource> queryResultBuffer_;
	uint64_t gpuFrequency_ = 0; // GPU縺ｮ繧ｿ繧､繝槭・縺ｮ蜻ｨ豕｢謨ｰ
	float gpuDrawTimeMs_ = 0.0f; // 險域ｸｬ邨先棡・医Α繝ｪ遘抵ｼ・
	Microsoft::WRL::ComPtr<ID3D12Resource> grabTexture_;
	uint32_t grabSrvHandle_ = 0;

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> loadCommandAllocator_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> loadCommandList_ = nullptr;
};
