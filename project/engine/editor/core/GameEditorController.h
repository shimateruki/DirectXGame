#pragma once

#ifdef USE_IMGUI

#include "SceneLoadContext.h"
#include "Transform.h"

#include <memory>
#include <cstdint>
#include <string>

class AssetReferenceExplorer;
class DebugEditor;
class EditorQuickSearch;
class DebrisEffectEditor;
class DirectXCommon;
class EngineManualWindow;
class GhostDirector;
class GhostRecorder;
class GpuParticleEditor;
struct ID3D12Resource;
struct ID3D12GraphicsCommandList;
class MeshEffectEditor;
class ParticleEditor;
class PostEffectEditor;
class PlayModeSnapshot;
class SelectedActorSession;
class SceneManager;
class SceneWorkspace;
class PlayModeChangeTracker;
class SpriteDebugEditor;
class TrailEmitterEditor;
class VfxSequencerEditor;

// EditorFrameStateは、Sprite操作やギズモ操作などカメラ入力へ影響するUI状態をまとめます。
// 関連：GameEditorController（この型を使う機能の本体）
struct EditorFrameState {
	bool spriteEditorBusy = false; // Sprite操作中でカメラ入力を抑えるか。
	bool gizmoBusy = false; // ギズモ操作中でカメラ入力を抑えるか。
};

// GameViewAreaは、エディタ内ゲームビューの画面上の位置とサイズを表します。
// 関連：GameEditorController（この型を使う機能の本体）
struct GameViewArea {
	// ImGuiの画面座標とピクセル寸法。ゲーム内のワールド座標とは区別する。
	float screenX = 0.0f;
	float screenY = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

// 担当：ゲーム本体とEditor各ツールを接続し、編集・Play切替と画面配置を統括する。
// 関連：Game（呼出元）、DebugEditor（編集操作）、PlayModeSnapshot（停止時復元）
// 主な入口：Initialize / BeginFrame / UpdateTools / DrawGameView / EndFrame / Finalize
class GameEditorController {
public:
	/// <summary>Editor各ツールの初期状態を作る。</summary>
	GameEditorController();

	/// <summary>Editorが保持するツールと一時状態を破棄する。</summary>
	~GameEditorController();

	/// <summary>各Editorツールを生成し、シーン管理とDirectX基盤へ接続する。</summary>
void Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon);

	/// <summary>Editorツールを終了して接続を外す。</summary>
	void Finalize();

	/// <summary>ImGuiフレームとEditor内部状態のフレーム開始処理を行う。</summary>
void BeginFrame();

	/// <summary>ゲームビューを描画し、カメラ入力に影響するUI状態を返す。</summary>
	EditorFrameState DrawGameView(SceneManager* sceneManager, bool isPlaying);

	/// <summary>再生停止やシーン切り替えなどのメインメニューを描画する。</summary>
void DrawMainMenuBar(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName);

	/// <summary>各Editorツールの更新処理をまとめて呼び出す。</summary>
void UpdateTools(float deltaTime, bool isPlaying, float timeScale);
    // 選択対象の確認ツールを実時間の経過秒数で更新する。
    void UpdateActorTools(float deltaTime);

    /// <summary>EditorのPlay中のシミュレーション停止状態。</summary>
    bool IsSimulationPaused() const { return simulationPaused_; }

	/// <summary>Inspector、Project、VFX、ステータスなどのツールウィンドウを描画する。</summary>
void DrawToolWindows(
		float& timeScale,
		float sceneUpdateTimeMs,
		float cpuCmdTimeMs,
		float drawTimeMs,
		float* updateTimeHistory,
		float* drawTimeHistory,
		int timeHistoryIndex);

	/// <summary>保留中の素材サムネイルを撮影する。</summary>
	void CapturePendingThumbnails();

	/// <summary>撮影済みのHUD用ポートレート画像を出力する。</summary>
	void ExportCapturedHudPortraits();

	/// <summary>編集対象シーンの3Dプレビューを描画する。</summary>
	void DrawScenePreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

	/// <summary>シーン上のギズモなどのデバッグ表示を描画する。</summary>
	void DrawSceneDebug(ID3D12GraphicsCommandList* commandList);

