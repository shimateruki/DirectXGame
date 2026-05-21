#include "TimeAttackUI.h"
#include "TextureManager.h"
#include <string>
#include <cmath>

void TimeAttackUI::Initialize(SpriteCommon* spriteCommon) {
    spriteCommon_ = spriteCommon;

    // 1. テクスチャの読み込み
    for (int i = 0; i < 10; ++i) {
        std::string path = "Resources/sprite/number/" + std::to_string(i) + ".png";
        numberTexHandles_[i] = TextureManager::GetInstance()->Load(path);
    }
    colonTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/number/colon.png");
    dotTexHandle_   = TextureManager::GetInstance()->Load("Resources/sprite/number/dot.png");

    // 2. スプライトの生成と初期配置 (8文字分: "00:00.00")
    Vector2 basePos = { 1200.0f, 50.0f };
    float xOffsets[8] = {
        0.0f,    // [0] 0文字目
        42.0f,   // [1] 1文字目
        90.0f,   // [2] コロン (幅を狭く見せたいので詰め気味に)
        106.0f,  // [3] 3文字目
        148.0f,  // [4] 4文字目
        200.0f,  // [5] ドット (ここも詰め気味に)
        212.0f,  // [6] 6文字目
        254.0f   // [7] 7文字目
    };

    for (int i = 0; i < 8; ++i) {
        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon_, numberTexHandles_[0]);

        if (i == 2) sprite->SetTextureHandle(colonTexHandle_);
        else if (i == 5) sprite->SetTextureHandle(dotTexHandle_);

        // ★ 配列からズレの数値をそのまま足すだけ！
        sprite->SetPosition({ basePos.x + xOffsets[i], basePos.y });
        sprite->SetSize({ 64.0f, 64.0f });

        digitSprites_.push_back(std::move(sprite));
    }
}

void TimeAttackUI::Update(float deltaTime) {
    if (isRunning_) {
        currentTime_ += deltaTime;
    }

    // --- 1. 目標となる本当の時間を計算 ---
    float t = currentTime_;
    int minutes = static_cast<int>(t / 60.0f);
    int seconds = static_cast<int>(fmodf(t, 60.0f));
    int ms = static_cast<int>(fmodf(t, 1.0f) * 100.0f);

    int targetDigits[6];
    targetDigits[0] = (minutes / 10) % 10;
    targetDigits[1] = minutes % 10;
    targetDigits[2] = (seconds / 10) % 10;
    targetDigits[3] = seconds % 10;
    targetDigits[4] = (ms / 10) % 10;
    targetDigits[5] = ms % 10;

    // --- 2. ドラムロール演出の処理 ---
    int displayDigits[6];

    if (isRolling_) {
        rollTimer_ += deltaTime;

        // ★ 0.12秒ごとに左から1桁ずつ「ピタッ！」と止まる（速さはお好みで調整）
        if (rollTimer_ > 0.12f) {
            rollTimer_ -= 0.12f;
            fixedDigitCount_++;
            if (fixedDigitCount_ >= 6) {
                isRolling_ = false; // 6桁すべて止まったら演出終了！
                fixedDigitCount_ = 6;
            }
        }

        for (int i = 0; i < 6; ++i) {
            if (i < fixedDigitCount_) {
                // すでに止まった桁は本当の数字を表示
                displayDigits[i] = targetDigits[i];
            }
            else {
                // まだ止まっていない桁はランダムに回す（ドラムロール）
                displayDigits[i] = std::rand() % 10;
            }
        }
    }
    else {
        // 演出中でなければ、すべて本当の数字を表示
        for (int i = 0; i < 6; ++i) displayDigits[i] = targetDigits[i];
    }

    // --- 3. テクスチャの差し替え ---
    digitSprites_[0]->SetTextureHandle(numberTexHandles_[displayDigits[0]]);
    digitSprites_[1]->SetTextureHandle(numberTexHandles_[displayDigits[1]]);
    // [2] はコロンなのでそのまま
    digitSprites_[3]->SetTextureHandle(numberTexHandles_[displayDigits[2]]);
    digitSprites_[4]->SetTextureHandle(numberTexHandles_[displayDigits[3]]);
    // [5] はドットなのでそのまま
    digitSprites_[6]->SetTextureHandle(numberTexHandles_[displayDigits[4]]);
    digitSprites_[7]->SetTextureHandle(numberTexHandles_[displayDigits[5]]);

    for (auto& sprite : digitSprites_) {
        if (sprite) sprite->Update();
    }
}

void TimeAttackUI::Draw() {
    for (auto& sprite : digitSprites_) {
        sprite->Draw();
    }
}

void TimeAttackUI::SetPosition(const Vector2& basePos, float spacingScale) {
    // 隊長が調整した「完璧な数値」の配列
    float xOffsets[8] = {
        0.0f,    // [0]
        42.0f,   // [1]
        90.0f,   // [2] コロン
        106.0f,  // [3]
        148.0f,  // [4]
        200.0f,  // [5] ドット
        216.0f,  // [6]
        258.0f   // [7]
    };

    for (int i = 0; i < 8; ++i) {
        if (i < digitSprites_.size() && digitSprites_[i]) {
            // ★ 配列の数値に倍率を掛けてから、basePos.x に足す！
            float posX = basePos.x + (xOffsets[i] * spacingScale);
            digitSprites_[i]->SetPosition({ posX, basePos.y });
        }
    }
}
void TimeAttackUI::SetAlpha(float alpha) {
    for (auto& sprite : digitSprites_) {
        if (sprite) {
            Vector4 color = sprite->GetColor();
            color.w = alpha; // 透明度(w)をセット
            sprite->SetColor(color);
        }
    }
}