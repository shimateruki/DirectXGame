#pragma once
#include "Sprite.h"
#include <memory>
#include <vector>

class SpriteCommon;

// タイムアタック用のタイマーUIクラス
class TimeAttackUI {
public:
    void Initialize(SpriteCommon* spriteCommon);
    void Update(float deltaTime);
    void Draw();

    void Start() { isRunning_ = true; }
    void Stop() { isRunning_ = false; }
    void Reset() { currentTime_ = 0.0f; isRunning_ = false; }
    void SetTime(float time) { currentTime_ = time; isRunning_ = false; }
    void SetPosition(const Vector2& basePos);
    float GetCurrentTime() const { return currentTime_; }
    void SetAlpha(float alpha);
private:
    // 数字(0~9)と記号のテクスチャハンドル
    uint32_t numberTexHandles_[10];
    uint32_t colonTexHandle_;
    uint32_t dotTexHandle_;

    // 画面に表示する8文字分のスプライト (例: "01:23.45")
    std::vector<std::unique_ptr<Sprite>> digitSprites_;



    float currentTime_ = 0.0f;
    bool isRunning_ = false;
    SpriteCommon* spriteCommon_ = nullptr;

};