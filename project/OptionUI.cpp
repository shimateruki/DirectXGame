#include "OptionUI.h"
#include "BaseScene.h"
#include "DebugConsole.h"
#include "KeyConfig.h"
#include "TextureManager.h"
#include "SpriteCommon.h"

void OptionUI::Initialize(BaseScene* scene, SpriteCommon* spriteCommon) {
    if (!scene || !spriteCommon) return;
    spriteCommon_ = spriteCommon;
    KeyConfig::GetInstance()->Initialize();
    // エディタで配置したベースUIの取得
    bgSprite_ = scene->GetSpriteByName("UI/back_ground.png");
    titleSprite_ = scene->GetSpriteByName("UI/option_UI.png");
    soundSprite_ = scene->GetSpriteByName("UI/sound.png");
    keyboardSprite_ = scene->GetSpriteByName("UI/keyboard.png");
    spaceIconSprite_ = scene->GetSpriteByName("UI/Space.png");
    cursorSprite_ = scene->GetSpriteByName("UI/cursor.png");

    if (cursorSprite_) {
        cursorBasePos_ = cursorSprite_->GetPosition();
    }

    currentState_ = MenuState::Top;
    currentOptionIndex_ = (int)OptionIndex::Sound;
    currentConfigIndex_ = 0;

    // ========================================================
    // ★ エディタ配置スプライトは「目印」なので、透明にして隠す！
    // ========================================================
    actionSprites_.clear();
    for (const auto& name : actionSpriteNames_) {
        Sprite* sp = scene->GetSpriteByName(name);
        if (sp) {
            sp->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f }); // 透明マジック！
        }
        actionSprites_.push_back(sp);
    }

    RefreshKeyIcons(); // 初回生成
}

bool OptionUI::Update(float deltaTime) {
    InputManager* input = InputManager::GetInstance();
    Vector4 normalColor = { 0.5f, 0.5f, 0.5f, 1.0f };
    Vector4 selectColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    switch (currentState_) {
    case MenuState::Top:
    {
        if (input->IsKeyTriggered(DIK_LEFT) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_LEFT)) {
            currentOptionIndex_--;
            if (currentOptionIndex_ < 0) currentOptionIndex_ = (int)OptionIndex::Max - 1;
        }
        if (input->IsKeyTriggered(DIK_RIGHT) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_RIGHT)) {
            currentOptionIndex_++;
            if (currentOptionIndex_ >= (int)OptionIndex::Max) currentOptionIndex_ = 0;
        }

        if (soundSprite_) soundSprite_->SetColor(currentOptionIndex_ == (int)OptionIndex::Sound ? selectColor : normalColor);
        if (keyboardSprite_) keyboardSprite_->SetColor(currentOptionIndex_ == (int)OptionIndex::KeyConfig ? selectColor : normalColor);

        // カーソルを透明にして隠す
        if (cursorSprite_) {
            cursorSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
        }

        if (input->IsKeyTriggered(DIK_BACKSPACE)) {
            return true;
        }

        if (input->IsKeyTriggered(DIK_SPACE)) {
            if (currentOptionIndex_ == (int)OptionIndex::KeyConfig) {
                currentState_ = MenuState::KeyConfig;
                currentConfigIndex_ = 0;
                RefreshKeyIcons(); // 最新情報を読み込み
            }
        }
        break;
    }

    case MenuState::KeyConfig:
    {
        if (input->IsKeyTriggered(DIK_UP) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP)) {
            currentConfigIndex_--;
            if (currentConfigIndex_ < 0) currentConfigIndex_ = (int)configActions_.size() - 1;
        }
        if (input->IsKeyTriggered(DIK_DOWN) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN)) {
            currentConfigIndex_++;
            if (currentConfigIndex_ >= (int)configActions_.size()) currentConfigIndex_ = 0;
        }

        // カーソルを不透明に戻し、エディタのY座標に完全スナップ！
        if (cursorSprite_ && currentConfigIndex_ < actionSprites_.size()) {
            cursorSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            Vector2 newPos = cursorBasePos_;
            if (actionSprites_[currentConfigIndex_]) {
                newPos.y = actionSprites_[currentConfigIndex_]->GetPosition().y;
            }
            cursorSprite_->SetPosition(newPos);
        }

        if (input->IsKeyTriggered(DIK_BACKSPACE)) {
            currentState_ = MenuState::Top;
        }

        if (input->IsKeyTriggered(DIK_SPACE)) {
            currentState_ = MenuState::WaitInput;
        }
        break;
    }

    case MenuState::WaitInput:
    {
        if (cursorSprite_) cursorSprite_->SetColor({ 1.0f, 0.3f, 0.3f, 1.0f });

        if (input->IsKeyTriggered(DIK_ESCAPE)) {
            currentState_ = MenuState::KeyConfig;
            break;
        }

        const auto& pressedKeys = input->GetPressedKeys();
        for (int newKey : pressedKeys) {
            if (input->IsKeyTriggered(newKey)) {
                if (IsValidKey(newKey)) {
                    std::string targetAction = configActions_[currentConfigIndex_];
                    int oldKey = KeyConfig::GetInstance()->GetKeyCode(targetAction);

                    // 重複スワップ処理
                    for (const auto& otherAction : configActions_) {
                        if (otherAction != targetAction && KeyConfig::GetInstance()->GetKeyCode(otherAction) == newKey) {
                            KeyConfig::GetInstance()->SetKeyCode(otherAction, oldKey);
                            break;
                        }
                    }

                    KeyConfig::GetInstance()->SetKeyCode(targetAction, newKey);
                    RefreshKeyIcons(); // 最新情報で再生成！
                    currentState_ = MenuState::KeyConfig;
                    break;
                }
            }
        }
        break;
    }
    }
    return false;
}

