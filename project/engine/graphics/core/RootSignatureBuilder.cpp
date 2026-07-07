#include "RootSignatureBuilder.h"
#include <cassert>

// =================================================================================
// 1. 直接指定系 (CBV / SRV / UAV)
// =================================================================================
// ルートシグネチャへ直接参照型のCBVパラメータを追加する。

void RootSignatureBuilder::AddCBV(uint32_t shaderRegister, uint32_t registerSpace, D3D12_SHADER_VISIBILITY visibility) {
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.Descriptor.ShaderRegister = shaderRegister;
    param.Descriptor.RegisterSpace = registerSpace;
    param.ShaderVisibility = visibility;
    parameters_.push_back(param);
}
// ルートシグネチャへ直接参照型のSRVパラメータを追加する。

void RootSignatureBuilder::AddSRV(uint32_t shaderRegister, uint32_t registerSpace, D3D12_SHADER_VISIBILITY visibility) {
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    param.Descriptor.ShaderRegister = shaderRegister;
    param.Descriptor.RegisterSpace = registerSpace;
    param.ShaderVisibility = visibility;
    parameters_.push_back(param);
}
// ルートシグネチャへ直接参照型のUAVパラメータを追加する。

void RootSignatureBuilder::AddUAV(uint32_t shaderRegister, uint32_t registerSpace, D3D12_SHADER_VISIBILITY visibility) {
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    param.Descriptor.ShaderRegister = shaderRegister;
    param.Descriptor.RegisterSpace = registerSpace;
    param.ShaderVisibility = visibility;
    parameters_.push_back(param);
}

// =================================================================================
// 2. テーブル系 (Descriptor Table)
// =================================================================================
// ディスクリプタテーブル用の範囲情報を作成する。

D3D12_DESCRIPTOR_RANGE RootSignatureBuilder::MakeRange(D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t shaderRegister, uint32_t numDescriptors, uint32_t registerSpace) {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = type;
    range.NumDescriptors = numDescriptors;
    range.BaseShaderRegister = shaderRegister;
    range.RegisterSpace = registerSpace;
    // テーブル内の連続したメモリに配置されるよう自動設定
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    return range;
}
// 複数のディスクリプタ範囲をまとめたテーブルをルートパラメータとして追加する。

void RootSignatureBuilder::AddDescriptorTable(const std::vector<D3D12_DESCRIPTOR_RANGE>& ranges, D3D12_SHADER_VISIBILITY visibility) {
    // 構築完了までメモリポインタが破棄されないよう、unique_ptr(ヒープ領域)にベクターごと退避させる
    auto rangeCopy = std::make_unique<std::vector<D3D12_DESCRIPTOR_RANGE>>(ranges);

    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(rangeCopy->size());
    param.DescriptorTable.pDescriptorRanges = rangeCopy->data(); // ポインタを渡す
    param.ShaderVisibility = visibility;

    tableRanges_.push_back(std::move(rangeCopy)); // メモリの所有権を保持リストへ移動
    parameters_.push_back(param);
}
// 単一範囲だけのディスクリプタテーブルを簡単に追加する。

void RootSignatureBuilder::AddSimpleDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE rangeType, uint32_t shaderRegister, uint32_t numDescriptors, uint32_t registerSpace, D3D12_SHADER_VISIBILITY visibility) {
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
    ranges.push_back(MakeRange(rangeType, shaderRegister, numDescriptors, registerSpace));
    AddDescriptorTable(ranges, visibility);
}

// =================================================================================
// 3. サンプラー系 (Static Sampler)
// =================================================================================
// よく使う設定から静的サンプラーを作成して追加する。

void RootSignatureBuilder::AddStaticSampler(uint32_t shaderRegister, uint32_t registerSpace, D3D12_SHADER_VISIBILITY visibility, D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE addressMode) {
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = filter;
    sampler.AddressU = addressMode;
    sampler.AddressV = addressMode;
    sampler.AddressW = addressMode;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = shaderRegister;
    sampler.RegisterSpace = registerSpace;
    sampler.ShaderVisibility = visibility;

    samplers_.push_back(sampler);
}
// 呼び出し側で作成した静的サンプラー設定をそのまま追加する。

void RootSignatureBuilder::AddStaticSamplerCustom(const D3D12_STATIC_SAMPLER_DESC& samplerDesc) {
    samplers_.push_back(samplerDesc);
}

// =================================================================================
// 4. ビルド実行 & 初期化
// =================================================================================
// 登録済みパラメータとサンプラーからルートシグネチャをシリアライズして作成する。

void RootSignatureBuilder::Build(ID3D12Device* device, ID3D12RootSignature** outRootSignature, D3D12_ROOT_SIGNATURE_FLAGS flags) {
    assert(device != nullptr);
    assert(outRootSignature != nullptr);

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = static_cast<UINT>(parameters_.size());
    rsDesc.pParameters = parameters_.empty() ? nullptr : parameters_.data();
    rsDesc.NumStaticSamplers = static_cast<UINT>(samplers_.size());
    rsDesc.pStaticSamplers = samplers_.empty() ? nullptr : samplers_.data();
    rsDesc.Flags = flags;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    // バージョン 1.0 でシリアライズ
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &signatureBlob, &errorBlob);

    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
        }
        assert(false && "Failed to serialize root signature!");
    }

    hr = device->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(outRootSignature)
    );
    assert(SUCCEEDED(hr) && "Failed to create root signature from blob!");
}
// Builderに保持しているパラメータ、サンプラー、テーブル範囲を初期化する。

void RootSignatureBuilder::Clear() {
    parameters_.clear();
    samplers_.clear();
    tableRanges_.clear(); 
}