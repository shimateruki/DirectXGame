#include "TextureManager.h"
#include <cassert>
#include "externals/DirectXTex/d3dx12.h" // UpdateSubresourcesなどのヘルパー関数を使用するため

/// <summary>
/// テクスチャデータをGPUにアップロードするためのヘルパー関数
/// </summary>
/// <param name="texture">アップロード先のテクスチャリソース（DEFAULTヒープ）</param>
/// <param name="mipImages">CPU上のテクスチャデータ（ミップマップ含む）</param>
/// <param name="intermediateResource">アップロード用の中間リソース（UPLOADヒープ）</param>
/// <param name="device">D3D12デバイス</param>
/// <param name="commandList">コマンドリスト</param>
void UploadTextureData(
    ID3D12Resource* texture,
    const DirectX::ScratchImage& mipImages,
    ID3D12Resource** intermediateResource,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList)
{
    // アップロードに必要なサブリソースの情報を準備
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    HRESULT hr = DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
    assert(SUCCEEDED(hr));

    // 中間リソースに必要なサイズを計算
    uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));

    // --- 中間リソースの作成 ---
    // ヒープの設定（UPLOADヒープ：CPUから書き込み可能）
    D3D12_HEAP_PROPERTIES uploadHeapProperties{};
    uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    // リソースの設定（バッファリソース）
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = intermediateSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // 中間リソースを実際に作成
    hr = device->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, // アップロードヒープなので初期状態はGENERIC_READ
        nullptr,
        IID_PPV_ARGS(intermediateResource));
    assert(SUCCEEDED(hr));

    // --- データの転送 ---
    // CPU上のテクスチャデータを中間リソースにコピーし、
    // 中間リソースから最終的なテクスチャリソースへコピーするコマンドをコマンドリストに積む
    UpdateSubresources(commandList, texture, *intermediateResource, 0, 0, UINT(subresources.size()), subresources.data());

    // --- リソースバリア ---
    // テクスチャリソースの状態をコピー先からシェーダーでの読み取り可能状態へ遷移させる
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; // 遷移前の状態：コピー先
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ; // 遷移後の状態：汎用読み取り
    // リソースバリアのコマンドをコマンドリストに積む
    commandList->ResourceBarrier(1, &barrier);
}


// --- シングルトンインスタンスの実体 ---
TextureManager* TextureManager::instance_ = nullptr;

// インスタンスを取得する静的メソッド
TextureManager* TextureManager::GetInstance() {
    if (instance_ == nullptr) {
        instance_ = new TextureManager();
    }
    return instance_;
}

// インスタンスを破棄する静的メソッド
void TextureManager::DestroyInstance() {
    delete instance_;
    instance_ = nullptr;
}

// 初期化処理
void TextureManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    device_ = dxCommon_->GetDevice();
    srvDescriptorHeap_ = dxCommon_->GetSrvDescriptorHeap();
    // SRVデスクリプタ1つ分のサイズを取得
    descriptorSizeSRV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // テクスチャデータの配列のメモリをあらかじめ確保しておく（パフォーマンスのため）
    textureDatas_.reserve(DirectXCommon::kMaxSRVCount);
}

// テクスチャをファイルから読み込む
uint32_t TextureManager::Load(const std::string& filePath) {
    // 1. 多重読み込みチェック
    // すでに読み込み済みのテクスチャか検索
    auto it = textureHandleMap_.find(filePath);
    if (it != textureHandleMap_.end()) {
        // 読み込み済みなら、そのハンドルを返す
        return it->second;
    }

    // 2. 新規読み込み
    // 資料の「最大数チェック」を反映 
    // SRVの最大数を超えないかチェック
    assert(textureDatas_.size() + kSRVIndexTop < DirectXCommon::kMaxSRVCount);

    // ハンドルを計算（vectorの現在の要素数がそのまま次のインデックスになる）
    // kSRVIndexTopでオフセットをかける (0番はImGuiなどが使うため)
    uint32_t handle = static_cast<uint32_t>(textureDatas_.size()) + kSRVIndexTop;

    // 新しいテクスチャデータを格納する領域を確保
    textureDatas_.emplace_back();
    TextureData& newData = textureDatas_.back();
    newData.filePath = filePath;

    // テクスチャファイルを読み込み、ミップマップを生成
    DirectX::ScratchImage mipImages = dxCommon_->LoadTexture(filePath);
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    newData.metadata = metadata; // 読み込んだメタデータを保存

    // GPU上にテクスチャリソースを作成
    newData.resource = dxCommon_->CreateTextureResource(metadata);
    // 作成したリソースに、読み込んだテクスチャデータをアップロード
    UploadTextureData(
        newData.resource.Get(), mipImages, &newData.intermediateResource,
        device_.Get(), dxCommon_->GetCommandList());

    // 3. SRV (Shader Resource View) の作成
    // ★★★ 資料の「SRVインデックス計算」を反映 ★★★
    uint32_t srvIndex = handle; // ハンドルがそのままSRVインデックスになる
    // SRVヒープの先頭ハンドルを取得
    newData.srvHandleCPU = srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    // ハンドル（インデックス）分オフセットをずらす
    newData.srvHandleCPU.ptr += (descriptorSizeSRV_ * srvIndex);
    // GPU側も同様
    newData.srvHandleGPU = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
    newData.srvHandleGPU.ptr += (descriptorSizeSRV_ * srvIndex);

    // SRV作成デスクリプタ（設定情報）
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format; // フォーマット
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; // 標準的なマッピング
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; // 2Dテクスチャとして設定
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels); // ミップマップレベル数
    // SRVをデスクリプタヒープ上に作成
    device_->CreateShaderResourceView(newData.resource.Get(), &srvDesc, newData.srvHandleCPU);

    // ファイルパスとハンドルの対応をマップに記録
    textureHandleMap_[filePath] = handle;

    // ハンドルを返す
    return handle;
}

// GPUデスクリプタハンドルを取得
D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetGPUHandle(uint32_t textureHandle) {
    // ハンドルが有効範囲内かチェック
    assert(textureHandle >= kSRVIndexTop && textureHandle < textureDatas_.size() + kSRVIndexTop);
    // ハンドルは1から始まるので、vectorのインデックスに合わせるために -kSRVIndexTop
    return textureDatas_[textureHandle - kSRVIndexTop].srvHandleGPU;
}

// テクスチャのメタデータを取得
const DirectX::TexMetadata& TextureManager::GetMetadata(uint32_t textureHandle) {
    // ハンドルが有効範囲内かチェック
    assert(textureHandle >= kSRVIndexTop && textureHandle < textureDatas_.size() + kSRVIndexTop);
    // ハンドルからインデックスを計算してメタデータを返す
    return textureDatas_[textureHandle - kSRVIndexTop].metadata;
}