#pragma once

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <deque>
#include <memory>
#include <functional>
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
#include "EnemyAttackPreviewWindow.h"
#include "ExecutablePackageWindow.h"
#include "GameDataDebugEditor.h"
#include "GhostDirector.h"
#include "HierarchyWindow.h"
#include "engine/utility/SystemDebug/editor/windows/inspector/InspectorWindow.h"
#include "MaterialPreviewBoard.h"
#include "ModelOptimizerWindow.h"
#include "PrimitiveDrawer.h"
#include "ProjectWindow.h"
#include "SceneSavePreview.h"
#include "SceneSerializer.h"
#include "SceneInventoryWindow.h"
#include "StatusTuningWindow.h"
#include "TextSpriteGenerator.h"

namespace engine { class Object3d; }
class DebrisEffectEditor;
class DirectXCommon;
class SceneManager;
class SceneWorkspace;
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
    // 操作開始時の JSON と対象の借用参照。対象が現在シーンに残っていることを登録時に確認する。
    struct ObjectStateSnapshot {
        engine::Object3d* object = nullptr;
        nlohmann::json beforeState;
    };

    /// <summary>シーン管理と描画基盤を借りて編集機能を初期化する。</summary>
    /// <remarks>借用先はFinalizeまで有効である必要がある。</remarks>
    void Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon);
    /// <summary>編集入力、選択、配置プレビュー、通知を毎フレーム更新する。</summary>
    void Update();
    /// <summary>編集状態とサブウィンドウを終了し、借用参照を解除する。</summary>
    void Finalize();

    /// <summary>ゲーム本編とは別の補助表示とギズモを描画する。</summary>
    void DrawDebug(ID3D12GraphicsCommandList* commandList);
    /// <summary>配置前のオブジェクトの3Dプレビューを描画する。</summary>
    void DrawPreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    /// <summary>Hierarchy、Inspector、Projectなどの編集UIを描画する。</summary>
    void DrawImGui() override;
    /// <summary>選択中オブジェクト名、またはシーン設定名を返す。</summary>
    std::string GetName() override {
        return (selectedObject_ && IsObjectInCurrentScene(selectedObject_)) ? selectedObject_->GetName() + " (Object3D)" : "Scene Settings";
    }

    /// <summary>Projectウィンドウを描画する。</summary>
    void DrawProjectWindow();
    /// <summary>Hierarchyウィンドウを描画する。</summary>
    void DrawHierarchy();
    /// <summary>他の固定Editorへ領域を譲る間、Projectの表示を切り替える。</summary>
    void SetProjectWindowVisible(bool visible) { projectWindowVisible_ = visible; }

    /// <summary>指定カテゴリのシーン内容を保存する。</summary>
    void SaveScene(SaveMode mode = SaveMode::All);
    /// <summary>選択中のオブジェクトだけを保存する。</summary>
    void SaveSingleObject();
    /// <summary>保存したファイル名の通知を開始する。</summary>
    void TriggerSaveNotification(const std::string& filename);
    /// <summary>保存完了通知を画面へ描画する。</summary>
    void DrawSaveNotification();

    /// <summary>保存されているScene Assetの一覧を返す。</summary>
    std::vector<SceneSerializer::SceneAssetInfo> GetSceneAssets() const;
    /// <summary>指定テンプレートからScene Assetを新規作成する。</summary>
    /// <param name="createdFilename">作成したファイル名の出力先。</param>
    /// <param name="errorMessage">失敗理由の出力先。</param>
    bool CreateSceneAsset(
        const std::string& sceneId,
        const std::string& displayName,
        const std::string& runtimeScene,
        SceneSerializer::SceneAssetTemplate sceneTemplate,
        std::string& createdFilename,
        std::string& errorMessage);
    /// <summary>既存Scene Assetを新しいIDと表示名で複製する。</summary>
    bool DuplicateSceneAsset(
        const std::string& sourceFilename,
        const std::string& newSceneId,
        const std::string& displayName,
        std::string& createdFilename,
        std::string& errorMessage);
    /// <summary>Scene AssetのIDと表示名を変更する。</summary>
    bool RenameSceneAsset(
        const std::string& sourceFilename,
        const std::string& newSceneId,
        const std::string& displayName,
        std::string& renamedFilename,
        std::string& errorMessage);
    /// <summary>指定ファイルのScene Assetを削除する。</summary>
    bool DeleteSceneAsset(const std::string& filename, std::string& errorMessage);
    /// <summary>Scene Assetに結び付く実行時シーン名を変更する。</summary>
    bool SetSceneAssetRuntimeScene(
        const std::string& filename,
        const std::string& runtimeScene,
        std::string& errorMessage);
    /// <summary>Scene AssetのController、BGM、照明、カメラ、空の設定を変更する。</summary>
    bool SetSceneAssetRuntimeSettings(
        const std::string& filename,
        const std::string& controllerName,
        const std::string& bgmPath,
        const std::string& lightPath,
        const std::string& cameraPath,
        const std::string& skyboxPath,
        std::string& errorMessage);
    /// <summary>Scene Assetの参照先と設定を検証し、結果を返す。</summary>
    SceneSerializer::SceneAssetValidationResult ValidateSceneAsset(const std::string& filename) const;
    /// <summary>SceneFactoryに登録された実行時シーン名の一覧を返す。</summary>
    std::vector<std::string> GetRegisteredSceneNames() const;
    /// <summary>Scene Assetを開く。未保存変更の破棄可否は引数に従う。</summary>
    /// <returns>開く処理を開始できた場合はtrue。失敗理由はerrorMessageへ書く。</returns>
    bool OpenSceneAsset(const std::string& filename, bool discardUnsavedChanges, std::string& errorMessage);
    /// <summary>現在の変更を保存してから指定Scene Assetを開く。</summary>
    void SaveThenOpenSceneAsset(const std::string& filename);
    /// <summary>指定ファイルが現在編集中のScene Assetか返す。</summary>
    bool IsCurrentSceneAsset(const std::string& filename) const;
    /// <summary>新規Scene Asset作成ダイアログを開く。</summary>
    void OpenCreateSceneAssetDialog();
    /// <summary>メニューからのScene Asset切り替えを要求する。</summary>
    void RequestOpenSceneAssetFromMenu(const std::string& filename);

    /// <summary>選択中オブジェクトを複製し、Undo履歴へ登録する。</summary>
    void DuplicateSelected();
    /// <summary>選択中オブジェクトを削除し、Undo履歴へ登録する。</summary>
    void DeleteSelected();
    /// <summary>直前の編集を取り消す。</summary>
    void PerformUndo();
    /// <summary>取り消した編集を再適用する。</summary>
    void PerformRedo();
    /// <summary>選択モデルの各Meshを子オブジェクトに分割する。</summary>
    void SplitSelectedModelIntoMeshChildren();
    /// <summary>選択中オブジェクトを下の床面へ移動する。</summary>
    void DropToFloor();
    /// <summary>指定モデルをGame Viewカーソル位置に生成する。</summary>
    void InstantiateModelAtCursor(const std::string& modelName);
    /// <summary>指定プリセットをGame Viewカーソル位置に生成する。</summary>
    void InstantiatePresetAtCursor(const std::string& presetName);
    /// <summary>指定PrefabをGame Viewカーソル位置に生成する。</summary>
    void InstantiatePrefabAtCursor(const std::string& prefabName);
    /// <summary>指定粒子オブジェクトをGame Viewカーソル位置に生成する。</summary>
    void InstantiateParticleAtCursor(const std::string& particleName);
    /// <summary>指定プリセットを連続配置するブラシ操作を開始する。</summary>
    void StartPresetBrush(const std::string& presetName);
    /// <summary>プリセットブラシを終了し、プレビューを片付ける。</summary>
    void StopPresetBrush();
    /// <summary>Game Viewの生成メニューを開く。</summary>
    void OpenGameViewCreateContextMenu();
    /// <summary>Game Viewの生成メニューを描画し、選択された素材を受け取る。</summary>
    void DrawGameViewCreateContextMenu();
    /// <summary>Game Viewから指定位置でのPlay開始をControllerへ通知する関数を登録する。</summary>
    void SetPlayFromPositionCallback(std::function<void(const engine::Vector3&, const std::string&)> callback) {
        playFromPositionCallback_ = callback;
    }
    /// <summary>Game Viewでの配置先座標を指定オブジェクトから計算する。</summary>
    engine::Vector3 CalculateGameViewCreatePosition(const engine::Object3d* object);
    /// <summary>所有権を受け取り、配置確定まで単一オブジェクトをプレビューする。</summary>
    /// <remarks>キャンセル時はオブジェクトを破棄する。</remarks>
    void StartGameViewCreatePreview(std::unique_ptr<engine::Object3d> object, const std::string& label);
    /// <summary>所有権を受け取り、複数オブジェクトを一組として配置プレビューする。</summary>
    void StartGameViewCreatePreview(std::vector<std::unique_ptr<engine::Object3d>> objects, const std::string& label);
    /// <summary>Editorから単一オブジェクトを追加し、Undo履歴へ登録する。</summary>
    void AddEditorObject(std::unique_ptr<engine::Object3d> object, const std::string& label);
    /// <summary>Editorから複数オブジェクトを追加し、Undo履歴へまとめて登録する。</summary>
    void AddEditorObjects(std::vector<std::unique_ptr<engine::Object3d>> objects, const std::string& label);
    /// <summary>指定Prefabだけを編集する一時セッションを開始する。</summary>
    bool BeginPrefabEditSession(const std::string& prefabName);
    /// <summary>編集中Prefabを保存し、セッションを終了する。</summary>
    bool SavePrefabEditSession();
    /// <summary>Prefab編集を取り消して通常シーンへ戻る。</summary>
    void CancelPrefabEditSession();
    /// <summary>Prefab編集モードの有効性と変更状態を取得する。</summary>
    bool IsPrefabEditMode() const { return prefabEditMode_; }
    bool IsPrefabEditDirty() const { return prefabEditDirty_; }
    /// <summary>編集中Prefabの名前を返す。</summary>
    const std::string& GetPrefabEditName() const { return prefabEditName_; }
    /// <summary>指定オブジェクトが現在のPrefab編集対象に属するか返す。</summary>
    bool IsPrefabEditObject(const engine::Object3d* object) const;
    /// <summary>編集前後の比較に使うオブジェクト状態をJSONへ保存する。</summary>
    nlohmann::json CaptureObjectState(engine::Object3d* object) const;
    /// <summary>複数オブジェクトの変更前状態を一組として保存する。</summary>
    std::vector<ObjectStateSnapshot> CaptureObjectStates(const std::vector<engine::Object3d*>& objects) const;
    /// <summary>単一オブジェクトの編集をUndo履歴へ登録する。</summary>
    void RegisterObjectEdited(engine::Object3d* object, const std::string& label);
    /// <summary>保存済みの変更前状態を使って単一編集をUndo履歴へ登録する。</summary>
    void RegisterObjectEdited(engine::Object3d* object, const nlohmann::json& beforeState, const std::string& label);
    /// <summary>複数オブジェクトの編集をUndo履歴へまとめて登録する。</summary>
    void RegisterObjectsEdited(const std::vector<ObjectStateSnapshot>& beforeStates, const std::string& label);
    /// <summary>指定オブジェクトの保存カテゴリを未保存状態にする。</summary>
    void MarkDirtyForObject(engine::Object3d* object);
    /// <summary>指定保存カテゴリの未保存状態を立てる。</summary>
    void MarkDirty(SaveMode mode);
    /// <summary>指定保存カテゴリの未保存状態を解除する。</summary>
    void ClearDirty(SaveMode mode);
    /// <summary>指定保存カテゴリに未保存変更があるか返す。</summary>
    bool IsDirty(SaveMode mode) const;
    /// <summary>いずれかの保存カテゴリに未保存変更があるか返す。</summary>
    bool HasAnyDirty() const;
    /// <summary>未保存カテゴリをUI表示用の文章で返す。</summary>
    std::string GetDirtySummaryText() const;

    /// <summary>Game Viewの位置とサイズを各プレビューツールへ通知する。</summary>
    void SetGameViewRegion(const engine::Vector2& offset, const engine::Vector2& size) {
        gameViewOffset_ = offset;
        gameViewSize_ = size;
        animationWorkbench_.SetGameViewRegion(offset, size);
        textSpriteGenerator_.SetGameViewRegion(offset, size);
        modelOptimizerWindow_.SetGameViewRegion(offset, size);
    }
    /// <summary>ウィンドウ上のGame View矩形を設定する。</summary>
    void SetGameViewScreenRect(float left, float top, float right, float bottom) {
        gameViewScreenRectLeft_ = static_cast<int>(left);
        gameViewScreenRectTop_ = static_cast<int>(top);
        gameViewScreenRectRight_ = static_cast<int>(right);
        gameViewScreenRectBottom_ = static_cast<int>(bottom);
        hasGameViewScreenRect_ = gameViewScreenRectRight_ > gameViewScreenRectLeft_ && gameViewScreenRectBottom_ > gameViewScreenRectTop_;
    }
    /// <summary>有効なGame View矩形を整数座標で返す。</summary>
    /// <returns>矩形がまだ設定されていない場合はfalse。</returns>
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
    /// <summary>Game View上にマウスがあるか、プレビューツールへ通知する。</summary>
    void SetGameViewHovered(bool hovered) { isGameViewHovered_ = hovered; animationWorkbench_.SetGameViewHovered(hovered); }
    /// <summary>Game View内のマウス位置をプレビューツールへ通知する。</summary>
    void SetGameViewMousePos(const engine::Vector2& pos) { gameViewMousePos_ = pos; animationWorkbench_.SetGameViewMousePos(pos); }

    /// <summary>パスからファイル名だけを取り出し、現在の保存先名として設定する。</summary>
    void SetSceneFilename(const std::string& filepath) {
        std::string name = filepath;
        size_t pos = name.find_last_of("/\\");
        if (pos != std::string::npos) name = name.substr(pos + 1);
        strcpy_s(currentSceneFilename_, sizeof(currentSceneFilename_), name.c_str());
    }

    /// <summary>編集対象の3Dオブジェクトを選択する。</summary>
    void SetSelectedObject(engine::Object3d* obj);
    /// <summary>現在の選択をInspector表示へ同期する。</summary>
    void SyncObjectSelectionToInspector();
    /// <summary>指定オブジェクトを選択し、Scene Viewの視点を合わせる。</summary>
    bool FocusSceneObject(engine::Object3d* object);
    /// <summary>所有権を受け取り、一時的な配置プレビューを設定する。</summary>
    void SetPreviewObject(std::unique_ptr<engine::Object3d> obj, const std::string& label = "Place Preview Object");
    /// <summary>経路編集モードを切り替える。</summary>
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

    /// <summary>現在シーンに残っている選択Objectを返す。無効ならnullptr。</summary>
    engine::Object3d* GetSelectedObject3D() const { return IsObjectInCurrentScene(selectedObject_) ? selectedObject_ : nullptr; }
    /// <summary>現在シーンに残っている選択Objectを返す。無効ならnullptr。</summary>
    engine::Object3d* GetSelectedObject() const { return IsObjectInCurrentScene(selectedObject_) ? selectedObject_ : nullptr; }
    /// <summary>複数選択中のObject参照を返す。所有権は移さない。</summary>
    const std::vector<engine::Object3d*>& GetSelectedObjects() const { return selectedObjects_; }
    /// <summary>選択枠表示モードの数値を取得・設定する。</summary>
    int GetSelectionOverlayMode() const;
    void SetSelectionOverlayMode(int mode);
    /// <summary>指定オブジェクトが現在の選択に含まれるか返す。</summary>
    bool IsObjectSelected(const engine::Object3d* object) const;
    /// <summary>複数選択中のObject数を返す。</summary>
    std::size_t GetSelectedObjectCount() const { return selectedObjects_.size(); }
    /// <summary>全Objectの選択を解除する。</summary>
    void ClearObjectSelection();
    /// <summary>指定Objectを現在の選択へ追加する。</summary>
    void AddSelectedObject(engine::Object3d* object);
    /// <summary>指定Objectの選択・解除を切り替える。</summary>
    void ToggleSelectedObject(engine::Object3d* object);
    /// <summary>借用しているSceneManagerを返す。</summary>
    SceneManager* GetSceneManager() const { return sceneManager_; }
    /// <summary>借用するSceneWorkspaceを設定・取得する。</summary>
    void SetSceneWorkspace(SceneWorkspace* workspace) { sceneWorkspace_ = workspace; }
    SceneWorkspace* GetSceneWorkspace() const { return sceneWorkspace_; }

    /// <summary>各サブEditorの借用参照を返す。所有権は移さない。</summary>
    LightEditor* GetLightEditor() const { return lightEditor_; }
    PostEffectEditor* GetPostEffectEditor() const { return postEffectEditor_; }
    SpriteDebugEditor* GetSpriteDebugEditor() const { return spriteDebugEditor_; }
    GPUParticleEditor* GetGPUParticleEditor() const { return gpuParticleEditor_; }
    VFXSequencerEditor* GetVFXSequencerEditor() const { return vfxSequencerEditor_; }
    ParticleEditor* GetParticleEditor() const { return particleEditor_; }
    GhostRecorder* GetGhostRecorder() const { return ghostRecorder_; }
    GhostDirector* GetGhostDirector() const { return ghostDirector_; }

    /// <summary>ファイル名と検索文字列の編集バッファーと容量を返す。</summary>
    char* GetCurrentSceneFilenameBuffer() { return currentSceneFilename_; }
    size_t GetSceneFilenameBufferSize() const { return sizeof(currentSceneFilename_); }
    char* GetSearchFilterBuffer() { return searchFilter_; }
    size_t GetSearchFilterBufferSize() const { return sizeof(searchFilter_); }

    /// <summary>Collider表示の切り替え値への編集用ポインターを返す。</summary>
    bool* GetDrawCollidersPtr() { return &drawColliders_; }
