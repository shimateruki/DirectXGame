#pragma once
#include <vector>
#include <memory> // unique_ptr
#include <string>

// 前方宣言
class GamePlayScene;
class Sprite;

class SpriteDebugEditor {
public:
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(GamePlayScene* scene);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// ImGui を使った毎フレーム更新
    /// </summary>
    void Update();

    /// <summary>
    /// (任意) デバッグ描画 (選択中のスプライト枠など)
    /// </summary>
    void DrawDebug(); // 今は空にしておく

private:
    GamePlayScene* scene_ = nullptr; // シーンの参照 (スプライトリスト取得用)
    Sprite* selectedSprite_ = nullptr; // 現在選択中のスプライトへのポインタ

    // (任意) スプライトレイアウト保存用
    void SaveSpriteLayout(const std::string& filename);
};