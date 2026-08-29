#pragma once

#include "RuntimeScene.h"

/// ゲームオーバー画面の入口です。結果UIや再開導線はInitializeSceneContentsへ追加します。
class GameOverScene final : public RuntimeScene {
public:
    std::string GetName() override { return "Game Over Scene"; }

protected:
    void InitializeSceneContents() override;
};