#ifdef USE_IMGUI
    /// <summary>Scene Icon、選択枠、不可視Object枠の表示切り替え値をそれぞれ返す。</summary>
    bool* GetDrawSceneIconsPtr() { return &drawSceneIcons_; }
    bool* GetDrawSelectionBoundsPtr() { return &drawSelectionBounds_; }
    bool* GetDrawInvisibleBoundsPtr() { return &drawInvisibleBounds_; }
private:
    bool drawSceneIcons_ = true;
    bool drawSelectionBounds_ = true;
    bool drawInvisibleBounds_ = true;
public:
#endif
    /// <summary>経路編集モードが有効か返す。</summary>
    bool GetIsPathEditMode() const { return isPathEditMode_; }

    /// <summary>VFX系サブEditorの借用参照を取得・設定する。所有権は移さない。</summary>
    MeshEffectEditor* GetMeshEffectEditor() const { return meshEffectEditor_; }
    void SetMeshEffectEditor(MeshEffectEditor* editor) { meshEffectEditor_ = editor; }
    DebrisEffectEditor* GetDebrisEffectEditor() const { return debrisEffectEditor_; }
    TrailEmitterEditor* GetTrailEmitterEditor() const { return trailEmitterEditor_; }
    /// <summary>DebugEditorが所有する各ツールウィンドウへの参照を返す。</summary>
    SceneInventoryWindow* GetSceneInventoryWindow() { return &sceneInventoryWindow_; }
    MaterialPreviewBoard* GetMaterialPreviewBoard() { return &materialPreviewBoard_; }
    EffectPreviewStage* GetEffectPreviewStage() { return EffectPreviewStage::GetInstance(); }
    EnemyAttackPreviewWindow* GetEnemyAttackPreviewWindow() { return &enemyAttackPreviewWindow_; }
    AnimationWorkbench* GetAnimationWorkbench() { return &animationWorkbench_; }
    AnimatorControllerEditor* GetAnimatorControllerEditor() { return &animatorControllerEditor_; }
    TextSpriteGenerator* GetTextSpriteGenerator() { return &textSpriteGenerator_; }
    ModelOptimizerWindow* GetModelOptimizerWindow() { return &modelOptimizerWindow_; }
    AssetAuditWindow* GetAssetAuditWindow() { return &assetAuditWindow_; }
    StatusTuningWindow* GetStatusTuningWindow() { return &statusTuningWindow_; }
    GameDataDebugEditor* GetGameDataDebugEditor() { return &gameDataDebugEditor_; }
    AudioSettingsWindow* GetAudioSettingsWindow() { return &audioSettingsWindow_; }
    ExecutablePackageWindow* GetExecutablePackageWindow() { return &executablePackageWindow_; }
    CaptureToolWindow* GetCaptureToolWindow() { return &captureToolWindow_; }
    ProjectWindow* GetProjectWindow() { return &projectWindow_; }
    /// <summary>イベントIDの表示切り替え値への編集用ポインターを返す。</summary>
    bool* GetDrawEventIDsPtr() { return &drawEventIDs_; }

