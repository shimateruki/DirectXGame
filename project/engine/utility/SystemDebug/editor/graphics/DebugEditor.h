#pragma once

// ========================================================================
// Includes
// ========================================================================
#include <d3d12.h> 
#include <wrl.h>   
#include <vector>  
#include <string> 
#include <deque>
#include <memory>

#include "engine/utility/math/Math.h"
#include "BaseScene.h"
#include "Transform.h"
#include "IEditable.h" 
#include "CollisionConfig.h"

// --- サブモジュール群 ---
#include "HierarchyWindow.h"
#include "ProjectWindow.h"
#include "InspectorWindow.h"
#include "EditorCommon.h"
#include "SceneSerializer.h"
#include "PrimitiveDrawer.h"
#include "GhostDirector.h"
#include "SceneValidator.h"
#include "MaterialPreviewBoard.h"
#include "EffectPreviewStage.h"
#include "AnimationWorkbench.h"
#include "SceneSavePreview.h"
#include "EventLinkGraph.h"


// ========================================================================
// 前方宣言 (Forward Declarations)
// ========================================================================
class Object3d;
class DebrisEffectEditor;
class DirectXCommon;
class SceneManager;
class GhostRecorder;
class Model;
class PostEffectEditor;
class SpriteDebugEditor;
class ParticleEditor;
class GPUParticleEditor;
class VFXSequencerEditor;
class LightEditor;
class MeshEffectEditor;
class TrailEmitterEditor;

