#pragma once

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>

#include "BaseScene.h"
#include "CollisionConfig.h"
#include "IEditable.h"
#include "Transform.h"
#include "engine/utility/math/Math.h"

// Editorを構成するサブウィンドウ。
#include "AnimationWorkbench.h"
#include "AnimatorControllerEditor.h"
#include "AssetAuditWindow.h"
#include "AudioSettingsWindow.h"
#include "CaptureToolWindow.h"
#include "EditorCommon.h"
#include "EditorPropertyRegistry.h"
#include "EditorTransactionManager.h"
#include "EffectPreviewStage.h"
#include "EventLinkGraph.h"
#include "ExecutablePackageWindow.h"
#include "GameDataDebugEditor.h"
#include "GhostDirector.h"
#include "HierarchyWindow.h"
#include "InspectorWindow.h"
#include "JsonBackupWindow.h"
#include "MaterialPreviewBoard.h"
#include "ModelOptimizerWindow.h"
#include "NodeGraphEditorWindow.h"
#include "PrimitiveDrawer.h"
#include "ProjectWindow.h"
#include "PropertyMatrixWindow.h"
#include "SceneSavePreview.h"
#include "SceneSerializer.h"
#include "SceneValidator.h"
#include "StatusTuningWindow.h"
#include "TerrainEditorWindow.h"
#include "Text3DGenerator.h"
#include "TextSpriteGenerator.h"

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

/// <summary>
/// DebugEditor全体の司令塔。各Editorウィンドウ、シーン編集、保存、Undo/Redo、GameView配置を統括する。
/// </summary>
// DebugEditorは、エディタ全体の入力、選択、保存、プレビュー描画を束ねる中核クラスです。
class DebugEditor : public IEditable {
    friend class HierarchyWindow; // 既存の直接参照互換のため残す。

public:
    struct ObjectStateSnapshot {
        Object3d* object = nullptr;
        nlohmann::json beforeState;
    };

    // ライフサイクル。
        // シーン管理やDirectX関連の参照を受け取り、各エディタ機能を利用できる状態にします。
void Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon);
        // 毎フレームの編集入力、選択状態、プレビュー生成、通知表示などを更新します。
void Update();
    void Finalize();

    // Debug描画と配置プレビュー描画。
        // エディタ用の補助表示やギズモなど、ゲーム本編とは別のデバッグ描画を行います。
void DrawDebug(ID3D12GraphicsCommandList* commandList);
    void DrawPreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    // IEditableとしてInspectorに表示する内容。
        // Hierarchy、Inspector、ProjectなどのエディタUIを描画します。
void DrawImGui() override;
    std::string GetName() override {
        return (selectedObject_ && IsObjectInCurrentScene(selectedObject_)) ? selectedObject_->GetName() + " (Object3D)" : "Scene Settings";
    }

    // 主要ウィンドウ。
    void DrawProjectWindow();
    void DrawHierarchy();
    // 下段を別の固定Editorで使う間だけProject描画を止めます。
    void SetProjectWindowVisible(bool visible) { projectWindowVisible_ = visible; }

    // シーン保存と保存通知。
    void SaveScene(SaveMode mode = SaveMode::All);
    void SaveSingleObject();
    void TriggerSaveNotification(const std::string& filename);
    void DrawSaveNotification();

    // 選択中オブジェクトへの編集操作。
        // 選択中オブジェクトを複製し、編集対象として扱えるように登録します。
void DuplicateSelected();
        // 選択中オブジェクトを削除し、必要なエディタ状態も同時に整理します。
void DeleteSelected();
        // 直前の編集コマンドを取り消して、オブジェクト状態を復元します。
void PerformUndo();
        // Undoで戻した編集コマンドを再適用します。