private:
        // モデルやプリセット生成時の配置結果をまとめ、生成後の選択やUndo登録に使います。
struct PlacementResult {
        engine::Vector3 position = { 0.0f, 0.0f, 0.0f };
        engine::Vector3 contactPosition = { 0.0f, 0.0f, 0.0f };
        engine::Vector3 normal = { 0.0f, 1.0f, 0.0f };
        bool found = false;
        bool hasSurface = false;
    };

    enum class PresetRootName { DisplayName, PathLeaf };
    /// <summary>AABBの各軸の最小値と最大値が有効か判定する。</summary>
    static bool IsValidAabb(const AABB& aabb);
    /// <summary>AABBの中心座標を返す。</summary>
    static engine::Vector3 GetAabbCenter(const AABB& aabb);
    /// <summary>シーンと予約済み名の双方で重複しないプリセットObject名を作る。</summary>
    static std::string MakeUniquePresetObjectName(BaseScene* scene, const std::string& baseName,
        const std::vector<std::string>& reservedNames);
    /// <summary>生成したプリセットObject一式へ重複しない名前を割り当てる。</summary>
    static void AssignPresetInstanceNames(BaseScene* scene, const std::string& presetName,
        std::vector<std::unique_ptr<engine::Object3d>>& objects, PresetRootName rootNameSource);

    /// <summary>Game Viewのマウス位置からワールド空間のRayを作る。</summary>
    engine::Ray ScreenPointToRay(const engine::Vector2& mousePos);
    /// <summary>Rayと配置用平面の交点を求める。</summary>
    /// <returns>交点がある場合はtrue。座標はintersectOutへ書く。</returns>
    bool IntersectRayPlane(const engine::Ray& ray, engine::Vector3& intersectOut);
    /// <summary>Game Viewのマウス位置から床・面・グリッドを考慮した配置を計算する。</summary>
    PlacementResult CalculateGameViewPlacement(const engine::Object3d* object, const engine::Vector2& mousePos, bool useGridSnap = true);
    /// <summary>計算済みの配置結果をObjectの位置と必要な向きへ反映する。</summary>
    void ApplyGameViewPlacement(engine::Object3d* object, const PlacementResult& placement, bool alignToSurface);
    /// <summary>カーソル移動に合わせて配置プレビューを更新する。</summary>
    void UpdatePreviewPlacement();
    /// <summary>配置プレビューを確定し、シーンとUndo履歴へ登録する。</summary>
    void ConfirmPreviewPlacement();
    /// <summary>配置プレビューを中止し、一時Objectを破棄する。</summary>
    void CancelPreviewPlacement();
    /// <summary>プリセットブラシのカーソル位置と押下状態を更新する。</summary>
    void UpdatePresetBrush();
    /// <summary>現在のブラシ設定から配置前Objectを作り直す。</summary>
    void RebuildPresetBrushPreview();
    /// <summary>現在位置へブラシのプリセットを配置する。</summary>
    void StampPresetBrush();
    /// <summary>プリセットブラシの配置範囲を画面に重ねて描画する。</summary>
    void DrawPresetBrushOverlay();
    /// <summary>ブラシで生成したObjectに配置前の表示設定を適用する。</summary>
    void ApplyBrushPreviewVisual(engine::Object3d* object);
    /// <summary>配置前Objectにプレビュー用の表示設定を適用する。</summary>
    void ApplyPreviewVisual(engine::Object3d* object);
    /// <summary>プレビュー用に変更したObjectの表示設定を元へ戻す。</summary>
    void RestorePreviewVisual(engine::Object3d* object);
    /// <summary>プレビューObjectの輪郭を指定インスタンス上限内で描画する。</summary>
    void DrawPreviewWire(ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit);
    /// <summary>配置候補位置を示すマーカーを描画する。</summary>
    void DrawPreviewMarker();
    /// <summary>ワールド座標をGame View上の画面座標へ変換する。</summary>
    engine::Vector3 WorldToScreen(const engine::Vector3& worldPos);
    /// <summary>シーン内の3Dアイコンを画面に描画する。</summary>
    void Draw3DIcons();
    /// <summary>イベントIDを対象Objectの近くに表示する。</summary>
    void DrawEventIDOverlay();
    /// <summary>保存対象のプレビューを描画する。</summary>
    void DrawSavePreview();
    /// <summary>Editorのクラッシュ復旧セッションを用意する。</summary>
    void InitializeCrashRecovery();
    /// <summary>自動保存と復旧セッションの状態を更新する。</summary>
    void UpdateCrashRecovery(float deltaTime);
    /// <summary>正常終了を復旧記録へ残し、一時状態を片付ける。</summary>
    void FinalizeCrashRecovery();
    /// <summary>現在の未保存状態を復旧用ドラフトへ保存する。</summary>
    void SaveCrashRecoveryDraft();
    /// <summary>終了状態、Dirty状態、対象ファイルを復旧セッションへ書く。</summary>
    void WriteCrashRecoverySession(bool cleanExit, bool dirty, const nlohmann::json& files = nlohmann::json::array());
    /// <summary>前回の異常終了から復旧可能なドラフトを探す。</summary>
    void LoadCrashRecoveryCandidate();
    /// <summary>復旧候補がある場合の選択ダイアログを描画する。</summary>
    void DrawCrashRecoveryPrompt();
    /// <summary>復旧用ドラフトを現在の編集状態へ戻す。</summary>
    /// <returns>復元に成功した場合はtrue。</returns>
    bool RestoreCrashRecoveryDraft();
    /// <summary>復元・破棄などの選択を復旧候補に反映する。</summary>
    void ResolveCrashRecoveryCandidate(const std::string& resolution);
    /// <summary>DDSキャッシュ生成の完了通知を読み取る。</summary>
    void PollDDSCacheNotifications();
    /// <summary>保存カテゴリに応じたプレビューの見出しを返す。</summary>
    std::string MakeSavePreviewTitle(SaveMode mode) const;
    /// <summary>指定Objectが現在のシーンに存在するか返す。</summary>
    bool IsObjectInCurrentScene(const engine::Object3d* object) const;
    /// <summary>削除済みの単一選択Objectを選択状態から外す。</summary>
    void ClearInvalidSelectedObject();
    /// <summary>削除済みObjectを複数選択の一覧から取り除く。</summary>
    void PruneInvalidSelectedObjects();
    /// <summary>Game Viewの位置に重なるObjectを探す。</summary>
    engine::Object3d* PickObjectAtGameViewPos(const engine::Vector2& mousePos);
    /// <summary>Game Viewの矩形範囲内にあるObjectを選択する。</summary>
    /// <param name="additive">既存の選択を維持して追加する場合はtrue。</param>
    void SelectObjectsInGameViewRect(const engine::Vector2& start, const engine::Vector2& end, bool additive);
    /// <summary>ドラッグ選択中の矩形を画面に重ねて描画する。</summary>
    void DrawSelectionRectangleOverlay();
    /// <summary>選択中Objectの境界枠を画面に重ねて描画する。</summary>
    void DrawSelectedObjectBoundsOverlay();
    /// <summary>選択表示に使うObjectのワールド位置を返す。</summary>
    engine::Vector3 GetObjectWorldPositionForSelection(engine::Object3d* object);
    /// <summary>JSONに保存した状態を指定Objectへ反映する。</summary>
    void ApplyObjectState(engine::Object3d* object, const nlohmann::json& state);
    /// <summary>名前が一致するシーンObjectを探す。</summary>
    engine::Object3d* FindObjectByName(const std::string& name) const;
    /// <summary>永続GUIDが一致するシーンObjectを探す。</summary>
    engine::Object3d* FindObjectByPersistentGuid(const std::string& guid) const;
    /// <summary>保存済み状態からObjectを生成し、シーンへ追加する。</summary>
    engine::Object3d* AddObjectFromState(const nlohmann::json& state);
    /// <summary>指定Objectを即時にシーンから外し、所有権を返す。</summary>
    std::unique_ptr<engine::Object3d> RemoveObjectImmediate(engine::Object3d* object);
    /// <summary>Prefab編集を終了し、必要ならUndo履歴を消去する。</summary>
    void EndPrefabEditSession(bool clearTransactions);
    /// <summary>シーン切り替え時にPrefab編集の一時状態を解除する。</summary>
    void ResetPrefabEditSessionForSceneChange();
    /// <summary>Inspector編集前の状態と現在状態を比較して履歴へ登録する。</summary>
    void TrackInspectorEdit(const std::vector<ObjectStateSnapshot>& beforeStates);
    /// <summary>保存カテゴリ名に対応するDirty状態を立てる。</summary>
    void MarkDirtyForCategory(const std::string& category);