// ========================================================================
// DebugEditor クラス
// 役割: エディタのメイン制御、各種ウィンドウの統括、シーンへの干渉を行う
// ========================================================================
class DebugEditor : public IEditable {
    friend class HierarchyWindow; // ゲッター経由に移行済みですが、互換性のため残置

public:
    // --------------------------------------------------------------------
    // ライフサイクル (Lifecycle)
    // --------------------------------------------------------------------
    void Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon);
    void Update();
    void Finalize();

    // --------------------------------------------------------------------
    // 描画関連 (Rendering)
    // --------------------------------------------------------------------
    void DrawDebug(ID3D12GraphicsCommandList* commandList);
    void DrawPreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    // IEditableの実装: Inspector等に描画されるUI
    void DrawImGui() override;
    std::string GetName() override {
        return selectedObject_ ? selectedObject_->GetName() + " (Object3D)" : "Scene Settings";
    }

    // --------------------------------------------------------------------
    // ウィンドウ個別描画 (Windows)
    // --------------------------------------------------------------------
    void DrawProjectWindow();
    void DrawHierarchy();

    // --------------------------------------------------------------------
    // シーン操作・ファイル管理 (File Management)
    // --------------------------------------------------------------------
    void SaveScene(SaveMode mode = SaveMode::All);
    void SaveSingleObject();      // 単体保存
    void TriggerSaveNotification(const std::string& filename);
    void DrawSaveNotification();

    // --------------------------------------------------------------------
    // オブジェクト編集機能 (Edit Operations)
    // --------------------------------------------------------------------
    void DuplicateSelected();     // 複製
    void DeleteSelected();        // 削除
    void PerformUndo();           // 元に戻す
    void PerformRedo();           // やり直し
    void DropToFloor();
    void InstantiateModelAtCursor(const std::string& modelName);
    void InstantiatePresetAtCursor(const std::string& presetName);
    void InstantiateParticleAtCursor(const std::string& particleName);
    void OpenGameViewCreateContextMenu();
    void DrawGameViewCreateContextMenu();
    Vector3 CalculateGameViewCreatePosition(const Object3d* object);
    void StartGameViewCreatePreview(std::unique_ptr<Object3d> object, const std::string& label);
    void AddEditorObject(std::unique_ptr<Object3d> object, const std::string& label);
    nlohmann::json CaptureObjectState(Object3d* object) const;
    void RegisterObjectEdited(Object3d* object, const std::string& label);
    void RegisterObjectEdited(Object3d* object, const nlohmann::json& beforeState, const std::string& label);
    void MarkDirtyForObject(Object3d* object);
    void MarkDirty(SaveMode mode);
    void ClearDirty(SaveMode mode);
    bool IsDirty(SaveMode mode) const;
    bool HasAnyDirty() const;
    std::string GetDirtySummaryText() const;
    // --------------------------------------------------------------------
    // セッター (Setters)
    // --------------------------------------------------------------------
    void SetGameViewRegion(const Vector2& offset, const Vector2& size) {
        gameViewOffset_ = offset;
        gameViewSize_ = size;
        animationWorkbench_.SetGameViewRegion(offset, size);
    }
    void SetGameViewHovered(bool hovered) { isGameViewHovered_ = hovered; animationWorkbench_.SetGameViewHovered(hovered); }
    void SetGameViewMousePos(const Vector2& pos) { gameViewMousePos_ = pos; animationWorkbench_.SetGameViewMousePos(pos); }
    void SetSceneFilename(const std::string& filepath) {
        std::string name = filepath;
        size_t pos = name.find_last_of("/\\");
        if (pos != std::string::npos) name = name.substr(pos + 1);
        strcpy_s(currentSceneFilename_, sizeof(currentSceneFilename_), name.c_str());
    }
    void SetSelectedObject(Object3d* obj) { selectedObject_ = obj; }
    void SetPreviewObject(std::unique_ptr<Object3d> obj, const std::string& label = "Place Preview Object");
    void SetIsPathEditMode(bool mode) { isPathEditMode_ = mode; }
    void SetEditors(
        PostEffectEditor* postEffectEditor,
        SpriteDebugEditor* spriteDebugEditor,
        ParticleEditor* particleEditor,
        GPUParticleEditor* gpuParticleEditor,
        VFXSequencerEditor* vfxSequencerEditor,
        GhostRecorder* ghostRecorder,
        GhostDirector* ghostDirector,
        LightEditor* lightEditor,
        MeshEffectEditor* meshEffectEditor,
        DebrisEffectEditor* debrisEffectEditor,
        TrailEmitterEditor* trailEmitterEditor)
    {
        postEffectEditor_    = postEffectEditor;
        spriteDebugEditor_   = spriteDebugEditor;
        particleEditor_      = particleEditor;
        gpuParticleEditor_   = gpuParticleEditor;
        vfxSequencerEditor_  = vfxSequencerEditor;
        ghostRecorder_       = ghostRecorder;
        ghostDirector_       = ghostDirector;
        lightEditor_         = lightEditor;
        meshEffectEditor_    = meshEffectEditor;
        debrisEffectEditor_  = debrisEffectEditor;
        trailEmitterEditor_  = trailEmitterEditor;
    }
    // --------------------------------------------------------------------
    // ゲッター (Getters)
    // --------------------------------------------------------------------
    Object3d* GetSelectedObject3D() const { return selectedObject_; }
    Object3d* GetSelectedObject() const { return selectedObject_; }
    SceneManager* GetSceneManager() const { return sceneManager_; }

    LightEditor* GetLightEditor() const { return lightEditor_; }
    PostEffectEditor* GetPostEffectEditor() const { return postEffectEditor_; }
    SpriteDebugEditor* GetSpriteDebugEditor() const { return spriteDebugEditor_; }
    GPUParticleEditor* GetGPUParticleEditor() const { return gpuParticleEditor_; }
    VFXSequencerEditor* GetVFXSequencerEditor() const { return vfxSequencerEditor_; }
    ParticleEditor* GetParticleEditor() const { return particleEditor_; }
    GhostRecorder* GetGhostRecorder() const { return ghostRecorder_; }
    GhostDirector* GetGhostDirector() const { return ghostDirector_; }

    char* GetCurrentSceneFilenameBuffer() { return currentSceneFilename_; }
    size_t GetSceneFilenameBufferSize() const { return sizeof(currentSceneFilename_); }
    char* GetSearchFilterBuffer() { return searchFilter_; }
    size_t GetSearchFilterBufferSize() const { return sizeof(searchFilter_); }

    bool* GetDrawCollidersPtr() { return &drawColliders_; }
    bool GetIsPathEditMode() const { return isPathEditMode_; }

    MeshEffectEditor* GetMeshEffectEditor() const { return meshEffectEditor_; }
    void SetMeshEffectEditor(MeshEffectEditor* editor) { meshEffectEditor_ = editor; }
    DebrisEffectEditor* GetDebrisEffectEditor() const { return debrisEffectEditor_; }
    TrailEmitterEditor* GetTrailEmitterEditor() const { return trailEmitterEditor_; }
    SceneValidator* GetSceneValidator() { return &sceneValidator_; }
    MaterialPreviewBoard* GetMaterialPreviewBoard() { return &materialPreviewBoard_; }
    EffectPreviewStage* GetEffectPreviewStage() { return EffectPreviewStage::GetInstance(); }
    AnimationWorkbench* GetAnimationWorkbench() { return &animationWorkbench_; }
    EventLinkGraph* GetEventLinkGraph() { return &eventLinkGraph_; }
    ProjectWindow* GetProjectWindow() { return &projectWindow_; }
    bool* GetDrawEventIDsPtr() { return &drawEventIDs_; }

