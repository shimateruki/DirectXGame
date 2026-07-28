#pragma once

#include "AbstractSceneFactory.h"
#include "Framework.h"
#include "SceneManager.h"

#include <chrono>
#include <memory>
#include <string>

#ifdef USE_IMGUI
#include "GameEditorController.h"
#endif

class PostEffect;

// Gameは、Framework上でシーン管理、エディタ、ポストエフェクト、プロファイル計測を統合します。
class Game : public Framework {
public:
	    // エンジン基盤、シーン、ポストエフェクト、エディタ機能を初期化します。
void Initialize() override;
	void Finalize() override;

protected:
	    // エディタ入力、ゲームシステム、シーン更新、プロファイル計測を進めます。
void Update() override;
	    // 実行モードに応じてゲーム画面またはエディタ用画面を描画します。
void Draw() override;

private:
	    // DirectX、オーディオ、入力などエンジンサービスを準備します。
void InitializeEngineServices();
	void InitializeScene();
	void InitializePostProcess();
	void InitializeEditorTools();
	void ConfigureInitialPlayState();
	void ApplyInitialSceneOverrides();

	std::string ResolveStartSceneName() const;
	float CalculateDeltaTime();

	void UpdateEditorFrame(float deltaTime);
	    // シーン、パーティクル、VFXなどゲーム中に動くシステムを更新します。
	void UpdateGameSystems(float deltaTime, float finalDeltaTime);
	void RunFixedUpdates(float finalDeltaTime, bool replayFrozen, bool sceneTransitioning);
	void RecordUpdateProfile(const std::chrono::high_resolution_clock::time_point& startUpdate);

	void DrawEditorFrame(PostEffect* postEffect);
	void DrawRuntimeFrame(PostEffect* postEffect);
	void DrawSceneToRenderTexture(bool editorMode);
	void DrawCameraEditorPreview(PostEffect* postEffect);
	    // シーン描画結果へポストエフェクトを適用し、表示先へ出力します。
void ApplyPostEffectPipeline(PostEffect* postEffect, bool outputForEditorGameView);
	void RecordDrawProfile(const std::chrono::high_resolution_clock::time_point& startDraw);
	void RecordFixedFpsProfile();

private:
	std::unique_ptr<SceneManager> sceneManager_ = nullptr;
	std::unique_ptr<AbstractSceneFactory> sceneFactory_ = nullptr;

	std::chrono::high_resolution_clock::time_point lastTime_;
	std::chrono::high_resolution_clock::time_point prePostDrawTime_;

	float timeScale_ = 1.0f;
	float fixedUpdateAccumulator_ = 0.0f;
	bool isPlaying_ = false;
	bool initialSceneOverridesPending_ = false;
	std::string currentSceneName_;

	float sceneUpdateTimeMs_ = 0.0f;
	float sceneDrawTimeMs_ = 0.0f;
	float updateTimeHistory_[120] = {};
	int timeHistoryIndex_ = 0;

	float drawTimeMs_ = 0.0f;
	float cpuCmdTimeMs_ = 0.0f;
	float drawTimeHistory_[120] = {};

#ifdef USE_IMGUI
	std::unique_ptr<GameEditorController> editorController_;
	EditorFrameState editorFrameState_;
#endif
};
