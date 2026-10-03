#pragma once

#include <d3d12.h>
#include <memory>
#include <vector>
#include <wrl.h>

/// <summary>ルートパラメータと静的サンプラーを積み上げてルートシグネチャを作成します。</summary>
class RootSignatureBuilder {
public:
    RootSignatureBuilder() = default;
    ~RootSignatureBuilder() = default;

    /// <summary>指定レジスタを直接参照する CBV を追加します。</summary>
    /// <param name="shaderRegister">CBV の b レジスタ番号。</param>
    /// <param name="registerSpace">レジスタ空間番号。</param>
    /// <param name="visibility">参照できるシェーダーステージ。</param>
    void AddCBV(uint32_t shaderRegister, uint32_t registerSpace = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    /// <summary>指定レジスタを直接参照する SRV を追加します。</summary>
    /// <param name="shaderRegister">SRV の t レジスタ番号。</param>
    /// <param name="registerSpace">レジスタ空間番号。</param>
    /// <param name="visibility">参照できるシェーダーステージ。</param>
    void AddSRV(uint32_t shaderRegister, uint32_t registerSpace = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    /// <summary>指定レジスタを直接参照する UAV を追加します。</summary>
    /// <param name="shaderRegister">UAV の u レジスタ番号。</param>
    /// <param name="registerSpace">レジスタ空間番号。</param>
    /// <param name="visibility">参照できるシェーダーステージ。</param>
    void AddUAV(uint32_t shaderRegister, uint32_t registerSpace = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    /// <summary>32 ビット値をルート定数として追加します。</summary>
    /// <param name="shaderRegister">定数の b レジスタ番号。</param>
    /// <param name="valueCount">32 ビット値の個数。0 は指定できません。</param>
    /// <param name="registerSpace">レジスタ空間番号。</param>
    /// <param name="visibility">参照できるシェーダーステージ。</param>
    void AddConstants(uint32_t shaderRegister, uint32_t valueCount, uint32_t registerSpace = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    /// <summary>ディスクリプタテーブルに追加する連続範囲を作ります。</summary>
    /// <param name="type">CBV、SRV、UAV、Sampler のいずれか。</param>
    /// <param name="shaderRegister">範囲の先頭レジスタ番号。</param>
    /// <param name="numDescriptors">範囲内のディスクリプタ数。</param>
    /// <param name="registerSpace">レジスタ空間番号。</param>
    /// <returns>直前の範囲に続けて配置する設定済み範囲。</returns>
    static D3D12_DESCRIPTOR_RANGE MakeRange(D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t shaderRegister, uint32_t numDescriptors = 1, uint32_t registerSpace = 0);

    /// <summary>複数の範囲を持つディスクリプタテーブルを追加します。</summary>
    /// <param name="ranges">コピーして保持する範囲の列。</param>
    /// <param name="visibility">参照できるシェーダーステージ。</param>
    void AddDescriptorTable(const std::vector<D3D12_DESCRIPTOR_RANGE>& ranges, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    /// <summary>単一範囲のディスクリプタテーブルを追加します。</summary>
    /// <param name="rangeType">範囲の種類。</param>
    /// <param name="shaderRegister">範囲の先頭レジスタ番号。</param>
    /// <param name="numDescriptors">範囲内のディスクリプタ数。</param>
    /// <param name="registerSpace">レジスタ空間番号。</param>
    /// <param name="visibility">参照できるシェーダーステージ。</param>
    void AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE rangeType, uint32_t shaderRegister, uint32_t numDescriptors = 1, uint32_t registerSpace = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    /// <summary>指定したフィルターとアドレス方式の静的サンプラーを追加します。</summary>
    /// <param name="shaderRegister">Sampler の s レジスタ番号。</param>
    /// <param name="registerSpace">レジスタ空間番号。</param>
    /// <param name="visibility">参照できるシェーダーステージ。</param>
    /// <param name="filter">テクスチャの補間方式。</param>
    /// <param name="addressMode">U、V、W に共通のアドレス方式。</param>
    void AddStaticSampler(uint32_t shaderRegister, uint32_t registerSpace = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
        D3D12_FILTER filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE addressMode = D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    /// <summary>呼び出し側で組み立てた静的サンプラーを追加します。</summary>
    /// <param name="samplerDesc">追加するサンプラーの記述。</param>
    void AddStaticSamplerCustom(const D3D12_STATIC_SAMPLER_DESC& samplerDesc);

    /// <summary>現在の設定をシリアライズし、D3D12 ルートシグネチャを作成します。</summary>
    /// <param name="device">作成に使用するデバイス。</param>
    /// <param name="outRootSignature">作成したルートシグネチャの受け取り先。</param>
    /// <param name="flags">ルートシグネチャの使用条件。</param>
    void Build(ID3D12Device* device, ID3D12RootSignature** outRootSignature, D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    /// <summary>登録済みのパラメータ、サンプラー、範囲をすべて破棄します。</summary>
    void Clear();

private:
    std::vector<D3D12_ROOT_PARAMETER> parameters_;
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers_;

    // DescriptorTable は範囲への生ポインタを持つため、Build までこの所有領域を保持します。
    std::vector<std::unique_ptr<std::vector<D3D12_DESCRIPTOR_RANGE>>> tableRanges_;
};
