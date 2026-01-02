
#pragma once
#include <d3d12.h> 
#include <wrl.h>   
#include <vector>  
#include "engine/utility/math/Math.h"
#include <string> 
#include"BaseScene.h"
#include <deque>

class Object3d;
class DirectXCommon;
class SceneManager;


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
    // ビットフラグ編集用のヘルパー関数
    void DrawAttributeSelector(const char* label, uint32_t* attribute);
    void DrawHierarchyNode(Object3d* obj);

    void SaveScene();             // シーン全体保存 (Ctrl + S)
    void SaveSingleObject();      // 単体保存 (Ctrl + Shift + S)
    void DuplicateSelected();     // 複製 (Ctrl + C)
    void DeleteSelected();        // 削除 (Delete)



private:
    void InitializePrimitiveDrawing();
    // instanceIndex を引数に追加
    void DrawWireCube(ID3D12GraphicsCommandList* commandList, const Matrix4x4& worldMatrix, const Vector4& color, int instanceIndex);

    void DrawHierarchyPanel(); // 左側：リストなど
    void DrawInspectorPanel(); // 右側：詳細設定
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
    AlignedMatrix4x4* primitiveWVPData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> primitiveColorBuffer_;
    AlignedVector4* primitiveColorData_ = nullptr;
    // モデル名一覧
    std::vector<std::string> modelNames_;
    // ImGui のリストボックスで選択されているインデックス
    int selectedModelIndex_ = 0;
    char currentSceneFilename_[128] = "scene_layout.json";
    std::vector<std::string> sceneFiles_;

    char searchFilter_[128] = ""; // 検索文字用バッファ

    //  Undoシステム用構造体 
    struct TransformCommand {
        Object3d* target;        // 操作したオブジェクト
        Object3d::Transform oldTf; // 変更前の状態
        Object3d::Transform newTf; // 変更後の状態
    };

    // 履歴スタック (最大50件くらい保存)
    std::deque<TransformCommand> undoStack_;

    // 編集中の一時保存用 (ドラッグ開始時の状態)
    Object3d::Transform tempTransformStart_;
    bool isDraggingTransform_ = false;

    // Undo実行関数
    void PerformUndo();
};