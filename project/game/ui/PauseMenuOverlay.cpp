#define NOMINMAX
#include "PauseMenuOverlay.h"

#include "InputManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SpriteLayoutScaler.h"
#include "TextureManager.h"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

void PauseMenuOverlay::Initialize(SpriteCommon* spriteCommon, const std::string& layoutPath, Mode mode) {
    // 共通レイアウトを読み込み、使用するシーンのモードに応じて項目を制御する。
    spriteCommon_ = spriteCommon;
    inputManager_ = InputManager::GetInstance();
    mode_ = mode;
    LoadLayout(layoutPath);
    BindLayoutSprites();
    SetAllVisible(false);
}

void PauseMenuOverlay::Finalize() {
    // シーン切り替え後に破棄済み Sprite への参照を残さない。
    sprites_.clear();
    background_ = nullptr;
    panel_ = nullptr;
    titlePlate_ = nullptr;
    title_ = nullptr;
    hintBack_ = nullptr;
    hintText_ = nullptr;
    cursorSlime_ = nullptr;
    cursorArrow_ = nullptr;
    rows_ = {};
    spriteCommon_ = nullptr;
    inputManager_ = nullptr;
    isActive_ = false;
}

void PauseMenuOverlay::SetActive(bool active) {
    // 開く操作を閉じる操作として再使用しないよう、キー解放まで入力を抑制する。
    if (isActive_ == active) {
        return;
    }

    isActive_ = active;
    suppressOpenCloseInput_ = active;
    selectedIndex_ = 0;
    SetAllVisible(active);
}

// ポーズ画面は選択された操作を返すだけにし、シーン遷移の実行は呼び出し側に任せます。
PauseMenuOverlay::Action PauseMenuOverlay::Update(float deltaTime) {
    // 入力結果をシーン側へ返し、画面遷移そのものはこの UI から実行しません。
    if (!isActive_) {
        return Action::None;
    }

    time_ += deltaTime;
    Action action = UpdateInput();
    UpdateSprites();

    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->Update();
        }
    }

    return action;
}

void PauseMenuOverlay::Draw() {
    // ポーズを閉じた後は非表示の Sprite を描画パスへ送らない。
    if (!isActive_) {
        return;
    }

    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->Draw();
        }
    }
}

void PauseMenuOverlay::CollectReplaySprites(std::vector<Sprite*>& replaySprites) const {
    // リプレイ中の見た目復元に使うため、所有権を移さず参照だけ集める。
    replaySprites.reserve(replaySprites.size() + sprites_.size());
    for (const auto& sprite : sprites_) {
        if (sprite) {
            replaySprites.push_back(sprite.get());
        }
    }
}

void PauseMenuOverlay::CaptureReplayState(json& state) const {
    // 選択位置とアニメーション時刻を保存し、巻き戻しで同じ画面へ戻す。
    state = {{"active", isActive_},
             {"suppressInput", suppressOpenCloseInput_},
             {"selectedIndex", selectedIndex_},
             {"time", time_}};
}

void PauseMenuOverlay::RestoreReplayState(const json& state) {
    // ステージ選択では存在しない項目を選ばず、表示状態を保存時刻へ揃える。
    if (!state.is_object()) {
        return;
    }
    isActive_ = state.value("active", isActive_);
    suppressOpenCloseInput_ = state.value("suppressInput", suppressOpenCloseInput_);
    selectedIndex_ = std::clamp(state.value("selectedIndex", selectedIndex_), 0, static_cast<int>(Item::Count) - 1);
    if (mode_ == Mode::StageSelect && (selectedIndex_ == static_cast<int>(Item::Retry) ||
                                      selectedIndex_ == static_cast<int>(Item::StageSelect))) {
        selectedIndex_ = static_cast<int>(Item::Resume);
    }
    time_ = state.value("time", time_);
    SetAllVisible(isActive_);
    if (isActive_) {
        UpdateSprites();
    }
}

