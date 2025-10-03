#include "TextureManager.h"
#include "externals/DirectXTex/d3dx12.h" // UpdateSubresourcesを使うため
#include <cassert>

TextureManager* TextureManager::GetInstance() {
	static TextureManager instance;
	return &instance;
}

void TextureManager::Initialize(DirectXCommon* dxCommon) {
	assert(dxCommon);
	dxCommon_ = dxCommon;
	textureDatas_.reserve(DirectXCommon::kMaxSRVCount);
}

uint32_t TextureManager::Load(const std::string& filePath) {
	// 読み込み済みテクスチャを検索
	for (size_t i = 0; i < textureDatas_.size(); ++i) {
		if (textureDatas_[i].filePath == filePath) {
			return static_cast<uint32_t>(i + kSRVIndexTop); // インデックス+開始番号を返す
		}
	}

	// 最大数を超えないかチェック
	assert(textureDatas_.size() + kSRVIndexTop < DirectXCommon::kMaxSRVCount);

	// 新しいテクスチャデータをコンテナに追加
	textureDatas_.resize(textureDatas_.size() + 1);
	TextureData& textureData = textureDatas_.back();
	textureData.filePath = filePath;

	// ファイル読み込みとミップマップ生成
	DirectX::ScratchImage mipImages{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, mipImages);
	assert(SUCCEEDED(hr));
	if (FAILED(hr)) {
		// 読み込み失敗時に必ずここで止まるようにする
		assert(false && "Failed to load texture file. Check the file path.");
	}
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();


	// テクスチャリソース作成
	textureData.resource = dxCommon_->CreateTextureResource(metadata);
	if (!textureData.resource) {
		// リソース作成がnullptrを返した場合に必ずここで止まるようにする
		assert(false && "dxCommon->CreateTextureResource returned nullptr. Check texture data.");
	}
	// 中間リソースを使ってテクスチャデータをGPUにアップロード
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(dxCommon_->GetDevice(), mipImages.GetImages(), mipImages.GetImageCount(), metadata, subresources);
	uint64_t intermediateSize = GetRequiredIntermediateSize(textureData.resource.Get(), 0, UINT(subresources.size()));
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = dxCommon_->CreateBufferResource(intermediateSize);
	UpdateSubresources(dxCommon_->GetCommandList(), textureData.resource.Get(), intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data());

	// バリアを張って状態を遷移
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = textureData.resource.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	dxCommon_->GetCommandList()->ResourceBarrier(1, &barrier);

	// SRVインデックスを計算
	uint32_t srvIndex = static_cast<uint32_t>(textureDatas_.size() - 1) + kSRVIndexTop;

	// ハンドルを取得
	const auto& srvHeap = dxCommon_->GetSrvDescriptorHeap();
	const auto descriptorSize = dxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	textureData.srvHandleCPU.ptr = srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + (descriptorSize * srvIndex);
	textureData.srvHandleGPU.ptr = srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + (descriptorSize * srvIndex);

	// SRVデスクリプション
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);

	// SRVを作成
	dxCommon_->GetDevice()->CreateShaderResourceView(textureData.resource.Get(), &srvDesc, textureData.srvHandleCPU);

	return srvIndex;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(uint32_t textureIndex) {
	// ImGuiの分を考慮して、実際のインデックスに変換
	uint32_t actualIndex = textureIndex - kSRVIndexTop;
	assert(actualIndex < textureDatas_.size());
	return textureDatas_[actualIndex].srvHandleGPU;
}

std::wstring TextureManager::ConvertString(const std::string& str) {
	if (str.empty()) { return std::wstring(); }
	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) { return std::wstring(); }
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}