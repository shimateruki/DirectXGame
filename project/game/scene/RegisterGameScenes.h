#pragma once

#include "SceneFactory.h"
#include "game/scene/clear/GameClearScene.h"
#include "game/scene/editor/SceneAssetEditorScene.h"
#include "game/scene/gameover/GameOverScene.h"
#include "game/scene/gameplay/GamePlayScene.h"
#include "game/scene/preview/PreviewScene.h"
#include "game/scene/select/GameSelectScene.h"
#include "game/scene/settings/SettingsScene.h"
#include "game/scene/title/TitleScene.h"
#include "game/scene/tutorial/TutorialScene.h"

// Scene Asset の runtimeScene と同じIDで、ゲーム固有の実行クラスを登録します。
// 登録順はエディターのシーン候補一覧にも使用します。
inline void RegisterGameScenes(SceneFactory& factory) {
    factory.RegisterScene("TITLE", [] { return std::make_unique<TitleScene>(); });
    factory.RegisterScene("SETTING", [] { return std::make_unique<SettingsScene>(); });
    factory.RegisterScene("GAMEPLAY", [] { return std::make_unique<GamePlayScene>(); });
    factory.RegisterScene("SELECT", [] { return std::make_unique<GameSelectScene>(); });
    factory.RegisterScene("GAMEOVER", [] { return std::make_unique<GameOverScene>(); });
    factory.RegisterScene("GAMECLEAR", [] { return std::make_unique<GameClearScene>(); });
    factory.RegisterScene("PREVIEW", [] { return std::make_unique<PreviewScene>(); });
    factory.RegisterScene("TUTORIAL", [] { return std::make_unique<TutorialScene>(); });
    factory.RegisterScene("SCENE_EDITOR", [] { return std::make_unique<SceneAssetEditorScene>(); });
}
