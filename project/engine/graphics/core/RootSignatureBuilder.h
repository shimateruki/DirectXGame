#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <memory>

// RootSignatureBuilderは、CBV/SRV/UAVやSamplerを追加しながらRootSignatureを作る補助クラスです。
class RootSignatureBuilder {
public:
    RootSignatureBuilder() = default;
    ~RootSignatureBuilder() = default;

    // =======================================================
    // Root Parameterを直接追加します。Register Spaceも明示できます。
    // =======================================================
        // 指定レジスタの定数バッファビューをRootParameterへ追加します。
void AddCBV(uint32_t shaderRegister, uint32_t registerSpace = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
        // 指定レジスタのシェーダーリソースビューをRootParameterへ追加します。
void AddSRV(uint32_t shaderRegister, uint32_t registerSpace = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
        // 指定レジスタの書き込み可能リソースビューをRootParameterへ追加します。
void AddUAV(uint32_t shaderRegister, uint32_t registerSpace = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
        // 少量の頻繁に変わる値を、定数バッファを作らずRoot Constantsとして追加します。
void AddConstants(uint32_t shaderRegister, uint32_t valueCount, uint32_t registerSpace = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    // =======================================================
    // 2. テーブル系 (汎用性と簡略化の両立)
    // =======================================================
    // Rangeを作るための便利静的関数
    static D3D12_DESCRIPTOR_RANGE MakeRange(D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t shaderRegister, uint32_t numDescriptors = 1, uint32_t registerSpace = 0);

    // [汎用版] 複数のRangeを持つ複雑なテーブルを追加する
        // 複数RangeをまとめたDescriptorTableを追加します。
void AddDescriptorTable(const std::vector<D3D12_DESCRIPTOR_RANGE>& ranges, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    // [簡易版] 「SRV1個だけ」などの簡単なテーブルを一発で追加する
    void AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE rangeType, uint32_t shaderRegister, uint32_t numDescriptors = 1, uint32_t registerSpace = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    // =======================================================
    // 3. サンプラー系
    // =======================================================
        // 標準的な静的SamplerをRootSignatureへ追加します。
void AddStaticSampler(uint32_t shaderRegister, uint32_t registerSpace = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
        D3D12_FILTER filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE addressMode = D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    void AddStaticSamplerCustom(const D3D12_STATIC_SAMPLER_DESC& samplerDesc);

    // =======================================================
    // 4. ビルド実行
    // =======================================================
        // 追加済みパラメータからD3D12 RootSignatureを生成します。
void Build(ID3D12Device* device, ID3D12RootSignature** outRootSignature, D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    void Clear();

private:
    std::vector<D3D12_ROOT_PARAMETER> parameters_;
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers_;

    // テーブルのRange情報をメモリ上に安全に保持するためのベクター
    std::vector<std::unique_ptr<std::vector<D3D12_DESCRIPTOR_RANGE>>> tableRanges_;
};
