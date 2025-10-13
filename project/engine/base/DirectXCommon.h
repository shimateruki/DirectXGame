#pragma once

// ======== DirectXの基本機能に必要なヘッダーファイル ========
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h> // ComPtrを使うのに必要
#include <string>
#include <chrono>

// ======== 外部ライブラリのヘッダーファイル ========
#include <dxcapi.h> // シェーダーコンパイルに必要
#include "externals/DirectXTex/DirectXTex.h"

// 前方宣言 
class WinApp;

// =================================================================
// DirectXの様々な初期化や機能を集約した、プログラムの基盤となるクラス
// Singletonパターンで、プログラム全体で唯一のインスタンスを共有する
// =================================================================
class DirectXCommon {
public:
	// ======== publicなメンバ関数（外部から呼び出して使う機能） ========

	/// <summary>
	/// Singletonインスタンスの取得
	/// </summary>
	static DirectXCommon* GetInstance();

	/// <summary>
	/// DirectXの各種初期化処理をまとめた関数
	/// </summary>
	void Initialize(WinApp* winApp);

	/// <summary>
	/// 終了処理 (ImGuiの終了など)
	/// </summary>
	void Finalize();

	/// <summary>
	/// 毎フレームの描画前に行う処理
	/// </summary>
	void PreDraw();

	/// <summary>
	/// 毎フレームの描画後に行う処理
	/// </summary>
	void PostDraw();


	// --- ゲッター関数（privateなメンバ変数を外部から安全に取得する） ---

	ID3D12Device* GetDevice() const { return device_.Get(); }
	ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }
	DXGI_FORMAT GetRTVFormat() const { return rtvFormat_; }
	size_t GetBackBufferCount() const { return backBufferCount_; }

	// 最大SRV数（最大テクスチャ枚数）
	static const uint32_t kMaxSRVCount = 512;
	void InitalaizeFixFPS();
	void UpdateFixFPS();

	// --- ユーティリティ関数（便利なヘルパー機能） ---

	/// <summary>
	/// HLSLシェーダーファイルをコンパイルする
	/// </summary>
	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profile);

	/// <summary>
	/// 汎用的なバッファリソースを作成する
	/// </summary>
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);

	/// <summary>
	/// テクスチャリソースを作成する
	/// </summary>
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);

	/// <summary>
	/// テクスチャデータをリソースにアップロードする (WriteToSubresource版)
	/// </summary>
	void UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages);

	/// <summary>
	/// テクスチャファイルを読み込む (staticなのでインスタンス不要)
	/// </summary>
	static DirectX::ScratchImage LoadTexture(const std::string& filePath);

	/// <summary>
	/// string を wstring に変換する (staticなのでインスタンス不要)
	/// </summary>
	static std::wstring ConvertString(const std::string& str);
	void FlushCommandQueue(bool reset = true);

private:
	// ======== privateなメンバ関数（このクラスの内部でのみ使う機能） ========

	// Singletonにするためのコンストラクタ等のprivate化
	DirectXCommon() = default;
	~DirectXCommon() = default;
	DirectXCommon(const DirectXCommon&) = delete;
	const DirectXCommon& operator=(const DirectXCommon&) = delete;

	// 各種初期化処理
	void InitializeDXGIDevice(); // DXGIデバイスの初期化
	void CreateCommand();        // コマンド関連の初期化
	void CreateSwapChain();      // スワップチェインの作成
	void CreateRTV();            // レンダーターゲットビューの作成
	void CreateDSV();            // 深度ステンシルビューの作成
	void CreateFence();          // フェンスの作成

	// DSV用のテクスチャリソースを作成するヘルパー
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(int32_t width, int32_t height);


private:
	// ======== privateなメンバ変数（このクラスが内部で保持するデータ） ========

	WinApp* winApp_ = nullptr; // WindowsAPIクラス

	//記録時間
	std::chrono::steady_clock::time_point reference_;

	// --- DirectXオブジェクト ---
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

	// --- GPUとの同期用 ---
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_ = nullptr;
	uint64_t fenceValue_ = 0;
	HANDLE fenceEvent_ = nullptr;

	// --- 描画領域 ---
	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissorRect_{};

	// --- その他 ---
	DXGI_FORMAT rtvFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	UINT backBufferIndex_ = 0;
	const size_t backBufferCount_ = 2;

	// --- シェーダーコンパイル用 ---
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_ = nullptr;
};