#pragma once
#include "Framework.h"
#include <memory>
#include "SceneManager.h"
#include"CameraManager.h"
#include"AbstractSceneFactory.h"
#ifdef _DEBUG
#include "DebugEditor.h"
#include "SpriteDebugEditor.h"
#include "ParticleEditor.h"
#endif



class Game : public Framework {
public:
	void Initialize() override;
	void Finalize() override;

protected:
	void Update() override;
	void Draw() override;

private:
	// ★ gameScene_ と debugEditor_ の代わりに SceneManager を持つ
	std::unique_ptr<SceneManager> sceneManager_ = nullptr;
	std::chrono::high_resolution_clock::time_point lastTime_;
	std::unique_ptr<AbstractSceneFactory> sceneFactory_;
	float timeScale_ = 1.0f;
#ifdef _DEBUG
	std::unique_ptr<DebugEditor> debugEditor_;
	std::unique_ptr<SpriteDebugEditor> spriteDebugEditor_;
	std::unique_ptr<ParticleEditor> particleEditor_;
	bool showParticleEditor_ = true;
	bool showDebugWindows_ = true;  // 3Dエディタ用
	bool showSpriteInspector_ = true; // 2Dエディタ用
	bool showDebugConsole_ = true;
#endif
};