void PauseMenuOverlay::LoadLayout(const std::string& layoutPath) {
    // スプライトの配置は JSON を優先し、サイズだけ画面解像度へ換算します。
    sprites_.clear();
    if (!spriteCommon_) {
        return;
    }

    std::ifstream file(layoutPath);
    if (!file.is_open()) {
        return;
    }

    json root;
    try {
        file >> root;
    } catch (...) {
        return;
    }

    if (!root.contains("sprites") || !root["sprites"].is_array()) {
        return;
    }

    const auto layoutScale = SpriteLayoutScaler::Make(root);
    for (const auto& spriteData : root["sprites"]) {
        if (!spriteData.is_object()) {
            continue;
        }
        const std::string name = SpriteLayoutScaler::ReadString(spriteData, "name", "pause_sprite");
        const std::string texture = SpriteLayoutScaler::ReadString(spriteData, "texture", "common/white.png");
        const uint32_t textureHandle = Sprite::LoadTexture(texture.empty() ? "common/white.png" : texture);

        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon_, textureHandle);
        sprite->SetName(name);
        sprite->SetTextureName(texture);
        sprite->SetPosition(SpriteLayoutScaler::ScalePosition(
            SpriteLayoutScaler::ReadVector2(spriteData.value("position", json::array()), sprite->GetPosition()),
            layoutScale));
        sprite->SetSize(SpriteLayoutScaler::ScaleSize(
            SpriteLayoutScaler::ReadVector2(spriteData.value("size", json::array()), sprite->GetSize()), layoutScale));
        sprite->SetAnchorPoint(
            SpriteLayoutScaler::ReadVector2(spriteData.value("anchor", json::array()), sprite->GetAnchorPoint()));
        sprite->SetColor(SpriteLayoutScaler::ReadColor(spriteData.value("color", json::array()), sprite->GetColor()));
        sprite->SetEmissive(SpriteLayoutScaler::ReadFloat(spriteData, "emissive", 1.0f));
        sprite->SetVisible(false);
        sprite->Update();
        sprites_.push_back(std::move(sprite));
    }
}

void PauseMenuOverlay::BindLayoutSprites() {
    // JSON 内の名前と操作行を一度だけ結び、更新時の文字列探索を避ける。
    background_ = FindSprite("pause_background");
    panel_ = FindSprite("pause_panel");
    titlePlate_ = FindSprite("pause_title_plate");
    title_ = FindSprite("pause_title");
    hintBack_ = FindSprite("pause_hint_back");
    hintText_ = FindSprite("pause_hint_text");
    cursorSlime_ = FindSprite("pause_cursor_slime");
    cursorArrow_ = FindSprite("pause_cursor_arrow");

    constexpr std::array<const char*, 5> prefixes = {"pause_resume", "pause_retry", "pause_stage_select",
                                                     "pause_settings", "pause_title_return"};

    for (int i = 0; i < static_cast<int>(Item::Count); ++i) {
        const std::string prefix = prefixes[static_cast<size_t>(i)];
        rows_[static_cast<size_t>(i)].backdrop = FindSprite(prefix + "_row");
        rows_[static_cast<size_t>(i)].label = FindSprite(prefix + "_label");
        if (rows_[static_cast<size_t>(i)].backdrop) {
            rows_[static_cast<size_t>(i)].backdropBaseSize = rows_[static_cast<size_t>(i)].backdrop->GetSize();
        }
        if (rows_[static_cast<size_t>(i)].label) {
            rows_[static_cast<size_t>(i)].labelBaseSize = rows_[static_cast<size_t>(i)].label->GetSize();
        }
    }
}

Sprite* PauseMenuOverlay::FindSprite(const std::string& name) const {
    // レイアウトに不足があっても nullptr として扱い、UI 全体は継続する。
    for (const auto& sprite : sprites_) {
        if (sprite && sprite->GetName() == name) {
            return sprite.get();
        }
    }
    return nullptr;
}

void PauseMenuOverlay::SetAllVisible(bool visible) {
    // 画面を閉じる時は背景だけでなく選択カーソルも一括で隠す。
    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->SetVisible(visible);
        }
    }
}

bool PauseMenuOverlay::IsOpenOrCloseHeld() const {
    // キーボードとゲームパッドの閉じる入力を同じ抑制判定へまとめる。
    if (!inputManager_) {
        return false;
    }

    return inputManager_->IsKeyPressed(DIK_P) || inputManager_->IsKeyPressed(DIK_ESCAPE) ||
           inputManager_->IsGamepadButtonPressed(XINPUT_GAMEPAD_START) ||
           inputManager_->IsGamepadButtonPressed(XINPUT_GAMEPAD_B);
}

