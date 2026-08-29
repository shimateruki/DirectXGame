#pragma once

#include "RuntimeScene.h"

/// タイトル画面の入口です。UIや演出はInitializeSceneContentsへ追加します。
class TitleScene final : public RuntimeScene {
public:
    std::string GetName() override { return "Title Scene"; }

protected:
    void InitializeSceneContents() override;
};