	/// <summary>バックバッファ上にEditorのUIを描画する。</summary>
	void DrawBackBufferUi();

	/// <summary>Editorの一フレーム処理を終了する。</summary>
	void EndFrame();

	/// <summary>UI操作と再生状態に合わせてカメラ入力の受付を切り替える。</summary>
    void ApplyCameraInputState(const EditorFrameState& frameState, bool isPlaying);

    /// <summary>ツールが要求する一時カメラを、通常カメラの更新後に反映する。</summary>
    void ApplyCameraOverrides();

	/// <summary>保存に対応するEditorツールの変更をまとめて保存する。</summary>
	void SaveAllEditors();

	/// <summary>未保存変更の確認を含むEditor終了処理を要求する。</summary>
	void RequestExit();

	/// <summary>Editor UIを隠し、ゲーム画面だけを撮るモードを切り替える。</summary>
	void SetPortfolioCaptureMode(bool enabled);

	/// <summary>ポートフォリオ撮影モードが有効か返す。</summary>
	bool IsPortfolioCaptureMode() const { return portfolioCaptureMode_; }

	/// <summary>登録済みEditor CommandをIDから実行する。</summary>
	/// <returns>該当コマンドを実行できた場合はtrue。</returns>
	bool ExecuteEditorCommand(const char* commandId);

	/// <summary>統合されたDebugEditorを返す。所有権は移さない。</summary>
	DebugEditor* GetDebugEditor() const { return debugEditor_.get(); }

private:
    enum class WorkspacePreset : int {
        Vfx = 0,
        Animation,
    };

    /// <summary>指定した作業領域の窓配置を適用する。</summary>
    void ApplyWorkspacePreset(WorkspacePreset preset);

    /// <summary>現在の窓配置を指定プリセットへ保存する。</summary>
    void SaveWorkspacePreset(WorkspacePreset preset);

    /// <summary>保留していた窓配置の切り替えを処理する。</summary>
    void ProcessPendingWorkspaceLayout();

    /// <summary>作業領域プリセットの選択メニューを描画する。</summary>
    void DrawWorkspaceMenu();

	/// <summary>メニューやショートカットから使うEditor Commandを登録する。</summary>
	void RegisterEditorCommands();

    /// <summary>各Editorツールを共通の更新・描画経路へ登録する。</summary>
    void RegisterEditorTools();

    bool ghostPathVisible_ = true;
    bool ghostModelVisible_ = true;
    bool sceneDirectionVisible_ = true;

	/// <summary>登録済みコマンドをメニュー項目として描画し、押されたら実行する。</summary>
	bool DrawEditorCommandMenuItem(const char* commandId, const char* labelOverride = nullptr, bool selected = false);

	/// <summary>現在のモードでシーンを編集できるか返す。</summary>
	bool CanEditScene() const;

	/// <summary>選択中オブジェクトを編集できるか返す。</summary>
	bool CanEditSelectedObject() const;

	/// <summary>ゲーム再生を停止し、Editor側の状態へ戻す。</summary>
	void StopPlay();

	/// <summary>再生開始前に開いていたシーンを読み直す。</summary>
	void ReloadPlayOriginScene();

	/// <summary>Editorの既定Dockspaceを初期配置する。</summary>
	void SetupDefaultDockspace();

	/// <summary>ゲームビューへドロップされた素材を画面範囲に基づいて処理する。</summary>
	void HandleGameViewDropTargets(SceneManager* sceneManager, const GameViewArea& area);

	/// <summary>ゲームビュー内にGhostのプレビューを描画する。</summary>
	void DrawGhostPreview(bool isPlaying, const GameViewArea& area);

	/// <summary>未保存変更の確認を含むゲーム再生を要求する。</summary>
	void RequestPlay(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName);

	/// <summary>ゲーム再生を開始し、再生元シーンを記録する。</summary>
	void StartPlay(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName);

	/// <summary>指定位置からの再生開始を保留する。</summary>
	void RequestPlayFromPosition(const engine::Vector3& position, const std::string& label);

