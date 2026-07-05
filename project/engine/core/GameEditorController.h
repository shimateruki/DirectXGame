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

struct EditorFrameState {
	bool spriteEditorBusy = false;
	bool gizmoBusy = false;
};

struct GameViewArea {
	float screenX = 0.0f;
	float screenY = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

class GameEditorController {
public:
	GameEditorController();
	~GameEditorController();

	void Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon);
	void Finalize();

	void BeginFrame();
	EditorFrameState DrawGameView(SceneManager* sceneManager, bool isPlaying);
	void DrawMainMenuBar(SceneManager* sceneManager, bool& isPlaying, const std::string& currentSceneName);
	void UpdateTools(float deltaTime, bool isPlaying, float timeScale);
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
