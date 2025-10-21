#pragma once
#include "Framework.h"
#include <memory>
// ★ SceneManager をインクルード
#include "SceneManager.h"

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


};