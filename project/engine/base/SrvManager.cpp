#include "SRVManager.h"
#include "engine/base/DirectXCommon.h"
#include <cassert>

SRVManager* SRVManager::GetInstance() {
    static SRVManager instance;
    return &instance;
}

void SRVManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon_->GetDevice();

    // デスクリプタヒープの生成
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NumDescriptors = kMaxSRVCount;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hr = device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvDescriptorHeap_));
    assert(SUCCEEDED(hr));

    // デスクリプタサイズの取得
    descriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

uint32_t SRVManager::CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc) {
    ID3D12Device* device = dxCommon_->GetDevice();
    assert(nextIndex_ < kMaxSRVCount);

    // SRVを作成するデスクリプタハンドルの取得
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += (descriptorSize_ * nextIndex_);

    // SRVの作成
    device->CreateShaderResourceView(resource, &srvDesc, cpuHandle);

    // 作成したSRVのインデックスを返す
    return nextIndex_++;
}

D3D12_GPU_DESCRIPTOR_HANDLE SRVManager::GetGPUHandle(uint32_t srvHandle) {
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
    gpuHandle.ptr += (descriptorSize_ * srvHandle);
    return gpuHandle;
}

void SRVManager::SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, uint32_t srvHandle) {
    // 対応するGPUハンドルを取得
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = GetGPUHandle(srvHandle);
    // デスクリプタテーブルをコマンドリストに設定
    commandList->SetGraphicsRootDescriptorTable(rootParameterIndex, gpuHandle);
}