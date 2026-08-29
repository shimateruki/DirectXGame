#pragma once

#include "RuntimeScene.h"

/// 実際のゲーム進行を実装するSceneです。初期状態では基本Playerだけを生成します。
class GameScene final : public RuntimeScene {
public:
    std::string GetName() override { return "Game Scene"; }

protected:
    void InitializeSceneContents() override;
};
