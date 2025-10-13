#include "SRVManager.h"
#include "engine/base/DirectXCommon.h"
#include <cassert>

SRVManager* SRVManager::GetInstance() {
    static SRVManager instance;
    return &instance;
}

void SRVManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    device_ = dxCommon->GetDevice();

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NumDescriptors = kMaxSRVCount;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hr = device_->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvDescriptorHeap_));
    assert(SUCCEEDED(hr));

    descriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

uint32_t SRVManager::CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc) {
    assert(nextIndex_ < kMaxSRVCount);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += (descriptorSize_ * nextIndex_);
    device_->CreateShaderResourceView(resource, &srvDesc, cpuHandle);
    return nextIndex_++;
}

void SRVManager::SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, uint32_t srvHandle) {
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
    gpuHandle.ptr += (descriptorSize_ * srvHandle);
    commandList->SetGraphicsRootDescriptorTable(rootParameterIndex, gpuHandle);
}