	/// <summary>保留中の再生開始位置をプレイヤーへ適用する。</summary>
	void ApplyPendingPlayStartPosition();

	/// <summary>未保存変更がある場合の終了確認ダイアログを描画する。</summary>
	void DrawUnsavedExitConfirmPopup();

	/// <summary>再生や終了前に確認すべき未保存変更があるか返す。</summary>
bool HasUnsavedEditorChanges() const;

	/// <summary>シーンに結び付いたEditorの選択やプレビュー状態を解除する。</summary>
	void ClearSceneBoundEditorState();

	/// <summary>フレーム時間と描画負荷を示すステータス窓を描画する。</summary>
	void DrawStatusWindow(
		float& timeScale,
		float sceneUpdateTimeMs,
		float cpuCmdTimeMs,
		float drawTimeMs,
		float* updateTimeHistory,
		float* drawTimeHistory,
		int timeHistoryIndex);

private:
	// 外部のシーン管理と再生状態を借用する。各ツールは下の unique_ptr が所有する。
	SceneManager* sceneManager_ = nullptr;
	bool* activePlayState_ = nullptr; // Game側のPlayフラグへの借用参照。
	std::string activeSceneName_; // 現在Editorへ通知したシーン名。

	std::unique_ptr<PostEffectEditor> postEffectEditor_;
	std::unique_ptr<DebugEditor> debugEditor_;
    std::unique_ptr<EditorQuickSearch> editorQuickSearch_;
    std::unique_ptr<SceneWorkspace> sceneWorkspace_;
	std::unique_ptr<SpriteDebugEditor> spriteDebugEditor_;
    std::unique_ptr<AssetReferenceExplorer> assetReferenceExplorer_;
    std::unique_ptr<PlayModeChangeTracker> playModeChangeTracker_;

	std::unique_ptr<ParticleEditor> particleEditor_;

	std::unique_ptr<GhostRecorder> ghostRecorder_;
	std::unique_ptr<GhostDirector> ghostDirector_;

	std::unique_ptr<GpuParticleEditor> gpuParticleEditor_;
	std::unique_ptr<VfxSequencerEditor> vfxSequencerEditor_;
	std::unique_ptr<MeshEffectEditor> meshEffectEditor_;
	std::unique_ptr<DebrisEffectEditor> debrisEffectEditor_;
	std::unique_ptr<TrailEmitterEditor> trailEmitterEditor_;

	std::unique_ptr<EngineManualWindow> engineManualWindow_;
	bool showDebugWindows_ = true;

	std::unique_ptr<PlayModeSnapshot> playModeSnapshot_;
    std::unique_ptr<SelectedActorSession> actorSession_;
	bool simulationPaused_ = false; // 確認ツールによって通常シミュレーションを停止中か。

	bool showEnemyAttackTimeline_ = false;
	bool showEffectPreviewTimeline_ = false;
    bool showAnimationTimeline_ = false;
    bool showCinematicTimeline_ = false;

	bool showDebugConsole_ = false;
	bool showTimeController_ = true;

	bool portfolioCaptureMode_ = false;

	bool pendingPlayOriginReload_ = false; // Play開始元のシーン再読込待ち。
	bool sceneTransitionStateCleared_ = false; // 遷移前のEditor参照を解除済みか。
	uint64_t playOriginSceneGeneration_ = 0; // Play開始元のシーン世代。
	std::string playOriginSceneName_; // Play開始元のシーン名。
	SceneLoadContext playOriginSceneLoadContext_; // Play停止時に戻す読込設定。

	bool dockspaceInitialized_ = false;
	bool openUnsavedExitConfirm_ = false;

    int activeWorkspacePreset_ = -1; // 選択中の配置Preset。-1は未選択。
    int pendingWorkspaceLayout_ = -1; // 次回適用する配置。-1は予約なし。
    std::string workspaceStatus_;

    bool hasPendingPlayStartPosition_ = false; // 開始位置の上書き予約があるか。
    engine::Vector3 pendingPlayStartPosition_ = engine::Vector3::ZERO; // 次のPlay開始で使うワールド位置。
    std::string pendingPlayStartLabel_; // 開始位置の表示用ラベル。
};

#endif