void PerformRedo();
    void SplitSelectedModelIntoMeshChildren();
    void DropToFloor();
    void InstantiateModelAtCursor(const std::string& modelName);
    void InstantiatePresetAtCursor(const std::string& presetName);
    void InstantiatePrefabAtCursor(const std::string& prefabName);
    void InstantiateParticleAtCursor(const std::string& particleName);
    void StartPresetBrush(const std::string& presetName);
    void StopPresetBrush();
    void OpenGameViewCreateContextMenu();
    void DrawGameViewCreateContextMenu();
    Vector3 CalculateGameViewCreatePosition(const Object3d* object);
    void StartGameViewCreatePreview(std::unique_ptr<Object3d> object, const std::string& label);
    void StartGameViewCreatePreview(std::vector<std::unique_ptr<Object3d>> objects, const std::string& label);
    void AddEditorObject(std::unique_ptr<Object3d> object, const std::string& label);
    void AddEditorObjects(std::vector<std::unique_ptr<Object3d>> objects, const std::string& label);
    bool BeginPrefabEditSession(const std::string& prefabName);
    bool SavePrefabEditSession();
    void CancelPrefabEditSession();
    bool IsPrefabEditMode() const { return prefabEditMode_; }
    bool IsPrefabEditDirty() const { return prefabEditDirty_; }
    const std::string& GetPrefabEditName() const { return prefabEditName_; }
    bool IsPrefabEditObject(const Object3d* object) const;
    nlohmann::json CaptureObjectState(Object3d* object) const;
    std::vector<ObjectStateSnapshot> CaptureObjectStates(const std::vector<Object3d*>& objects) const;
    void RegisterObjectEdited(Object3d* object, const std::string& label);
    void RegisterObjectEdited(Object3d* object, const nlohmann::json& beforeState, const std::string& label);
    void RegisterObjectsEdited(const std::vector<ObjectStateSnapshot>& beforeStates, const std::string& label);
    void MarkDirtyForObject(Object3d* object);
    void MarkDirty(SaveMode mode);
    void ClearDirty(SaveMode mode);
    bool IsDirty(SaveMode mode) const;
    bool HasAnyDirty() const;
    std::string GetDirtySummaryText() const;

    // GameViewの領域と入力状態。
    void SetGameViewRegion(const Vector2& offset, const Vector2& size) {
        gameViewOffset_ = offset;
        gameViewSize_ = size;
        animationWorkbench_.SetGameViewRegion(offset, size);
        textSpriteGenerator_.SetGameViewRegion(offset, size);
        modelOptimizerWindow_.SetGameViewRegion(offset, size);
    }
    void SetGameViewScreenRect(float left, float top, float right, float bottom) {
        gameViewScreenRectLeft_ = static_cast<int>(left);
        gameViewScreenRectTop_ = static_cast<int>(top);
        gameViewScreenRectRight_ = static_cast<int>(right);
        gameViewScreenRectBottom_ = static_cast<int>(bottom);
        hasGameViewScreenRect_ = gameViewScreenRectRight_ > gameViewScreenRectLeft_ && gameViewScreenRectBottom_ > gameViewScreenRectTop_;
    }
    bool GetGameViewScreenRect(int& left, int& top, int& right, int& bottom) const {
        if (!hasGameViewScreenRect_) {
            return false;
        }
        left = gameViewScreenRectLeft_;
        top = gameViewScreenRectTop_;
        right = gameViewScreenRectRight_;
        bottom = gameViewScreenRectBottom_;
        return true;
    }
    void SetGameViewHovered(bool hovered) { isGameViewHovered_ = hovered; animationWorkbench_.SetGameViewHovered(hovered); }
    void SetGameViewMousePos(const Vector2& pos) { gameViewMousePos_ = pos; animationWorkbench_.SetGameViewMousePos(pos); }

    void SetSceneFilename(const std::string& filepath) {
        std::string name = filepath;
        size_t pos = name.find_last_of("/\\");
        if (pos != std::string::npos) name = name.substr(pos + 1);
        strcpy_s(currentSceneFilename_, sizeof(currentSceneFilename_), name.c_str());
    }

    void SetSelectedObject(Object3d* obj);
    void SyncObjectSelectionToInspector();
    void SetPreviewObject(std::unique_ptr<Object3d> obj, const std::string& label = "Place Preview Object");
    void SetIsPathEditMode(bool mode) { isPathEditMode_ = mode; }

    /// <summary>
    /// 外部で生成された各種サブエディタへの参照を登録する。
    /// </summary>
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
        postEffectEditor_ = postEffectEditor;
        spriteDebugEditor_ = spriteDebugEditor;
        particleEditor_ = particleEditor;
        gpuParticleEditor_ = gpuParticleEditor;
        vfxSequencerEditor_ = vfxSequencerEditor;
        ghostRecorder_ = ghostRecorder;
        ghostDirector_ = ghostDirector;
        lightEditor_ = lightEditor;
        meshEffectEditor_ = meshEffectEditor;
        debrisEffectEditor_ = debrisEffectEditor;
        trailEmitterEditor_ = trailEmitterEditor;
    }

    // 選択状態とサブエディタ参照。
    Object3d* GetSelectedObject3D() const { return IsObjectInCurrentScene(selectedObject_) ? selectedObject_ : nullptr; }
    Object3d* GetSelectedObject() const { return IsObjectInCurrentScene(selectedObject_) ? selectedObject_ : nullptr; }
    const std::vector<Object3d*>& GetSelectedObjects() const { return selectedObjects_; }
    int GetSelectionOverlayMode() const;
    void SetSelectionOverlayMode(int mode);
    bool IsObjectSelected(const Object3d* object) const;
    std::size_t GetSelectedObjectCount() const { return selectedObjects_.size(); }
    void ClearObjectSelection();
    void AddSelectedObject(Object3d* object);
    void ToggleSelectedObject(Object3d* object);
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
    AnimatorControllerEditor* GetAnimatorControllerEditor() { return &animatorControllerEditor_; }
    EventLinkGraph* GetEventLinkGraph() { return &eventLinkGraph_; }
    NodeGraphEditorWindow* GetNodeGraphEditorWindow() { return &nodeGraphEditorWindow_; }
    TextSpriteGenerator* GetTextSpriteGenerator() { return &textSpriteGenerator_; }
    Text3DGenerator* GetText3DGenerator() { return &text3DGenerator_; }
    ModelOptimizerWindow* GetModelOptimizerWindow() { return &modelOptimizerWindow_; }
    AssetAuditWindow* GetAssetAuditWindow() { return &assetAuditWindow_; }
    PropertyMatrixWindow* GetPropertyMatrixWindow() { return &propertyMatrixWindow_; }
    StatusTuningWindow* GetStatusTuningWindow() { return &statusTuningWindow_; }
    GameDataDebugEditor* GetGameDataDebugEditor() { return &gameDataDebugEditor_; }
    TerrainEditorWindow* GetTerrainEditorWindow() { return &terrainEditorWindow_; }
    JsonBackupWindow* GetJsonBackupWindow() { return &jsonBackupWindow_; }
    AudioSettingsWindow* GetAudioSettingsWindow() { return &audioSettingsWindow_; }
    ExecutablePackageWindow* GetExecutablePackageWindow() { return &executablePackageWindow_; }
    CaptureToolWindow* GetCaptureToolWindow() { return &captureToolWindow_; }
    ProjectWindow* GetProjectWindow() { return &projectWindow_; }
    bool* GetDrawEventIDsPtr() { return &drawEventIDs_; }

