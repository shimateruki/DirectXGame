#pragma once
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;

// SRV デスクリプタヒープの確保、作成、バインドをまとめる管理クラス
// SRVManagerは、シェーダーから参照するテクスチャやバッファ用DescriptorHeapを管理します。
class SRVManager {
public:
    // ImGui が先頭の一部を使うため、ゲーム側で使える十分な数を用意する。
    static const size_t kMaxSRVCount = 2018;

public:
        // エンジン全体で共有するSRV管理インスタンスを取得します。
static SRVManager* GetInstance();

        // SRV用DescriptorHeapを作成し、割り当てを開始できる状態にします。
void Initialize(DirectXCommon* dxCommon);

    // 空きインデックスを確保して SRV を作成する
        // 新しいSRV番号を確保し、指定リソース用Descriptorを作成します。
uint32_t CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);

    ID3D12DescriptorHeap* GetDescriptorHeap() const { return srvDescriptorHeap_.Get(); }

    // Graphics 用のルートパラメータへ SRV をセットする
        // グラフィックスパイプラインへ指定SRVをバインドします。
void SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, uint32_t srvHandle);

    void SetDescriptorHeaps(ID3D12GraphicsCommandList* commandList);

    // 確保済みのインデックスを指定して SRV を作成する
    void CreateSRVforResource(uint32_t index, ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);
        // 未使用のSRVインデックスを1つ払い出します。
uint32_t Allocate();
        // SRVインデックスに対応するGPU Descriptor Handleを取得します。
D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);

    // Compute Shader 用のルートパラメータへ SRV をセットする
    void SetComputeRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, uint32_t srvHandle);

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);

private:
    SRVManager() = default;
    ~SRVManager() = default;
    SRVManager(const SRVManager&) = delete;
    SRVManager& operator=(const SRVManager&) = delete;

private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_ = nullptr;

    uint32_t descriptorSize_ = 0;

    // 0,1 は ImGui が使うため、ゲーム用 SRV は 2 番から確保する。
    uint32_t nextIndex_ = 2;
};
