#pragma once

#include <d3d12.h>
#include <string>
#include <vector>
#include <map>
#include <wrl.h>
#include "DirectXCommon.h"

class TextureManager {
public:
    struct TextureData {
        std::string filePath;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU;
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU;
    };

public: // �V���O���g��
    static TextureManager* GetInstance();
    static void DestroyInstance();

private:
    TextureManager() = default;
    ~TextureManager() = default;
    TextureManager(const TextureManager&) = delete;
    const TextureManager& operator=(const TextureManager&) = delete;
    static TextureManager* instance_;

public: // �����o�֐�
    void Initialize(DirectXCommon* dxCommon);
    uint32_t Load(const std::string& filePath);
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t textureHandle);

private:
    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
    uint32_t descriptorSizeSRV_ = 0;

    std::vector<TextureData> textureDatas_;
    std::map<std::string, uint32_t> textureHandleMap_;

    // SRV�C���f�b�N�X�̊J�n�ԍ��iImGui��0�Ԃ��g���Ă��邽�߁j
    static const uint32_t kSRVIndexTop = 1;
};