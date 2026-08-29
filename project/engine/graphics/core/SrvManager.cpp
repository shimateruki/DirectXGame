#include "SRVManager.h"
#include "DirectXCommon.h"
#include <cassert>
// SRVManagerのシングルトンインスタンスを返す。

SRVManager* SRVManager::GetInstance() {
    static SRVManager instance;
    return &instance;
}
// CBV/SRV/UAV用のシェーダー可視ディスクリプタヒープを作成する。

void SRVManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    device_ = dxCommon->GetDevice();

    // デスクリプタヒープの生成設定
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NumDescriptors = kMaxSRVCount; // 256個確保
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    // SRVはこのManagerが所有する共通Descriptor Heapへ生成します。
    HRESULT hr = device_->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvDescriptorHeap_));
    assert(SUCCEEDED(hr));

    // デスクリプタ1個分のサイズを取得
    descriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


    nextIndex_ = 2;
}
// 次に空いているSRVスロットへリソースを登録し、使用したハンドル番号を返す。

uint32_t SRVManager::CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 範囲外チェック
    assert(nextIndex_ < kMaxSRVCount);

    // ヒープの先頭ハンドルを取得
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

    // 次に使う場所へずらす
    cpuHandle.ptr += (descriptorSize_ * nextIndex_);

    // SRV生成
    device_->CreateShaderResourceView(resource, &srvDesc, cpuHandle);

    // インデックスを返して、カウンタを進める
    return nextIndex_++;
}
// グラフィックスパイプライン用に、指定SRVをルートディスクリプタテーブルへ設定する。

void SRVManager::SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, uint32_t srvHandle) {
    // 指定されたハンドルのGPUアドレスを計算してセット
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
    gpuHandle.ptr += (descriptorSize_ * srvHandle);
    commandList->SetGraphicsRootDescriptorTable(rootParameterIndex, gpuHandle);
}
// コマンドリストへSRVディスクリプタヒープを設定し、シェーダーから参照可能にする。

void SRVManager::SetDescriptorHeaps(ID3D12GraphicsCommandList* commandList) {
    assert(srvDescriptorHeap_);

    // ヒープをコマンドリストに登録
    ID3D12DescriptorHeap* pHeaps[] = { srvDescriptorHeap_.Get() };
    commandList->SetDescriptorHeaps(1, pHeaps);
}
// SRVスロットだけを先に確保し、後からリソースを差し替えられるようにする。

uint32_t SRVManager::Allocate() {
    std::lock_guard<std::mutex> lock(mutex_);

    // 上限チェック
    assert(nextIndex_ < kMaxSRVCount);

    // 現在のインデックスを確保して返す
    uint32_t index = nextIndex_;
    nextIndex_++;
    return index;
}

//  CreateSRVforResource
// 既に確保済みのSRVスロットへ、指定リソースのSRVを作成し直す。
void SRVManager::CreateSRVforResource(uint32_t index, ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc) {
    std::lock_guard<std::mutex> lock(mutex_);

    // ヒープの先頭ハンドルを取得
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

    // 指定された index の場所までアドレスをずらす
    cpuHandle.ptr += (static_cast<unsigned long long>(descriptorSize_) * index);

    // そこで SRV を作成する
    device_->CreateShaderResourceView(pResource, &srvDesc, cpuHandle);
}
// SRVハンドル番号からGPU側ディスクリプタハンドルを計算して返す。

D3D12_GPU_DESCRIPTOR_HANDLE SRVManager::GetGPUDescriptorHandle(uint32_t index) {
    // 範囲外チェック (任意ですがあると安全)
    if (index >= kMaxSRVCount) {
        assert(false && "SRV Index out of range!");
    }

    // 先頭ハンドルを取得
    D3D12_GPU_DESCRIPTOR_HANDLE handle = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();

    // インデックス分だけアドレスをずらす
    handle.ptr += (static_cast<UINT64>(descriptorSize_) * index);

    return handle;
}
// コンピュートパイプライン用に、指定SRVをルートディスクリプタテーブルへ設定する。

void SRVManager::SetComputeRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, uint32_t srvHandle) {
    // 指定されたハンドルのGPUアドレスを計算して、Compute用のルートパラメータにセット
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = GetGPUDescriptorHandle(srvHandle);
    commandList->SetComputeRootDescriptorTable(rootParameterIndex, gpuHandle);
}
// SRVハンドル番号からCPU側ディスクリプタハンドルを計算して返す。

D3D12_CPU_DESCRIPTOR_HANDLE SRVManager::GetCPUDescriptorHandle(uint32_t index) {
    // 範囲外チェック
    if (index >= kMaxSRVCount) {
        assert(false && "SRV Index out of range!");
    }

    // 先頭ハンドルを取得
    D3D12_CPU_DESCRIPTOR_HANDLE handle = srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

    // インデックス分だけアドレスをずらす
    handle.ptr += (static_cast<SIZE_T>(descriptorSize_) * index);

    return handle;
}