bool OptionUI::IsOptionSprite(Sprite* sp) const {
    if (sp == bgSprite_ || sp == titleSprite_ || sp == soundSprite_ ||
        sp == keyboardSprite_ || sp == spaceIconSprite_ || sp == cursorSprite_) {
        return true;
    }
    for (Sprite* actionSp : actionSprites_) {
        if (sp == actionSp) return true;
    }
    return false;
}

// --- OptionUI.cpp ---

void OptionUI::RefreshKeyIcons() {
    keyIconSprites_.clear(); // 古い見切れた画像をクリア

    for (size_t i = 0; i < configActions_.size(); ++i) {
        int keyCode = KeyConfig::GetInstance()->GetKeyCode(configActions_[i]);
        std::string spriteName = GetKeySpriteName(keyCode);
        uint32_t texHandle = TextureManager::GetInstance()->Load("Resources/sprite/" + spriteName);

        // ========================================================
        // ★ エディタで配置されたダミー（目印）の座標を取得
        // ========================================================
        Vector2 markerPos = { 0, 0 };
        if (i < actionSprites_.size() && actionSprites_[i]) {
            markerPos = actionSprites_[i]->GetPosition();
        }

        // ==========================================
        // ★ ここから可変サイズ対応！ 
        // ==========================================

        // 通常キーの基準サイズ（正方形の1辺）
        const float normalKeySize = 42.0f;

        // これから決める動的なサイズと調整量
        float dynamicWidth = normalKeySize;
        float dynamicHeight = normalKeySize;
        float horizontalShift = 0.0f; // 中央揃えにするための横移動量

        // --- キーコードによって幅（dynamicWidth）を変える ---
        if (keyCode == DIK_SPACE) {
            // Spaceキーは思い切り広く (文字が見えるように)
            dynamicWidth = 80.0f;
        }
        else if (keyCode == DIK_LSHIFT || keyCode == DIK_RSHIFT ||
            keyCode == DIK_LCONTROL || keyCode == DIK_RCONTROL ||
            keyCode == DIK_RETURN) {
            // Shift, Ctrl, Enterは中くらいに広く
            dynamicWidth = 80.0f;
        }

        // --- 中央揃えの計算 ---
        // 通常の正方形から幅が広がった分の「半分」だけ左にずらす
        horizontalShift = (dynamicWidth - normalKeySize) / 2.0f;


        // ★ 新しくスプライトを生成し、完璧なサイズと座標に設定する！
        auto sp = std::make_unique<Sprite>();
        sp->Initialize(spriteCommon_, texHandle);

        // 可変サイズを設定
        sp->SetSize({ dynamicWidth, dynamicHeight });

        // 目印の座標から、広がった分だけ左にずらして中央に配置
        sp->SetPosition({ markerPos.x - horizontalShift, markerPos.y });

        sp->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        sp->Update();

        keyIconSprites_.push_back(std::move(sp));
    }
}