private:
    struct PlacementResult {
        Vector3 position = { 0.0f, 0.0f, 0.0f };
        Vector3 contactPosition = { 0.0f, 0.0f, 0.0f };
        Vector3 normal = { 0.0f, 1.0f, 0.0f };
        bool found = false;
        bool hasSurface = false;
    };

    // --------------------------------------------------------------------
    // 内部ヘルパー (Internal Helpers)
    // --------------------------------------------------------------------
    Ray ScreenPointToRay(const Vector2& mousePos);
    bool IntersectRayPlane(const Ray& ray, Vector3& intersectOut);
    PlacementResult CalculateGameViewPlacement(const Object3d* object, const Vector2& mousePos, bool useGridSnap = true);
    void ApplyGameViewPlacement(Object3d* object, const PlacementResult& placement, bool alignToSurface);
    void UpdatePreviewPlacement();
    void ConfirmPreviewPlacement();
    void CancelPreviewPlacement();
    void ApplyPreviewVisual(Object3d* object);
    void RestorePreviewVisual(Object3d* object);
    void DrawPreviewWire(ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit);
    void DrawPreviewMarker();
    Vector3 WorldToScreen(const Vector3& worldPos);
    void Draw3DIcons();
    void DrawEventIDOverlay();
    void DrawSavePreview();
    std::string MakeSavePreviewTitle(SaveMode mode) const;
    void ApplyObjectState(Object3d* object, const nlohmann::json& state);
    Object3d* FindObjectByName(const std::string& name) const;
    Object3d* AddObjectFromState(const nlohmann::json& state);
    std::unique_ptr<Object3d> RemoveObjectImmediate(Object3d* object);
    void TrackInspectorEdit(Object3d* beforeTarget, const nlohmann::json& beforeState);
    void MarkDirtyForCategory(const std::string& category);
