#pragma once

#ifdef USE_IMGUI

#include <memory>
#include <string>

class DebugEditor;
class DebrisEffectEditor;
class DirectXCommon;
class EngineManualWindow;
class GhostDirector;
class GhostRecorder;
class GPUParticleEditor;
struct ID3D12Resource;
struct ID3D12GraphicsCommandList;
class MeshEffectEditor;
class ParticleEditor;
class PostEffectEditor;
class SceneManager;
class SpriteDebugEditor;
class TrailEmitterEditor;
class VFXSequencerEditor;

// EditorFrameStateは、Sprite操作やギズモ操作などカメラ入力へ影響するUI状態をまとめます。
struct EditorFrameState {
	bool spriteEditorBusy = false;
	bool gizmoBusy = false;
};

// GameViewAreaは、エディタ内ゲームビューの画面上の位置とサイズを表します。
struct GameViewArea {
	float screenX = 0.0f;
	float screenY = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

// GameEditorControllerは、DebugEditorや各種ツールウィンドウをまとめてゲーム本体と接続します。
class GameEditorController {
public:
	GameEditorController();
	~GameEditorController();

	    // 各エディタツールを生成し、シーン管理とDirectX基盤へ接続します。
void Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon);
	void Finalize();

	    // ImGuiフレームやエディタ内部状態のフレーム開始処理を行います。
void BeginFrame();
	EditorFrameState DrawGameView(SceneManager* sceneManager, bool isPlaying);
	    // 再生停止やシーン切り替えなどのメインメニューを描画します。
void DrawMainMenuBar(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName);
	    // 各エディタツールの更新処理をまとめて呼び出します。
void UpdateTools(float deltaTime, bool isPlaying, float timeScale);
	    // Inspector、Project、VFX、ステータスなどのツールウィンドウを描画します。
void DrawToolWindows(
		float& timeScale,
		float sceneUpdateTimeMs,
		float cpuCmdTimeMs,
		float drawTimeMs,
		float* updateTimeHistory,
		float* drawTimeHistory,
		int timeHistoryIndex);

	void CapturePendingThumbnails();
	void DrawScenePreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);
	void DrawSceneDebug(ID3D12GraphicsCommandList* commandList);
	void DrawBackBufferUi();
	void EndFrame();
	    // UI操作中や再生中の状態に合わせてカメラ入力を抑制します。
void ApplyCameraInputState(const EditorFrameState& frameState, bool isPlaying);	void ApplyCameraOverrides();
	void SaveAllEditors();
	void RequestExit();

	// ポートフォリオ用に、エディタUIを隠してゲーム画面だけを表示する撮影モードを切り替える。
	void SetPortfolioCaptureMode(bool enabled);
	bool IsPortfolioCaptureMode() const { return portfolioCaptureMode_; }

	DebugEditor* GetDebugEditor() const { return debugEditor_.get(); }
private:
	void SetupDefaultDockspace();
	void HandleGameViewDropTargets(SceneManager* sceneManager, const GameViewArea& area);
	void DrawGhostPreview(bool isPlaying, const GameViewArea& area);
	void RequestPlay(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName);
	void StartPlay(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName);
	void DrawUnsavedPlayConfirmPopup(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName);
	void DrawUnsavedExitConfirmPopup();
	    // 再生や終了前に確認すべき未保存変更があるか調べます。
bool HasUnsavedEditorChanges() const;
	void ClearSceneBoundEditorState();
	void DrawStatusWindow(
		float& timeScale,
		float sceneUpdateTimeMs,
		float cpuCmdTimeMs,
		float drawTimeMs,
		float* updateTimeHistory,
		float* drawTimeHistory,
		int timeHistoryIndex);

private:
	std::unique_ptr<PostEffectEditor> postEffectEditor_;
	std::unique_ptr<DebugEditor> debugEditor_;
	std::unique_ptr<SpriteDebugEditor> spriteDebugEditor_;
	std::unique_ptr<ParticleEditor> particleEditor_;
	std::unique_ptr<GhostRecorder> ghostRecorder_;
	std::unique_ptr<GhostDirector> ghostDirector_;
	std::unique_ptr<GPUParticleEditor> gpuParticleEditor_;
	std::unique_ptr<VFXSequencerEditor> vfxSequencerEditor_;
	std::unique_ptr<MeshEffectEditor> meshEffectEditor_;
	std::unique_ptr<DebrisEffectEditor> debrisEffectEditor_;
	std::unique_ptr<TrailEmitterEditor> trailEmitterEditor_;
	std::unique_ptr<EngineManualWindow> engineManualWindow_;	bool showDebugWindows_ = true;
	bool showDebugConsole_ = true;
	bool showTimeController_ = true;
	bool showBossDebug_ = false;
	bool portfolioCaptureMode_ = false;
	bool previousPlayingState_ = false;
	bool dockspaceInitialized_ = false;
	bool openUnsavedPlayConfirm_ = false;	bool openUnsavedExitConfirm_ = false;
};

#endif