private:
        // モデルやプリセット生成時の配置結果をまとめ、生成後の選択やUndo登録に使います。
struct PlacementResult {
        Vector3 position = { 0.0f, 0.0f, 0.0f };
        Vector3 contactPosition = { 0.0f, 0.0f, 0.0f };
        Vector3 normal = { 0.0f, 1.0f, 0.0f };
        bool found = false;
        bool hasSurface = false;
    };

    // GameView配置、プレビュー、保存、Undo/Redoで使う内部処理。
    Ray ScreenPointToRay(const Vector2& mousePos);
    bool IntersectRayPlane(const Ray& ray, Vector3& intersectOut);
    PlacementResult CalculateGameViewPlacement(const Object3d* object, const Vector2& mousePos, bool useGridSnap = true);
    void ApplyGameViewPlacement(Object3d* object, const PlacementResult& placement, bool alignToSurface);
    void UpdatePreviewPlacement();
    void ConfirmPreviewPlacement();
    void CancelPreviewPlacement();
    void UpdatePresetBrush();
    void RebuildPresetBrushPreview();
    void StampPresetBrush();
    void DrawPresetBrushOverlay();
    void ApplyBrushPreviewVisual(Object3d* object);
    void ApplyPreviewVisual(Object3d* object);
    void RestorePreviewVisual(Object3d* object);
    void DrawPreviewWire(ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit);
    void DrawPreviewMarker();
    Vector3 WorldToScreen(const Vector3& worldPos);
    void Draw3DIcons();
    void DrawEventIDOverlay();
    void DrawSavePreview();
    void InitializeCrashRecovery();
    void UpdateCrashRecovery(float deltaTime);
    void FinalizeCrashRecovery();
    void SaveCrashRecoveryDraft();
    void WriteCrashRecoverySession(bool cleanExit, bool dirty, const nlohmann::json& files = nlohmann::json::array());
    void LoadCrashRecoveryCandidate();
    void DrawCrashRecoveryPrompt();
    bool RestoreCrashRecoveryDraft();
    void ResolveCrashRecoveryCandidate(const std::string& resolution);
    void PollDDSCacheNotifications();
    std::string MakeSavePreviewTitle(SaveMode mode) const;
    bool IsObjectInCurrentScene(const Object3d* object) const;
    void ClearInvalidSelectedObject();
    void PruneInvalidSelectedObjects();
    Object3d* PickObjectAtGameViewPos(const Vector2& mousePos);
    void SelectObjectsInGameViewRect(const Vector2& start, const Vector2& end, bool additive);
    void DrawSelectionRectangleOverlay();
    void DrawSelectedObjectBoundsOverlay();
    Vector3 GetObjectWorldPositionForSelection(Object3d* object);
    void ApplyObjectState(Object3d* object, const nlohmann::json& state);
    Object3d* FindObjectByName(const std::string& name) const;
    Object3d* AddObjectFromState(const nlohmann::json& state);
    std::unique_ptr<Object3d> RemoveObjectImmediate(Object3d* object);
    void EndPrefabEditSession(bool clearTransactions);
    void ResetPrefabEditSessionForSceneChange();
    void TrackInspectorEdit(const std::vector<ObjectStateSnapshot>& beforeStates);
    void MarkDirtyForCategory(const std::string& category);