private:
    // ====================================================================
    // メンバ変数
    // ====================================================================

    // --- コアシステムへの参照 ---
    SceneManager* sceneManager_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;
    Math math_;

    // --- エディタの状態・データ ---
    Object3d* selectedObject_ = nullptr;
    std::unique_ptr<Object3d> previewObject_ = nullptr;
    Vector4 previewObjectOriginalColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    BlendMode previewObjectOriginalBlendMode_ = BlendMode::kNormal;
    int32_t previewObjectOriginalMaterialType_ = 0;
    float previewObjectOriginalEmissive_ = 1.0f;
    std::string previewObjectOriginalClassName_;
    std::string previewObjectOriginalModelName_;
    Model* previewObjectOriginalModel_ = nullptr;
    ColliderConfig previewObjectOriginalColliderConfig_{};
    bool previewObjectUsesFallbackModel_ = false;
    Vector3 previewPlacementContactPosition_ = { 0.0f, 0.0f, 0.0f };
    bool hasPreviewPlacementContact_ = false;
    std::string previewCreateCommandLabel_ = "Place Preview Object";
    BaseScene* lastUpdatedScene_ = nullptr;

    bool drawColliders_ = true;
    bool drawEventIDs_ = true;
    bool isPathEditMode_ = false;

    // --- ファイル関連 ---
    char currentSceneFilename_[128] = "scene_layout.json";
    char searchFilter_[128] = "";
    std::vector<std::string> modelNames_;
    std::vector<std::string> sceneFiles_;
    int selectedModelIndex_ = 0;
    std::string currentPreviewModelName_ = "";
    char presetNameBuffer_[64] = "";

    // --- GameView領域 (レイキャスト用) ---
    Vector2 gameViewMousePos_ = { 0, 0 };
    Vector2 gameViewSize_ = { 1266, 530 };
    Vector2 gameViewOffset_ = { 0, 0 };
    bool isGameViewHovered_ = false;
    Vector2 gameViewCreateMenuMousePos_ = { 0, 0 };
    Vector2 gameViewCreateMenuScreenPos_ = { 0, 0 };
    bool requestGameViewCreateMenu_ = false;

    // --- Dirty管理 ---
    bool dirtyPlayer_ = false;
    bool dirtyEnemy_ = false;
    bool dirtyObject_ = false;
    SaveMode pendingSaveMode_ = SaveMode::All;
    bool pendingSaveIsSingleObject_ = false;

    // --- Undo/Redo システム ---
    enum class EditorCommandType {
        ObjectCreated,
        ObjectDeleted,
        ObjectEdited
    };
    struct EditorCommand {
        EditorCommandType type = EditorCommandType::ObjectEdited;
        std::string label;
        std::string beforeName;
        std::string afterName;
        nlohmann::json beforeState;
        nlohmann::json afterState;
    };
    void RegisterCommand(const EditorCommand& command);
    std::deque<EditorCommand> undoStack_;
    std::deque<EditorCommand> redoStack_;
    nlohmann::json inspectorEditStartState_;
    Object3d* inspectorEditTarget_ = nullptr;
    bool hasInspectorEditStart_ = false;
    Transform tempTransformStart_;
    nlohmann::json tempObjectStateStart_;
    bool isDraggingTransform_ = false;

    // --- スナップ機能 ---
    bool isGridSnapEnabled_ = false;
    float snapValue_ = 1.0f;

    // --- カメラ制御・プレビューフラグ ---
    bool wasPreviewActive_ = false;
    int previousCameraMode_ = 0;

    // --- 通知UI用 ---
    float saveNotificationTimer_ = 0.0f;
    std::string saveNotificationMsg_ = "";

    // --- 各種サブエディタへのポインタ ---
    PostEffectEditor* postEffectEditor_ = nullptr;
    SpriteDebugEditor* spriteDebugEditor_ = nullptr;
    ParticleEditor* particleEditor_ = nullptr;
    GPUParticleEditor* gpuParticleEditor_ = nullptr;
    VFXSequencerEditor* vfxSequencerEditor_ = nullptr;
    GhostRecorder* ghostRecorder_ = nullptr;
    GhostDirector* ghostDirector_ = nullptr;
    LightEditor* lightEditor_ = nullptr;

    // --- ウィンドウ・サブモジュール インスタンス ---
    HierarchyWindow hierarchyWindow_;
    ProjectWindow projectWindow_;
    InspectorWindow inspectorWindow_;
    SceneSerializer serializer_;
    PrimitiveDrawer primitiveDrawer_; // デバッグ描画管理 (DX12の処理を隔離)
    SceneValidator sceneValidator_;
    MaterialPreviewBoard materialPreviewBoard_;
    AnimationWorkbench animationWorkbench_;
    SceneSavePreview sceneSavePreview_;
    EventLinkGraph eventLinkGraph_;
    MeshEffectEditor* meshEffectEditor_ = nullptr;
    DebrisEffectEditor* debrisEffectEditor_ = nullptr;
    TrailEmitterEditor* trailEmitterEditor_ = nullptr;

};
