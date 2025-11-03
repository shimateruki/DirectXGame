#pragma once
#include "Framework.h"
#include <memory>
#include "SceneManager.h"
#include"CameraManager.h"

#ifdef _DEBUG
#include "DebugEditor.h"
#include "SpriteDebugEditor.h"
#endif
// Frameworkを継承した、このゲーム独自のクラス
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
#ifdef _DEBUG
	std::unique_ptr<DebugEditor> debugEditor_;
	std::unique_ptr<SpriteDebugEditor> spriteDebugEditor_;
#endif
};