private:
    // コアシステムへの参照。
    SceneManager* sceneManager_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;
    Math math_;

    // 選択、配置プレビュー、プリセットブラシの状態。
    enum class SelectionOverlayMode {
        Compact = 0,
        Detailed,
        Hidden
    };

    Object3d* selectedObject_ = nullptr;
    std::vector<Object3d*> selectedObjects_;
    SelectionOverlayMode selectionOverlayMode_ = SelectionOverlayMode::Compact;
    std::vector<ObjectStateSnapshot> groupTransformStartStates_;
    bool isSelectionRectDragging_ = false;
    bool isSelectionRectReady_ = false;
    Vector2 selectionRectStart_ = { 0.0f, 0.0f };
    Vector2 selectionRectEnd_ = { 0.0f, 0.0f };
    std::unique_ptr<Object3d> previewObject_ = nullptr;
    std::vector<std::unique_ptr<Object3d>> previewChildObjects_;
    std::vector<std::unique_ptr<Object3d>> brushPreviewObjects_;
    bool isPresetBrushMode_ = false;
    std::string brushPresetName_;
    float brushSpacing_ = 2.0f;
    bool hasLastBrushStamp_ = false;
    Vector3 lastBrushStampPosition_ = { 0.0f, 0.0f, 0.0f };
    Vector4 previewObjectOriginalColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    BlendMode previewObjectOriginalBlendMode_ = BlendMode::kNormal;
    int32_t previewObjectOriginalMaterialType_ = 0;
    int32_t previewObjectOriginalSelectedLighting_ = 2;
    int32_t previewObjectOriginalEnableLighting_ = 1;
    float previewObjectOriginalEmissive_ = 1.0f;
    std::string previewObjectOriginalClassName_;
    std::string previewObjectOriginalModelName_;
    Model* previewObjectOriginalModel_ = nullptr;
    ColliderConfig previewObjectOriginalColliderConfig_{};
    bool previewObjectUsesFallbackModel_ = false;
    Vector3 previewPlacementContactPosition_ = { 0.0f, 0.0f, 0.0f };
    bool hasPreviewPlacementContact_ = false;
    std::string previewCreateCommandLabel_ = "Place Preview Object";

        // 生成プレビュー中だけ一時的に差し替える表示状態を保持します。
