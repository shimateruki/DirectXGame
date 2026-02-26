#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

class DirectXCommon;

class SRVManager {
public:
    // ImGuiがIndex 1を使うので、十分な数を用意する
    static const size_t kMaxSRVCount = 2018;

public:
    static SRVManager* GetInstance();

    // 初期化
    void Initialize(DirectXCommon* dxCommon);

    // SRV生成（空いている場所を使ってViewを作る）
    uint32_t CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);

    // ヒープ取得（変数を一本化したので安全）
    ID3D12DescriptorHeap* GetDescriptorHeap() const { return srvDescriptorHeap_.Get(); }

    // ルートパラメータにヒープの場所をセットする
    void SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, uint32_t srvHandle);

    // コマンドリストにヒープを登録する
    void SetDescriptorHeaps(ID3D12GraphicsCommandList* commandList);
    //  確保済みのインデックスを指定してSRVを作る
    void CreateSRVforResource(uint32_t index, ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);
    uint32_t Allocate();
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);
    // Compute Shader用のルートパラメータセット関数
    void SetComputeRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, uint32_t srvHandle);

    // CPUディスクリプタハンドルの取得
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);
private:
    SRVManager() = default;
    ~SRVManager() = default;
    SRVManager(const SRVManager&) = delete;
    SRVManager& operator=(const SRVManager&) = delete;

private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_ = nullptr;

    // ★修正：変数をこれ1つに統一！（descriptorHeap_ は削除済み）
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_ = nullptr;

    uint32_t descriptorSize_ = 0;

    // ★修正：ImGuiが 0, 1 を使うので、ゲーム用は「2」から配る
    uint32_t nextIndex_ = 2;
};