#pragma once
#include "engine/base/Framework.h"
#include "engine/scene/GamePlayScene.h" 
#include <memory>
#ifdef _DEBUG
#include "DebugEditor.h" 
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
	// ゲームプレイシーン
	std::unique_ptr<GamePlayScene> gameScene_ = nullptr;
#ifdef _DEBUG
	std::unique_ptr<DebugEditor> debugEditor_ = nullptr;
#endif
};