struct PreviewVisualState {
        std::string className;
        Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        BlendMode blendMode = BlendMode::kNormal;
        int32_t materialType = 0;
        int32_t selectedLighting = 2;
        int32_t enableLighting = 1;
        float emissive = 1.0f;
    };
    std::unordered_map<Object3d*, PreviewVisualState> previewVisualStates_;
    BaseScene* lastUpdatedScene_ = nullptr;

    // Prefab Modeでは現在Sceneを一時的に隠し、Prefab階層だけを同じInspector/Gizmoで編集します。
    bool prefabEditMode_ = false;
    bool prefabEditDirty_ = false;
    std::string prefabEditName_;
    Object3d* prefabEditRoot_ = nullptr;
    BaseScene* prefabEditScene_ = nullptr;
    std::unordered_map<Object3d*, bool> prefabEditPreviousVisibility_;

    bool drawColliders_ = true;
    bool drawEventIDs_ = true;
    bool isPathEditMode_ = false;
    bool projectWindowVisible_ = true;

    // ファイル名、検索、モデル一覧。
    char currentSceneFilename_[128] = "scene_layout.json";
    char searchFilter_[128] = "";
    std::vector<std::string> modelNames_;
    std::vector<std::string> sceneFiles_;
    int selectedModelIndex_ = 0;
    std::string currentPreviewModelName_ = "";
    char presetNameBuffer_[64] = "";

    // GameViewの領域と右クリック生成メニュー。
    Vector2 gameViewMousePos_ = { 0, 0 };
    Vector2 gameViewSize_ = { 1266, 530 };
    Vector2 gameViewOffset_ = { 0, 0 };
    int gameViewScreenRectLeft_ = 0;
    int gameViewScreenRectTop_ = 0;
    int gameViewScreenRectRight_ = 0;
    int gameViewScreenRectBottom_ = 0;
    bool hasGameViewScreenRect_ = false;
    bool isGameViewHovered_ = false;
    Vector2 gameViewCreateMenuMousePos_ = { 0, 0 };
    Vector2 gameViewCreateMenuScreenPos_ = { 0, 0 };
    bool requestGameViewCreateMenu_ = false;

    // 保存対象ごとのDirty状態。
    bool dirtyPlayer_ = false;
    bool dirtyEnemy_ = false;
    bool dirtyObject_ = false;
    bool dirtyCamera_ = false;
    SaveMode pendingSaveMode_ = SaveMode::All;
    bool pendingSaveIsSingleObject_ = false;

    // Undo/Redoコマンド。
        // Undo/Redoで扱う編集操作の種類を表します。
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
    void ApplyEditorCommand(const EditorCommand& command, bool undo);
    std::vector<ObjectStateSnapshot> inspectorEditStartStates_;
    bool hasInspectorEditStart_ = false;
    Transform tempTransformStart_;
    nlohmann::json tempObjectStateStart_;
    bool isDraggingTransform_ = false;

    // スナップとプレビュー補助。
    bool isGridSnapEnabled_ = false;
    float snapValue_ = 1.0f;
    int gizmoPivotMode_ = 0; // 0: 原点, 1: モデル中心, 2: コリジョン中心
    bool wasPreviewActive_ = false;
    int previousCameraMode_ = 0;

    // 通知UIとDDSキャッシュ通知。
    float saveNotificationTimer_ = 0.0f;
    std::string saveNotificationMsg_ = "";
    std::uintmax_t ddsCacheNotificationReadOffset_ = 0;
    std::string crashRecoverySessionId_;
    std::string crashRecoverySessionDir_;
    std::string crashRecoveryDraftDir_;
    nlohmann::json crashRecoveryPendingManifest_;
    nlohmann::json crashRecoveryLastFiles_;
    float crashRecoveryAutosaveTimer_ = 0.0f;
    float crashRecoveryHeartbeatTimer_ = 0.0f;
    bool crashRecoverySessionActive_ = false;
    bool crashRecoveryLastDirty_ = false;
    bool crashRecoveryPending_ = false;
    bool crashRecoveryPromptOpened_ = false;
    std::string crashRecoveryStatus_;

    // 外部所有のサブエディタ。
    PostEffectEditor* postEffectEditor_ = nullptr;
    SpriteDebugEditor* spriteDebugEditor_ = nullptr;
    ParticleEditor* particleEditor_ = nullptr;
    GPUParticleEditor* gpuParticleEditor_ = nullptr;
    VFXSequencerEditor* vfxSequencerEditor_ = nullptr;
    GhostRecorder* ghostRecorder_ = nullptr;
    GhostDirector* ghostDirector_ = nullptr;
    LightEditor* lightEditor_ = nullptr;

    // DebugEditorが所有するサブウィンドウ。
    HierarchyWindow hierarchyWindow_;
    ProjectWindow projectWindow_;
    InspectorWindow inspectorWindow_;
    SceneSerializer serializer_;
    PrimitiveDrawer primitiveDrawer_;
    SceneValidator sceneValidator_;
    MaterialPreviewBoard materialPreviewBoard_;
    AnimationWorkbench animationWorkbench_;
    AnimatorControllerEditor animatorControllerEditor_;
    SceneSavePreview sceneSavePreview_;
    EventLinkGraph eventLinkGraph_;
    NodeGraphEditorWindow nodeGraphEditorWindow_;
    TextSpriteGenerator textSpriteGenerator_;
    Text3DGenerator text3DGenerator_;
    ModelOptimizerWindow modelOptimizerWindow_;
    AssetAuditWindow assetAuditWindow_;
    PropertyMatrixWindow propertyMatrixWindow_;
    StatusTuningWindow statusTuningWindow_;
    GameDataDebugEditor gameDataDebugEditor_;
    TerrainEditorWindow terrainEditorWindow_;
    JsonBackupWindow jsonBackupWindow_;
    AudioSettingsWindow audioSettingsWindow_;
    ExecutablePackageWindow executablePackageWindow_;
    CaptureToolWindow captureToolWindow_;

    // 外部所有のVFX系サブエディタ。
    MeshEffectEditor* meshEffectEditor_ = nullptr;
    DebrisEffectEditor* debrisEffectEditor_ = nullptr;
    TrailEmitterEditor* trailEmitterEditor_ = nullptr;
};
