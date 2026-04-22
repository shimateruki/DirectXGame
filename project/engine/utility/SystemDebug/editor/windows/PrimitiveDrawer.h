#pragma once
#include <d3d12.h>
#include <wrl.h>
#include "engine/utility/math/Math.h"

class DirectXCommon;

// DebugEditorからお引越しした構造体
struct AlignedMatrix4x4 {
    Matrix4x4 matrix;
    char padding[256 - sizeof(Matrix4x4)];
};

struct AlignedVector4 {
    Vector4 vector;
    char padding[256 - sizeof(Vector4)];
};

// ワイヤーフレームの描画を専門に行うクラス
class PrimitiveDrawer {
public:
    PrimitiveDrawer() = default;
    ~PrimitiveDrawer();

    void Initialize(DirectXCommon* dxCommon);
    void Finalize();

    // 描画開始前のパイプライン設定
    void PreDraw(ID3D12GraphicsCommandList* commandList);

    // キューブの描画
    void DrawWireCube(ID3D12GraphicsCommandList* commandList, const Matrix4x4& worldMatrix, const Vector4& color, int instanceIndex);
    void DrawWireSphere(ID3D12GraphicsCommandList* commandList, const Matrix4x4& worldMatrix, const Vector4& color, int instanceIndex);
    void DrawWireCylinder(ID3D12GraphicsCommandList* commandList, const Matrix4x4& worldMatrix, const Vector4& color, int instanceIndex);
private:
    DirectXCommon* dxCommon_ = nullptr;
    static const int kMaxInstances = 2048;

    // --- プリミティブ描画リソース ---
    Microsoft::WRL::ComPtr<ID3D12RootSignature> primitiveRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> primitivePipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> cubeVertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW cubeVertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> cubeIndexBuffer_;
    D3D12_INDEX_BUFFER_VIEW cubeIndexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> primitiveWVPBuffer_;
    AlignedMatrix4x4* primitiveWVPData_ = nullptr;
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