PauseMenuOverlay::Action PauseMenuOverlay::UpdateInput() {
    // 押下を一回だけ Action に変え、実際のシーン遷移は呼び出し側に任せる。
    if (!inputManager_) {
        return Action::None;
    }

    if (suppressOpenCloseInput_) {
        if (!IsOpenOrCloseHeld()) {
            suppressOpenCloseInput_ = false;
        }
        return Action::None;
    }

    const bool up = inputManager_->IsKeyTriggered(DIK_UP) || inputManager_->IsKeyTriggered(DIK_W) ||
                    inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP);
    const bool down = inputManager_->IsKeyTriggered(DIK_DOWN) || inputManager_->IsKeyTriggered(DIK_S) ||
                      inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN);

    if (up) {
        ChangeSelection(-1);
    }
    if (down) {
        ChangeSelection(1);
    }

    if (inputManager_->IsKeyTriggered(DIK_P) || inputManager_->IsKeyTriggered(DIK_ESCAPE) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_START) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B)) {
        return Action::Resume;
    }

    if (inputManager_->IsKeyTriggered(DIK_SPACE) || inputManager_->IsKeyTriggered(DIK_RETURN) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
        switch (static_cast<Item>(selectedIndex_)) {
        case Item::Resume:
            return Action::Resume;
        case Item::Retry:
            return Action::Retry;
        case Item::StageSelect:
            return Action::StageSelect;
        case Item::Settings:
            return Action::OpenSettings;
        case Item::Title:
            return Action::ReturnTitle;
        default:
            break;
        }
    }

    return Action::None;
}

void PauseMenuOverlay::UpdateSprites() {
    // 選択行だけ拡縮と脈動を適用し、カーソルをその行の配置へ追従させる。
    const Vector4 normalText = {0.48f, 0.36f, 0.24f, 0.90f};
    const Vector4 normalRowColor = {0.95f, 0.90f, 0.78f, 0.88f};

    for (int i = 0; i < static_cast<int>(Item::Count); ++i) {
        MenuRow& row = rows_[static_cast<size_t>(i)];
        const bool selected = selectedIndex_ == i;
        const float pulse = selected ? (0.5f + 0.5f * std::sin(time_ * 5.6f)) : 0.0f;
        const float labelScale = selected ? 1.03f + pulse * 0.025f : 0.94f;
        const float rowScale = selected ? 1.02f + pulse * 0.014f : 0.98f;
        const Vector4 selectedText = {1.0f, 1.0f, 1.0f, 1.0f};

        if (row.backdrop) {
            const uint32_t handle = Sprite::LoadTexture(selected ? "ui/settings/settings_row_selected.png"
                                                                 : "ui/settings/settings_row.png");
            row.backdrop->SetTextureHandle(handle);
            const auto& metadata = TextureManager::GetInstance()->GetMetadata(handle);
            row.backdrop->SetTextureRect({0.0f, 0.0f},
                                         {static_cast<float>(metadata.width), static_cast<float>(metadata.height)});
            row.backdrop->SetSize({row.backdropBaseSize.x * rowScale, row.backdropBaseSize.y * rowScale});
            row.backdrop->SetColor(selected ? Vector4{0.72f + pulse * 0.10f, 1.0f, 1.0f, 1.0f} : normalRowColor);
        }
        if (row.label) {
            row.label->SetSize({row.labelBaseSize.x * labelScale, row.labelBaseSize.y * labelScale});
            row.label->SetColor(selected ? selectedText : normalText);
        }
    }

    MenuRow& selectedRow = rows_[static_cast<size_t>(selectedIndex_)];
    if (selectedRow.backdrop) {
        const Vector2 rowPosition = selectedRow.backdrop->GetPosition();
        const Vector2 rowSize = selectedRow.backdrop->GetSize();
        const float pulse = 0.5f + 0.5f * std::sin(time_ * 5.6f);
        const float bob = std::sin(time_ * 7.5f) * 3.0f;
        if (cursorArrow_) {
            cursorArrow_->SetPosition({rowPosition.x - rowSize.x * 0.55f, rowPosition.y + bob});
            cursorArrow_->SetColor({1.0f, 0.88f + pulse * 0.08f, 0.34f, 1.0f});
        }
        if (cursorSlime_) {
            cursorSlime_->SetPosition({rowPosition.x - rowSize.x * 0.72f, rowPosition.y + bob - 2.0f});
            const float cursorScale = 1.0f + pulse * 0.045f;
            cursorSlime_->SetSize({70.0f * cursorScale, 70.0f * cursorScale});
            cursorSlime_->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
}

void PauseMenuOverlay::ChangeSelection(int direction) {
    // ステージ選択では使えない項目を飛ばし、上下操作で有効項目だけ巡回する。
    if (mode_ == Mode::StageSelect) {
        constexpr std::array<int, 3> kItems = {static_cast<int>(Item::Resume), static_cast<int>(Item::Settings),
                                               static_cast<int>(Item::Title)};
        const auto current = std::find(kItems.begin(), kItems.end(), selectedIndex_);
        const int index = current == kItems.end() ? 0 : static_cast<int>(current - kItems.begin());
        selectedIndex_ = kItems[static_cast<size_t>((index + direction + static_cast<int>(kItems.size())) %
                                                    static_cast<int>(kItems.size()))];
        return;
    }
    const int count = static_cast<int>(Item::Count);
    selectedIndex_ = (selectedIndex_ + direction + count) % count;
}
