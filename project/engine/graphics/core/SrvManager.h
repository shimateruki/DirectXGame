#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

class DirectXCommon;

class SRVManager {
public:
    static const size_t kMaxSRVCount = 256;

public:
    static SRVManager* GetInstance();
    void Initialize(DirectXCommon* dxCommon);
    uint32_t CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);
    ID3D12DescriptorHeap* GetDescriptorHeap() const { return srvDescriptorHeap_.Get(); }
    void SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, uint32_t srvHandle);
    /// <summary>
    /// SRVヒープをコマンドリストに設定する
    /// </summary>
    void SetDescriptorHeaps(ID3D12GraphicsCommandList* commandList);

private:
    SRVManager() = default;
    ~SRVManager() = default;
    SRVManager(const SRVManager&) = delete;
    SRVManager& operator=(const SRVManager&) = delete;

private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap_;
    uint32_t descriptorSize_ = 0;
    // 0番はImGuiが使用するため、1からインデックスを割り当てる
    uint32_t nextIndex_ = 1;
};