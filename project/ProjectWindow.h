#pragma once

class DebugEditor; // 前方宣言

// 下部パネル（Project / アセットブラウザ）の描画を担当するクラス
class ProjectWindow {
public:
    ProjectWindow() = default;
    ~ProjectWindow() = default;

    // 起動時に DebugEditor のポインタを受け取っておく
    void Initialize(DebugEditor* editor);

    // 毎フレームの描画処理
    void Draw();

private:
    DebugEditor* editor_ = nullptr; // 本体へのアクセス権
};