private:
    // コアシステムへの参照。
    SceneManager* sceneManager_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;
    engine::Math math_;

    // 選択、配置プレビュー、プリセットブラシの状態。
    enum class SelectionOverlayMode {
        Compact = 0,
        Detailed,
        Hidden
    };

    engine::Object3d* selectedObject_ = nullptr;
    std::vector<engine::Object3d*> selectedObjects_;
    SelectionOverlayMode selectionOverlayMode_ = SelectionOverlayMode::Compact;
    std::vector<ObjectStateSnapshot> groupTransformStartStates_;
    bool isSelectionRectDragging_ = false;
    bool isSelectionRectReady_ = false;
    engine::Vector2 selectionRectStart_ = { 0.0f, 0.0f };
    engine::Vector2 selectionRectEnd_ = { 0.0f, 0.0f };
    std::unique_ptr<engine::Object3d> previewObject_ = nullptr;
    std::vector<std::unique_ptr<engine::Object3d>> previewChildObjects_;
    std::vector<std::unique_ptr<engine::Object3d>> brushPreviewObjects_;
    bool isPresetBrushMode_ = false;
    std::string brushPresetName_;
    float brushSpacing_ = 2.0f;
    bool hasLastBrushStamp_ = false;
    engine::Vector3 lastBrushStampPosition_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector4 previewObjectOriginalColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
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
    engine::Vector3 previewPlacementContactPosition_ = { 0.0f, 0.0f, 0.0f };
    bool hasPreviewPlacementContact_ = false;
    std::string previewCreateCommandLabel_ = "Place Preview Object";

        // 生成プレビュー中だけ一時的に差し替える表示状態を保持します。
