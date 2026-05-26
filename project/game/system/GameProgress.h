#pragma once

// ゲームの進行度やフラグをゲーム全体で一生保持するクラス（シングルトン）
class GameProgress {
private:
    GameProgress() = default;
    ~GameProgress() = default;

public:
    // シングルトンインスタンスの取得
    static GameProgress* GetInstance() {
        static GameProgress instance;
        return &instance;
    }

    // コピー禁止
    GameProgress(const GameProgress&) = delete;
    GameProgress& operator=(const GameProgress&) = delete;

    // 全てのフラグをリセット（最初からはじめる・リスタート用）
    void Reset() {
        hasBridgeDropped = false;
        hasFinishedTutorial = false;
    }

    // ==========================================
    // ここにゲーム全体で保持したいフラグを追加していく
    // ==========================================
    bool hasBridgeDropped = false;    // 橋が落ちたか？
    bool hasFinishedTutorial = false; // チュートリアル終わったか？
};