#pragma once
#include "Framework.h"
#include <memory>
#include "SceneManager.h"
#include"CameraManager.h"
#include"AbstractSceneFactory.h"
#ifdef USE_IMGUI
#include "DebugEditor.h"
#include "SpriteDebugEditor.h"
#include "ParticleEditor.h"
#endif

#include"LightEditor.h"
#include <GhostRecorder.h>
#include "CameraEditor.h"


class Game : public Framework {
public:
	void Initialize() override;
	void Finalize() override;

protected:
	void Update() override;
	void Draw() override;

private:
	std::unique_ptr<SceneManager> sceneManager_ = nullptr;
	std::chrono::high_resolution_clock::time_point lastTime_;
	std::unique_ptr<AbstractSceneFactory> sceneFactory_;
	float timeScale_ = 1.0f;
	bool isPlaying_ = false;
	std::string currentSceneName_;
#ifdef USE_IMGUI
	std::unique_ptr<DebugEditor> debugEditor_;
	std::unique_ptr<SpriteDebugEditor> spriteDebugEditor_;
	std::unique_ptr<ParticleEditor> particleEditor_;
	std::unique_ptr<GhostRecorder> ghostRecorder_;
	bool showLightEditor_ = false;
	bool showParticleEditor_ = false;
	bool showDebugWindows_ = false;
	bool showSpriteInspector_ = false;
	bool showDebugConsole_ = false;
	bool showCameraEditor = false;
	bool showGhostRecorder_ = false;
	bool showTimeController_ = false;

#endif
};