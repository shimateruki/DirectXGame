#include "SRVManager.h"
#include "DirectXCommon.h"
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

    // 再初期化した場合も ImGui が予約する 0、1 を避けて払い出しを始めます。
    nextIndex_ = 2;
}

uint32_t SRVManager::CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc) {
    // 記述子の払い出しとビュー作成を同じロック内で行い、並列登録時の重複を防ぐ。
    std::lock_guard<std::mutex> lock(mutex_);
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

void SRVManager::SetDescriptorHeaps(ID3D12GraphicsCommandList* commandList) {
    // 描画コマンドにこの SRV ヒープを明示してから記述子テーブルを使用する。
    assert(srvDescriptorHeap_);

    ID3D12DescriptorHeap* pHeaps[] = { srvDescriptorHeap_.Get() };
    commandList->SetDescriptorHeaps(1, pHeaps);
}

uint32_t SRVManager::Allocate() {
    // ビューを後で作る呼び出しにも、同じ連番から記述子枠を確保する。
    std::lock_guard<std::mutex> lock(mutex_);
    assert(nextIndex_ < kMaxSRVCount);
    uint32_t index = nextIndex_;
    nextIndex_++;
    return index;
}

void SRVManager::CreateSRVforResource(uint32_t index, ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc) {
    // 事前に確保した枠へビューを書き込み、払い出し位置は進めない。
    std::lock_guard<std::mutex> lock(mutex_);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += (static_cast<unsigned long long>(descriptorSize_) * index);
    device_->CreateShaderResourceView(pResource, &srvDesc, cpuHandle);
}

D3D12_GPU_DESCRIPTOR_HANDLE SRVManager::GetGPUDescriptorHandle(uint32_t index) {
    // SRVヒープの先頭から記述子一個分の幅で進め、シェーダーに渡す位置を求める。
    if (index >= kMaxSRVCount) {
        assert(false && "SRV Index out of range!");
    }

    D3D12_GPU_DESCRIPTOR_HANDLE handle = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += (static_cast<UINT64>(descriptorSize_) * index);

    return handle;
}

void SRVManager::SetComputeRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, uint32_t srvHandle) {
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = GetGPUDescriptorHandle(srvHandle);
    commandList->SetComputeRootDescriptorTable(rootParameterIndex, gpuHandle);
}

D3D12_CPU_DESCRIPTOR_HANDLE SRVManager::GetCPUDescriptorHandle(uint32_t index) {
    // CPUから記述子を書き換える際はGPU用とは別のヒープ先頭ハンドルを使う。
    if (index >= kMaxSRVCount) {
        assert(false && "SRV Index out of range!");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += (static_cast<SIZE_T>(descriptorSize_) * index);

    return handle;
}
