
#pragma once
#include <d3d12.h> 
#include <wrl.h>   
#include <vector>  
#include "engine/utility/math/Math.h"
#include <string> 
#include"BaseScene.h"


class Object3d;
class DirectXCommon;
class SceneManager;

// ★★★ アライメントエラー対策 ★★★

struct AlignedMatrix4x4 {
    Matrix4x4 matrix;
    // 256バイト (sizeof(Matrix4x4)=64)
    char padding[256 - sizeof(Matrix4x4)];
};

struct AlignedVector4 {
    Vector4 vector;
    // 256バイト (sizeof(Vector4)=16)
    char padding[256 - sizeof(Vector4)];
};



class DebugEditor {
public:
    void Initialize( SceneManager* sceneManager, DirectXCommon* dxCommon
    );
    void Update();
    void Finalize();
    void DrawDebug(ID3D12GraphicsCommandList* commandList);
    void DrawImGui();
    void DrawProjectWindow();
    void UpdateObjectInSceneJSON(Object3d* object, const std::string& filename);

private:
    void InitializePrimitiveDrawing();
    // instanceIndex を引数に追加
    void DrawWireCube(ID3D12GraphicsCommandList* commandList, const Matrix4x4& worldMatrix, const Vector4& color, int instanceIndex);

    
private:
    SceneManager* sceneManager_ = nullptr;
    Object3d* selectedObject_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;

    bool drawColliders_ = true;
    /// <summary>
    /// 最後に Update を実行したシーン
    /// </summary>
    BaseScene* lastUpdatedScene_ = nullptr;

    // 同時に描画するコライダーの最大数
    static const int kMaxInstances = 2048;

    // --- プリミティブ描画リソース ---
    Microsoft::WRL::ComPtr<ID3D12RootSignature> primitiveRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> primitivePipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> cubeVertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW cubeVertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> cubeIndexBuffer_;
    D3D12_INDEX_BUFFER_VIEW cubeIndexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> primitiveWVPBuffer_;
    // ★ 修正: アライメント済み構造体のポインタに変更
    AlignedMatrix4x4* primitiveWVPData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> primitiveColorBuffer_;
    // ★ 修正: アライメント済み構造体のポインタに変更
    AlignedVector4* primitiveColorData_ = nullptr;

    // モデル名一覧
    std::vector<std::string> modelNames_;

    // ImGui のリストボックスで選択されているインデックス
    int selectedModelIndex_ = 0;

    // ★ スポナーウィンドウを描画する private 関数
    void DrawObjectSpawnerWindow();
};