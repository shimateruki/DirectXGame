#pragma once

// ======== DirectX�̊�{�@�\�ɕK�v�ȃw�b�_�[�t�@�C�� ========
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h> // ComPtr���g���̂ɕK�v
#include <string>
#include <chrono>

// ======== �O�����C�u�����̃w�b�_�[�t�@�C�� ========
#include <dxcapi.h> // �V�F�[�_�[�R���p�C���ɕK�v
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#include "externals/DirectXTex/DirectXTex.h"

// �O���錾 
class WinApp;

// =================================================================
// DirectX�̗l�X�ȏ�������@�\���W�񂵂��A�v���O�����̊�ՂƂȂ�N���X
// Singleton�p�^�[���ŁA�v���O�����S�̂ŗB��̃C���X�^���X�����L����
// =================================================================
class DirectXCommon {
public:
	// ======== public�ȃ����o�֐��i�O������Ăяo���Ďg���@�\�j ========

	/// <summary>
	/// Singleton�C���X�^���X�̎擾
	/// </summary>
	static DirectXCommon* GetInstance();

	/// <summary>
	/// DirectX�̊e�평�����������܂Ƃ߂��֐�
	/// </summary>
	void Initialize(WinApp* winApp);

	/// <summary>
	/// �I������ (ImGui�̏I���Ȃ�)
	/// </summary>
	void Finalize();

	/// <summary>
	/// ���t���[���̕`��O�ɍs������
	/// </summary>
	void PreDraw();

	/// <summary>
	/// ���t���[���̕`���ɍs������
	/// </summary>
	void PostDraw();


	// --- �Q�b�^�[�֐��iprivate�ȃ����o�ϐ����O��������S�Ɏ擾����j ---

	ID3D12Device* GetDevice() const { return device_.Get(); }
	ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }
	DXGI_FORMAT GetRTVFormat() const { return rtvFormat_; }
	size_t GetBackBufferCount() const { return backBufferCount_; }
	ID3D12DescriptorHeap* GetSrvDescriptorHeap() const { return srvDescriptorHeap_.Get(); }

	void InitalaizeFixFPS();
	void UpdateFixFPS();

	// --- ���[�e�B���e�B�֐��i�֗��ȃw���p�[�@�\�j ---

	/// <summary>
	/// HLSL�V�F�[�_�[�t�@�C�����R���p�C������
	/// </summary>
	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profile);

	/// <summary>
	/// �ėp�I�ȃo�b�t�@���\�[�X���쐬����
	/// </summary>
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);

	/// <summary>
	/// �e�N�X�`�����\�[�X���쐬����
	/// </summary>
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);

	/// <summary>
	/// �e�N�X�`���f�[�^�����\�[�X�ɃA�b�v���[�h���� (WriteToSubresource��)
	/// </summary>
	void UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages);

	/// <summary>
	/// �e�N�X�`���t�@�C����ǂݍ��� (static�Ȃ̂ŃC���X�^���X�s�v)
	/// </summary>
	static DirectX::ScratchImage LoadTexture(const std::string& filePath);

	/// <summary>
	/// string �� wstring �ɕϊ����� (static�Ȃ̂ŃC���X�^���X�s�v)
	/// </summary>
	static std::wstring ConvertString(const std::string& str);


private:
	// ======== private�ȃ����o�֐��i���̃N���X�̓����ł̂ݎg���@�\�j ========

	// Singleton�ɂ��邽�߂̃R���X�g���N�^����private��
	DirectXCommon() = default;
	~DirectXCommon() = default;
	DirectXCommon(const DirectXCommon&) = delete;
	const DirectXCommon& operator=(const DirectXCommon&) = delete;

	// �e�평��������
	void InitializeDXGIDevice(); // DXGI�f�o�C�X�̏�����
	void CreateCommand();        // �R�}���h�֘A�̏�����
	void CreateSwapChain();      // �X���b�v�`�F�C���̍쐬
	void CreateRTV();            // �����_�[�^�[�Q�b�g�r���[�̍쐬
	void CreateDSV();            // �[�x�X�e���V���r���[�̍쐬
	void CreateFence();          // �t�F���X�̍쐬
	void InitializeImGui();      // ImGui�̏�����

	// DSV�p�̃e�N�X�`�����\�[�X���쐬����w���p�[
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(int32_t width, int32_t height);


private:
	// ======== private�ȃ����o�ϐ��i���̃N���X�������ŕێ�����f�[�^�j ========

	WinApp* winApp_ = nullptr; // WindowsAPI�N���X

	//�L�^����
	std::chrono::steady_clock::time_point reference_;

	// --- DirectX�I�u�W�F�N�g ---
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Device> device_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_ = nullptr;
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources_[2] = {};
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_ = nullptr;

	// --- GPU�Ƃ̓����p ---
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_ = nullptr;
	uint64_t fenceValue_ = 0;
	HANDLE fenceEvent_ = nullptr;

	// --- �`��̈� ---
	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissorRect_{};

	// --- ���̑� ---
	DXGI_FORMAT rtvFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	UINT backBufferIndex_ = 0;
	const size_t backBufferCount_ = 2;

	// --- �V�F�[�_�[�R���p�C���p ---
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_ = nullptr;
};