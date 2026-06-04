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

class Game : public Framework {
public:
	void Initialize() override;
	void Finalize() override;

protected:
	void Update() override;
	void Draw() override;

private:
	void InitializeEngineServices();
	void InitializeScene();
	void InitializePostProcess();
	void InitializeEditorTools();
	void ConfigureInitialPlayState();
	void ApplyInitialSceneOverrides();

	std::string ResolveStartSceneName() const;
	float CalculateDeltaTime();

	void UpdateEditorFrame(float deltaTime);
	void UpdateGameSystems(float deltaTime, float finalDeltaTime);
	void RecordUpdateProfile(const std::chrono::high_resolution_clock::time_point& startUpdate);

	void DrawEditorFrame(PostEffect* postEffect);
	void DrawRuntimeFrame(PostEffect* postEffect);
	void DrawSceneToRenderTexture(bool editorMode);
	void ApplyPostEffectPipeline(PostEffect* postEffect, bool outputForEditorGameView);
	void RecordDrawProfile(const std::chrono::high_resolution_clock::time_point& startDraw);
	void RecordFixedFpsProfile();

private:
	std::unique_ptr<SceneManager> sceneManager_ = nullptr;
	std::unique_ptr<AbstractSceneFactory> sceneFactory_ = nullptr;

	std::chrono::high_resolution_clock::time_point lastTime_;
	std::chrono::high_resolution_clock::time_point prePostDrawTime_;

	float timeScale_ = 1.0f;
	bool isPlaying_ = false;
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
