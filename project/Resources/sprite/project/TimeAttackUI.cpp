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

    // --- 時間の分解 ---
    float t = currentTime_;
    int minutes = static_cast<int>(t / 60.0f);
    int seconds = static_cast<int>(fmodf(t, 60.0f));
    int ms      = static_cast<int>(fmodf(t, 1.0f) * 100.0f); // 小数点以下2桁 (0~99)

    // --- 各桁の抽出 (最大99分まで対応) ---
    int m10  = (minutes / 10) % 10;
    int m1   = minutes % 10;
    int s10  = (seconds / 10) % 10;
    int s1   = seconds % 10;
    int ms10 = (ms / 10) % 10;
    int ms1  = ms % 10;

    // --- テクスチャの差し替え ---
    digitSprites_[0]->SetTextureHandle(numberTexHandles_[m10]);
    digitSprites_[1]->SetTextureHandle(numberTexHandles_[m1]);
    // [2] はコロンなのでそのまま
    digitSprites_[3]->SetTextureHandle(numberTexHandles_[s10]);
    digitSprites_[4]->SetTextureHandle(numberTexHandles_[s1]);
    // [5] はドットなのでそのまま
    digitSprites_[6]->SetTextureHandle(numberTexHandles_[ms10]);
    digitSprites_[7]->SetTextureHandle(numberTexHandles_[ms1]);

    // スプライトの行列更新
    for (auto& sprite : digitSprites_) {
        sprite->Update();
    }
}

void TimeAttackUI::Draw() {
    for (auto& sprite : digitSprites_) {
        sprite->Draw();
    }
}

void TimeAttackUI::SetPosition(const Vector2& basePos) {
    // Initializeで使ったのと同じ「X座標のズレ」を使って一斉に移動させる
    float xOffsets[8] = {
        0.0f,    // [0] 分(十)
        42.0f,   // [1] 分(一)
        74.0f,   // [2] コロン
        106.0f,  // [3] 秒(十)
        148.0f,  // [4] 秒(一)
        180.0f,  // [5] ドット
        212.0f,  // [6] ミリ秒(十)
        254.0f   // [7] ミリ秒(一)
    };

    for (int i = 0; i < 8; ++i) {
        if (digitSprites_[i]) {
            digitSprites_[i]->SetPosition({ basePos.x + xOffsets[i], basePos.y });
            digitSprites_[i]->Update(); // 位置を変えたら行列も更新しておく
        }
    }
}