#include "engine/scene/Game.h"
#include "engine/base/Framework.h" // Frameworkをインクルード
#include <memory>

// Windowsアプリでのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

    // Frameworkクラスのポインタで、Gameクラスのインスタンスを生成
    std::unique_ptr<Framework> game = std::make_unique<Game>();

    // Frameworkの初期化
    game->Initialize();

    // Frameworkのメインループ実行
    game->Run();

    // Frameworkの終了処理
    game->Finalize();

    return 0;
}