struct PreviewVisualState {
        std::string className;
        engine::Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        BlendMode blendMode = BlendMode::kNormal;
        int32_t materialType = 0;
        int32_t selectedLighting = 2;
        int32_t enableLighting = 1;
        float emissive = 1.0f;
    };
    std::unordered_map<engine::Object3d*, PreviewVisualState> previewVisualStates_;
    BaseScene* lastUpdatedScene_ = nullptr;

    // Prefab Modeでは現在Sceneを一時的に隠し、Prefab階層だけを同じInspector/Gizmoで編集します。
    bool prefabEditMode_ = false;
    bool prefabEditDirty_ = false;
    std::string prefabEditName_;
    engine::Object3d* prefabEditRoot_ = nullptr;
    BaseScene* prefabEditScene_ = nullptr;
    std::unordered_map<engine::Object3d*, bool> prefabEditPreviousVisibility_;

    bool drawColliders_ = false;
    bool drawEventIDs_ = false;
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
    engine::Vector2 gameViewMousePos_ = { 0, 0 };
    engine::Vector2 gameViewSize_ = { 1266, 530 };
    engine::Vector2 gameViewOffset_ = { 0, 0 };
    int gameViewScreenRectLeft_ = 0;
    int gameViewScreenRectTop_ = 0;
    int gameViewScreenRectRight_ = 0;
    int gameViewScreenRectBottom_ = 0;
    bool hasGameViewScreenRect_ = false;
    bool isGameViewHovered_ = false;
    engine::Vector2 gameViewCreateMenuMousePos_ = { 0, 0 };
    engine::Vector2 gameViewCreateMenuScreenPos_ = { 0, 0 };
    bool requestGameViewCreateMenu_ = false;

    // 保存対象ごとのDirty状態。
    std::function<void(const engine::Vector3&, const std::string&)> playFromPositionCallback_;
    bool dirtyPlayer_ = false;
    bool dirtyEnemy_ = false;
    bool dirtyObject_ = false;
    bool dirtyCamera_ = false;
    SaveMode pendingSaveMode_ = SaveMode::All;
    bool pendingSaveIsSingleObject_ = false;
    std::string pendingSceneAssetOpenAfterSave_;

    // Undo/Redoコマンド。
        // Undo/Redoで扱う編集操作の種類を表します。