void OptionUI::DrawKeyIcons() {


    // ★ プログラムで新しく生成した完璧なスプライトだけを描画！
    for (auto& sp : keyIconSprites_) {
        sp->Draw();
    }
}

// --- IsValidKey と GetKeySpriteName はそのまま ---
bool OptionUI::IsValidKey(int keyCode) const {
    if (keyCode >= DIK_1 && keyCode <= DIK_0) return true;
    if (keyCode >= DIK_Q && keyCode <= DIK_P) return true;
    if (keyCode >= DIK_A && keyCode <= DIK_L) return true;
    if (keyCode >= DIK_Z && keyCode <= DIK_M) return true;
    if (keyCode == DIK_SPACE || keyCode == DIK_LSHIFT || keyCode == DIK_RSHIFT ||
        keyCode == DIK_LCONTROL || keyCode == DIK_RCONTROL || keyCode == DIK_RETURN) {
        return true;
    }
    return false;
}

std::string OptionUI::GetKeySpriteName(int keyCode) const {
    if (keyCode == DIK_SPACE) return "UI/Space.png";
    if (keyCode == DIK_LSHIFT || keyCode == DIK_RSHIFT) return "UI/Shift.png";
    if (keyCode == DIK_LCONTROL || keyCode == DIK_RCONTROL) return "UI/Ctrl.png";
    if (keyCode == DIK_RETURN) return "UI/Eenter.png";

    if (keyCode == DIK_A) return "UI/A.png"; if (keyCode == DIK_B) return "UI/B.png";
    if (keyCode == DIK_C) return "UI/C.png"; if (keyCode == DIK_D) return "UI/D.png";
    if (keyCode == DIK_E) return "UI/E.png"; if (keyCode == DIK_F) return "UI/F.png";
    if (keyCode == DIK_G) return "UI/G.png"; if (keyCode == DIK_H) return "UI/H.png";
    if (keyCode == DIK_I) return "UI/I.png"; if (keyCode == DIK_J) return "UI/J.png";
    if (keyCode == DIK_K) return "UI/K.png"; if (keyCode == DIK_L) return "UI/L.png";
    if (keyCode == DIK_M) return "UI/M.png"; if (keyCode == DIK_N) return "UI/N.png";
    if (keyCode == DIK_O) return "UI/O.png"; if (keyCode == DIK_P) return "UI/P.png";
    if (keyCode == DIK_Q) return "UI/Q.png"; if (keyCode == DIK_R) return "UI/R.png";
    if (keyCode == DIK_S) return "UI/S.png"; if (keyCode == DIK_T) return "UI/T.png";
    if (keyCode == DIK_U) return "UI/U.png"; if (keyCode == DIK_V) return "UI/V.png";
    if (keyCode == DIK_W) return "UI/W.png"; if (keyCode == DIK_X) return "UI/X.png";
    if (keyCode == DIK_Y) return "UI/Y.png"; if (keyCode == DIK_Z) return "UI/Z.png";

    if (keyCode == DIK_0) return "UI/0.png"; if (keyCode == DIK_1) return "UI/1.png";
    if (keyCode == DIK_2) return "UI/2.png"; if (keyCode == DIK_3) return "UI/3.png";
    if (keyCode == DIK_4) return "UI/4.png"; if (keyCode == DIK_5) return "UI/5.png";
    if (keyCode == DIK_6) return "UI/6.png"; if (keyCode == DIK_7) return "UI/7.png";
    if (keyCode == DIK_8) return "UI/8.png"; if (keyCode == DIK_9) return "UI/9.png";

    return "white.png";
}