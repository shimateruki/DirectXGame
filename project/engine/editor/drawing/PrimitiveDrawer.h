#pragma once
#include <d3d12.h>
#include <wrl.h>
#include "engine/utility/math/Math.h"

class DirectXCommon;

// 定数バッファの一スロットを 256 バイトにするための余白付き行列。要素を並べても CBV の間隔を保つ。
// 関連：PrimitiveDrawer（この型を使う機能の本体）
struct AlignedMatrix4x4 {
    engine::Matrix4x4 matrix;

    char padding[256 - sizeof(engine::Matrix4x4)];
};

// 定数バッファの一スロットを 256 バイトにするための余白付き Vector4。
// 関連：PrimitiveDrawer（この型を使う機能の本体）
struct AlignedVector4 {
    engine::Vector4 vector;

    char padding[256 - sizeof(engine::Vector4)];
};

// 担当：Editor用の箱・球・円柱などをワイヤーフレームで描画する。
// 関連：DirectXCommon（GPU基盤）、Camera（視点）、各Editor（利用）
// 主な入口：Initialize / PreDraw / DrawWireCube / Finalize
class PrimitiveDrawer {
public:
    PrimitiveDrawer() = default;

    ~PrimitiveDrawer();

    // ワイヤー描画のパイプライン・頂点と定数バッファを準備する。
    void Initialize(DirectXCommon* dxCommon);

    // 描画用GPU資源と保持状態を解放する。
    void Finalize();

    // 描画開始前のパイプライン設定
    void PreDraw(ID3D12GraphicsCommandList* commandList);

    // キューブの描画
    // 各 DrawWire 関数で使う instanceIndex は 0～8191。同一描画内で異なる表示には別のスロットを使う。
    void DrawWireCube(ID3D12GraphicsCommandList* commandList, const engine::Matrix4x4& worldMatrix, const engine::Vector4& color, int instanceIndex);

    // 指定行列・色でワイヤー球を描く。instanceIndexは描画バッファの枠。
    void DrawWireSphere(ID3D12GraphicsCommandList* commandList, const engine::Matrix4x4& worldMatrix, const engine::Vector4& color, int instanceIndex);

    // 指定行列・色でワイヤー円柱を描く。instanceIndexは描画バッファの枠。
    void DrawWireCylinder(ID3D12GraphicsCommandList* commandList, const engine::Matrix4x4& worldMatrix, const engine::Vector4& color, int instanceIndex);

private:
    DirectXCommon* dxCommon_ = nullptr;
    static const int kMaxInstances = 8192;

    // --- プリミティブ描画リソース ---
    Microsoft::WRL::ComPtr<ID3D12RootSignature> primitiveRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> primitivePipelineState_;

    Microsoft::WRL::ComPtr<ID3D12Resource> cubeVertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW cubeVertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> cubeIndexBuffer_;
    D3D12_INDEX_BUFFER_VIEW cubeIndexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> primitiveWvpBuffer_;
    AlignedMatrix4x4* primitiveWvpData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> primitiveColorBuffer_;
    AlignedVector4* primitiveColorData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> sphereVertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW sphereVertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> sphereIndexBuffer_;
    D3D12_INDEX_BUFFER_VIEW sphereIndexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> cylinderVertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW cylinderVertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> cylinderIndexBuffer_;
    D3D12_INDEX_BUFFER_VIEW cylinderIndexBufferView_{};

};