enum class EditorCommandType {
        ObjectCreated,
        ObjectDeleted,
        ObjectEdited
    };

    // Object 編集の前後 JSON と名前。Undo は before、Redo は after を使って編集状態を戻す。
    struct EditorCommand {
        EditorCommandType type = EditorCommandType::ObjectEdited;
        std::string label;
        std::string beforeName;
        std::string afterName;
        nlohmann::json beforeState;
        nlohmann::json afterState;
    };

    /// <summary>編集操作をUndo・Redoの履歴へ登録する。</summary>
    void RegisterCommand(const EditorCommand& command);
    /// <summary>編集操作の前後状態をObjectへ適用する。</summary>
    /// <param name="undo">変更前状態へ戻す場合はtrue。</param>
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

    SceneWorkspace* sceneWorkspace_ = nullptr;
    // DebugEditorが所有するサブウィンドウ。
    HierarchyWindow hierarchyWindow_;
    ProjectWindow projectWindow_;
    InspectorWindow inspectorWindow_;
    SceneSerializer serializer_;
    PrimitiveDrawer primitiveDrawer_;
    SceneInventoryWindow sceneInventoryWindow_;
    MaterialPreviewBoard materialPreviewBoard_;
    EnemyAttackPreviewWindow enemyAttackPreviewWindow_;
    AnimationWorkbench animationWorkbench_;
    AnimatorControllerEditor animatorControllerEditor_;
    SceneSavePreview sceneSavePreview_;
    TextSpriteGenerator textSpriteGenerator_;
    ModelOptimizerWindow modelOptimizerWindow_;
    AssetAuditWindow assetAuditWindow_;
    StatusTuningWindow statusTuningWindow_;
    GameDataDebugEditor gameDataDebugEditor_;
    AudioSettingsWindow audioSettingsWindow_;
    ExecutablePackageWindow executablePackageWindow_;
    CaptureToolWindow captureToolWindow_;

    // 外部所有のVFX系サブエディタ。
    MeshEffectEditor* meshEffectEditor_ = nullptr;
    DebrisEffectEditor* debrisEffectEditor_ = nullptr;
    TrailEmitterEditor* trailEmitterEditor_ = nullptr;
};
