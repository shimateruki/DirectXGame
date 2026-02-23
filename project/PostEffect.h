#pragma once
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <wrl.h>
#include <vector>

class PostEffect {
public:
    struct Params {
        float threshold = 1.5f;      
        float bloomIntensity = 2.0f;
        float spread = 2.0f;
        int32_t enableToneMapping = 1; 
        float vignetteIntensity = 1.0f;    // 周辺減光の強さ
        float chromaticAberration = 0.02f; // 色収差のズレ幅
        float filmGrainIntensity = 0.03f;  // ノイズの強さ
        float time = 0.0f;                 // ノイズを毎フレーム動かすための時間
        float radialCenterX = 0.5f;   // ぼかしの中心X (0.0 ～ 1.0)
        float radialCenterY = 0.5f;   // ぼかしの中心Y (0.0 ～ 1.0)
        float radialIntensity = 0.0f; // ぼかしの強さ (0.0でオフ)
        float radialPadding = 0.0f;   // 16バイト合わせの詰め物
        float lutIntensity = 0.0f;
        float damageFlash = 0.0f;     // ダメージ時の赤画面 (0.0 ～ 1.0)
        float cinemaBarHeight = 0.0f; // 上下の黒帯の太さ (0.0 ～ 0.5)
        float wobbleIntensity = 0.0f; // 画面の波打ちの強さ (0.0 でオフ)
        float scanlineIntensity = 0.0f; // ブラウン管の横縞の強さ (0.0 ～ 1.0)
        float mosaicSize = 0.0f;        // モザイクの粗さ (1.0以下でオフ、大きいほど粗い)
        float padding1 = 0.0f;          // 16バイト合わせの詰め物
        float padding2 = 0.0f;
        float padding3 = 0.0f;

    };

    void Initialize(DirectXCommon* dxCommon);

    //  どのPSO（シェーダー）を使って、どのテクスチャを、どこに描くか指定できるように変更
    void Draw(ID3D12GraphicsCommandList* commandList, uint32_t srvHandle, int psoIndex = 0);

    void PreDrawScene(ID3D12GraphicsCommandList* commandList, int targetTexIndex = 0, bool clear = true);

    // ★ テクスチャのリソースバリア（RTV <-> SRV）を張る関数を追加
    void TransitionToSRV(ID3D12GraphicsCommandList* commandList, int texIndex);
    void TransitionToRTV(ID3D12GraphicsCommandList* commandList, int texIndex);

    // ★ 任意のテクスチャのSRVを取得できるように変更
    uint32_t GetSRVHandle(int texIndex = 0) const { return renderTextures_[texIndex].srvHandle; }
    ID3D12Resource* GetRenderTexture(int texIndex = 0) const { return renderTextures_[texIndex].resource.Get(); }
    Params* GetParams() { return paramsData_; }
    void SetLUTTexture(uint32_t srvHandle) { lutSrvHandle_ = srvHandle; }
private:
    void CreateMesh();
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateRenderTexture(int texIndex, int width, int height, DXGI_FORMAT format);
    void CreateConstBuffer();

private:
    // ==========================================
    // ★ レンダーテクスチャ管理用構造体
    // ==========================================
    struct RenderTexture {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
        uint32_t srvHandle = 0;
        D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    };

    // ★ 複数のテクスチャを持てるように配列（ベクター）にする
    std::vector<RenderTexture> renderTextures_;

    // ★ 複数のパイプライン（シェーダー）を持てるように配列にする
    std::vector<Microsoft::WRL::ComPtr<ID3D12PipelineState>> pipelineStates_;

    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;
    Params* paramsData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    struct VertexPosUv {
        Vector3 pos;
        Vector2 uv;
    };

    uint32_t lutSrvHandle_ = 0; 
};