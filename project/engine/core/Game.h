#pragma once
#include "Framework.h"
#include <memory>
#include "SceneManager.h"
#include"CameraManager.h"
#include"AbstractSceneFactory.h"
#include "PostEffect.h"
#ifdef USE_IMGUI
#include "DebugEditor.h"
#include "SpriteDebugEditor.h"
#include "ParticleEditor.h"
#include "imgui_internal.h"
#include <imgui.h>
#include "VFXSequencerEditor.h"
#endif

#include"LightEditor.h"
#include <GhostRecorder.h>
#include "CameraEditor.h"
#include <PostEffectEditor.h>
#include "GhostDirector.h"
#include "GPUParticleEditor.h"



class Game : public Framework {
public:
	void Initialize() override;
	void Finalize() override;

protected:
	void Update() override;
	void Draw() override;

private:
	void SaveAllEditors();
private:
	std::unique_ptr<SceneManager> sceneManager_ = nullptr;
	std::chrono::high_resolution_clock::time_point lastTime_;
	std::unique_ptr<AbstractSceneFactory> sceneFactory_;
	float timeScale_ = 1.0f;
	bool isPlaying_ = false;
	std::string currentSceneName_;
	std::unique_ptr<PostEffect> postEffect_; 
	std::unique_ptr<PostEffectEditor> postEffectEditor_;
	float sceneUpdateTimeMs_ = 0.0f;
	float sceneDrawTimeMs_ = 0.0f;
	float updateTimeHistory_[120] = { 0 };
	int timeHistoryIndex_ = 0;
	float drawTimeMs_ = 0.0f;
	float drawTimeHistory_[120] = { 0 };
#ifdef USE_IMGUI
	std::unique_ptr<DebugEditor> debugEditor_;
	std::unique_ptr<SpriteDebugEditor> spriteDebugEditor_;
	std::unique_ptr<ParticleEditor> particleEditor_;
	std::unique_ptr<GhostRecorder> ghostRecorder_;
	std::unique_ptr<GhostDirector> ghostDirector_;
	std::unique_ptr<GPUParticleEditor> gpuParticleEditor_;
	std::unique_ptr<VFXSequencerEditor> vfxSequencerEditor_;
	bool showLightEditor_ = false;
	bool showParticleEditor_ = false;
	bool showDebugWindows_ = true;
	bool showSpriteInspector_ = false;
	bool showDebugConsole_ = false;
	bool showCameraEditor = false;
	bool showGhostRecorder_ = false;
	bool showTimeController_ = false;
	bool showPostEffectEditor_ = false; 
	bool showBossDebug_ = false;
	ImVec2 lastGameViewSize_ = { 0, 0 };
#endif
};