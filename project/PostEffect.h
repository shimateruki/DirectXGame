#pragma once
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <wrl.h>

// 画面全体を覆う板ポリゴンを描画し、ポストエフェクトをかけるクラス
class PostEffect {
public:
    struct Params {
        float threshold = 0.8f;
        float bloomIntensity = 2.0f;
        float spread = 2.0f;
        float padding; // 16バイト境界合わせ
    };
    void Initialize(DirectXCommon* dxCommon);

    // コマンドリストと、貼り付ける画像のハンドル(SRV)を渡して描画
    void Draw(ID3D12GraphicsCommandList* commandList, uint32_t srvHandle);
    // 描画先を「テクスチャB」に切り替える関数
    void PreDrawScene(ID3D12GraphicsCommandList* commandList);
    // テクスチャBのハンドルをGameViewに渡す用
    uint32_t GetSRVHandle() const { return srvHandle_; }
    ID3D12Resource* GetRenderTexture() const { return renderTexture_.Get(); }
    Params* GetParams() { return paramsData_; }
private:
    void CreateMesh();
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateRenderTexture();
    void CreateConstBuffer();



private:
    // テクスチャB用のリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> renderTexture_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    uint32_t srvHandle_ = 0; // GameViewに渡す用
    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;
    Params* paramsData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // 板ポリゴン用頂点データ
    struct VertexPosUv {
        Vector3 pos;
        Vector2 uv;
    };
};