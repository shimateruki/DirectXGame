#pragma once

#include <cstdint>
#include <d3d12.h>
#include <mutex>
#include <wrl.h>

class DirectXCommon;

/// <summary>シェーダー可視の SRV ヒープと、ゲーム用スロットの払い出しを管理します。</summary>
class SRVManager {
public:
    // 先頭の 2 スロットは ImGui が使用します。
    static const size_t kMaxSRVCount = 2018;

    /// <summary>エンジンで共有する管理インスタンスを取得します。</summary>
    static SRVManager* GetInstance();

    /// <summary>DirectX デバイスから SRV ヒープを作成し、払い出し位置を初期化します。</summary>
    /// <param name="dxCommon">初期化済みの DirectX 管理オブジェクト。</param>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>次のゲーム用スロットに SRV を作成します。</summary>
    /// <param name="resource">参照対象の GPU リソース。</param>
    /// <param name="srvDesc">作成する SRV の記述。</param>
    /// <returns>作成した SRV のヒープ内インデックス。</returns>
    uint32_t CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);

    /// <summary>管理中のディスクリプタヒープを取得します。</summary>
    ID3D12DescriptorHeap* GetDescriptorHeap() const { return srvDescriptorHeap_.Get(); }

    /// <summary>指定した SRV を描画用ルートパラメータへ設定します。</summary>
    /// <param name="commandList">設定先のコマンドリスト。</param>
    /// <param name="rootParameterIndex">ディスクリプタテーブルのルートパラメータ番号。</param>
    /// <param name="srvHandle">SRV のヒープ内インデックス。</param>
    void SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, uint32_t srvHandle);

    /// <summary>管理中のシェーダー可視ヒープをコマンドリストに設定します。</summary>
    /// <param name="commandList">設定先のコマンドリスト。</param>
    void SetDescriptorHeaps(ID3D12GraphicsCommandList* commandList);

    /// <summary>確保済みスロットに SRV を作成または上書きします。</summary>
    /// <param name="index">事前に確保したヒープ内インデックス。</param>
    /// <param name="pResource">参照対象の GPU リソース。</param>
    /// <param name="srvDesc">作成する SRV の記述。</param>
    void CreateSRVforResource(uint32_t index, ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);

    /// <summary>後から SRV を作成できるよう、ゲーム用スロットだけを確保します。</summary>
    /// <returns>確保したヒープ内インデックス。</returns>
    uint32_t Allocate();

    /// <summary>SRV インデックスに対応する GPU ハンドルを取得します。</summary>
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);

    /// <summary>指定した SRV をコンピュート用ルートパラメータへ設定します。</summary>
    /// <param name="commandList">設定先のコマンドリスト。</param>
    /// <param name="rootParameterIndex">ディスクリプタテーブルのルートパラメータ番号。</param>
    /// <param name="srvHandle">SRV のヒープ内インデックス。</param>
    void SetComputeRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, uint32_t srvHandle);

    /// <summary>SRV インデックスに対応する CPU ハンドルを取得します。</summary>
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);

private:
    SRVManager() = default;
    ~SRVManager() = default;
    SRVManager(const SRVManager&) = delete;
    SRVManager& operator=(const SRVManager&) = delete;

    Microsoft::WRL::ComPtr<ID3D12Device> device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_ = nullptr;
    uint32_t descriptorSize_ = 0; // デバイスから取得した 1 スロットのバイト数。
    mutable std::mutex mutex_;    // 並列のスロット確保と SRV 作成を直列化します。
    uint32_t nextIndex_ = 2;       // 0、1 は ImGui 用です。
};
