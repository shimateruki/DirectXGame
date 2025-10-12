#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <vector>

class DirectXCommon;

/// <summary>
/// SRV(シェーダーリソースビュー)を管理するクラス
/// </summary>
class SRVManager {
public:
    // デスクリプタの最大数
    static const size_t kMaxSRVCount = 256;

public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static SRVManager* GetInstance();

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// SRVを作成する
    /// </summary>
    /// <param name="resource">SRVを作成するリソース</param>
    /// <param name="srvDesc">SRVの設定デスクリプション</param>
    /// <returns>作成されたSRVのハンドル(インデックス)</returns>
    uint32_t CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);

    /// <summary>
    /// GPUデスクリプタハンドルを取得
    /// </summary>
    /// <param name="srvHandle">SRVハンドル</param>
    /// <returns>GPUデスクリプタハンドル</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t srvHandle);

    /// <summary>
    /// デスクリプタテーブルをコマンドリストに設定する
    /// </summary>
    /// <param name="commandList">コマンドリスト</param>
    /// <param name="rootParameterIndex">ルートパラメータのインデックス</param>
    /// <param name="srvHandle">SRVハンドル</param>
    void SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, uint32_t srvHandle);

    /// <summary>
/// SRV用デスクリプタヒープを取得する
/// </summary>
/// <returns>SRV用デスクリプタヒープ</returns>
    ID3D12DescriptorHeap* GetDescriptorHeap() const { return srvDescriptorHeap_.Get(); }


private:
    SRVManager() = default;
    ~SRVManager() = default;
    SRVManager(const SRVManager&) = delete;
    SRVManager& operator=(const SRVManager&) = delete;

private:
    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_ = nullptr;
    uint32_t descriptorSize_ = 0;
    uint32_t nextIndex_ = 0; // 次に割り当てるSRVのインデックス
};