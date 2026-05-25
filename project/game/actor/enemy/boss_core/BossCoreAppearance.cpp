#include "BossCoreShared.h"

void BossCore::StartAppearance() {
    if (isAppearing_ || isBattleStarted_) return;

    isAppearing_ = true;

    // ====================================================
    // 変更：まずは「フェーズ0（1秒間の完全静止）」からスタート
    // ====================================================
    appearancePhase_ = 0;
    appearanceTimer_ = 1.0f; // 1秒待つ

    DebugConsole::GetInstance()->AddLog("[EVENT] ボス部屋到達…1秒間の静寂！");
}

void BossCore::StartBattle() {
    if (isBattleStarted_) return; // 既に始まっていたら何もしない

    isBattleStarted_ = true;
    startBattlePos_ = GetTranslate(); // 登場ムービー終了時の初期座標を記憶
    animTimer_ = 0.0f; // ここから2秒後に最初の攻撃をさせるため、タイマーをリセット

    DebugConsole::GetInstance()->AddLog("[BATTLE START] ボスが行動を開始した！！！");

    // ====================================================
    // 戦闘開始フラグがONになったので、
    // 現在の状態(Idle)を再セットして、即座に属性を「kEnemy」に更新する
    // ====================================================
    ChangeState(state_);
}

void BossCore::UpdateAppearance(float deltaTime) {
    if (!isAppearing_) return;

    appearanceTimer_ -= deltaTime;

    // ====================================================
    // フェーズ0（1秒間の待機）
    // ====================================================
    if (appearancePhase_ == 0) {
        if (appearanceTimer_ <= 0.0f) {
            // 1秒の沈黙が終わったフェーズ1（咆哮）へ移行し、カメラを動かす
            appearancePhase_ = 1;
            appearanceTimer_ = 2.0f; // 咆哮の2秒間
            DebugConsole::GetInstance()->AddLog("[EVENT] ボス起動！！");

            if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
                CameraEditor::GetInstance()->PlayOverrideCamera(camera, "a");
            }
        }
        return; // 待機中はここで処理を終わる（ボスは微動だにしない）
    }

    // ====================================================
    // フェーズ1：咆哮とスケール変更
    // ====================================================
    float t = 2.0f - appearanceTimer_;
    Vector3 currentScale = { 1.0f, 1.0f, 1.0f };

    if (t < 0.5f) {
        // ① 息を吸い込む
        float p = t / 0.5f;
        float shrink = std::sin(p * 3.1415f) * 0.2f;
        currentScale = { 1.0f - shrink, 1.0f - shrink, 1.0f - shrink };
    }
    else if (t < 1.8f) {
        // ② 咆哮・ブルブル震える
        float p = (t - 0.5f) / 1.3f;
        float swell = (1.0f - std::pow(p, 2.0f)) * 0.3f;

        float shakeX = std::sin(t * 60.0f) * 0.05f;
        float shakeY = std::cos(t * 65.0f) * 0.05f;
        float shakeZ = std::sin(t * 70.0f) * 0.05f;

        currentScale = { 1.0f + swell + shakeX, 1.0f + swell + shakeY, 1.0f + swell + shakeZ };
        SetColor({ 1.0f, 0.6f, 0.6f, 1.0f });
    }
    else {
        // ③ スッ…と元に戻る
        currentScale = { 1.0f, 1.0f, 1.0f };
        SetColor(greenColor_);
        defaultColor_ = greenColor_;
    }

    SetScale(currentScale);

    if (appearanceTimer_ <= 0.0f) {
        isAppearing_ = false;
        SetScale({ 1.0f, 1.0f, 1.0f });
        SetColor(greenColor_);
        defaultColor_ = greenColor_;

        // ゴーストディレクターのアニメーション（EntranceAnimation.json）を再生
        if (director_) {
            director_->PlayScenario(false, false);
            isWaitingForDirector_ = true; // アニメーション終了を待つフラグをオンにする
        }
    }
}

