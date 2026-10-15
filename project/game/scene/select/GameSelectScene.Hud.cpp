#define NOMINMAX
#include "game/scene/select/GameSelectScene.h"
#include "Player.h"
#include "CameraManager.h"
#include "StageManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr const char* kSpriteResourcePrefix = "Resources/sprite/";
std::string ToSpriteRelativePath(const std::string& path) {
    // Sprite 管理器が期待する Resources/sprite 以下の相対名へ揃える。
    if (path.rfind(kSpriteResourcePrefix, 0) == 0) {
        return path.substr(std::string(kSpriteResourcePrefix).size());
    }
    return path;
}

} // namespace

// HUDはシーン所有のSpriteを借用する。更新時点・保存値の参照先・報酬演出との呼び出し順は変更しない。
engine::Sprite* GameSelectScene::FindSpriteByName(const std::string& name) const {
    // シーン所有の既存 Sprite を先に探し、再初期化による二重表示を避ける。
    for (const auto& sprite : sprites_) {
        if (sprite && sprite->GetName() == name) {
            return sprite.get();
        }
    }
    return nullptr;
}

GameSelectScene::StageSelectHudSprite
GameSelectScene::BindStageSelectHUDSprite(const std::string& name, const std::string& texturePath,
                                          const engine::Vector2& position, const engine::Vector2& size, const engine::Vector2& anchor,
                                          const engine::Vector4& color) {
    // JSON に配置済みならその Sprite を使い、ない物だけ補助生成する。

    engine::Sprite* sprite = FindSpriteByName(name);
    bool createdFromFallback = false;
    if (!sprite) {
        auto newSprite = std::make_unique<engine::Sprite>();
        newSprite->Initialize(spriteCommon_.get(), texturePath);
        newSprite->SetName(name);
        newSprite->SetTextureName(ToSpriteRelativePath(texturePath));
        sprite = newSprite.get();
        sprites_.push_back(std::move(newSprite));
        createdFromFallback = true;
    }

    if (sprite->GetTextureName().empty()) {
        sprite->SetTextureName(ToSpriteRelativePath(texturePath));
    }

    if (createdFromFallback) {
        sprite->SetPosition(position);
        sprite->SetSize(size);
        sprite->SetAnchorPoint(anchor);
        sprite->SetColor(color);
    }
    sprite->SetVisible(true);
    sprite->Update();

    return {sprite, sprite->GetPosition(), sprite->GetSize(), sprite->GetColor()};
}

void GameSelectScene::InitializeStageSelectHUD() {
    // 桁ごとの Sprite を先に結び、毎フレームの数値更新で生成し直さない。
    const engine::Vector4 white = {1.0f, 1.0f, 1.0f, 0.96f};
    const engine::Vector4 crownNumber = {1.0f, 0.94f, 0.50f, 1.0f};
    const engine::Vector4 starNumber = {1.0f, 0.90f, 0.30f, 1.0f};
    const engine::Vector4 coinNumber = {1.0f, 0.82f, 0.24f, 1.0f};
    const engine::Vector4 xColor = {1.0f, 0.95f, 0.58f, 0.96f};
    const std::array<float, 3> rows = {86.0f, 142.0f, 198.0f};
    const std::array<std::string, 3> iconTextures = {"Resources/sprite/ui/title/crown_progress_icon.png",
                                                     "Resources/sprite/ui/hud/stage_star_filled.png",
                                                     "Resources/sprite/ui/hud/coin_icon.png"};

    stageSelectCrownIcon_ = BindStageSelectHUDSprite("stage_select_hud_crown_icon", iconTextures[0], {42.0f, rows[0]},
                                                     {48.0f, 48.0f}, {0.0f, 0.5f}, white);
    stageSelectCrownXIcon_ = BindStageSelectHUDSprite("stage_select_hud_crown_x", "Resources/sprite/ui/hud/xUi.png",
                                                      {94.0f, rows[0]}, {38.0f, 38.0f}, {0.5f, 0.5f}, xColor);
    stageSelectStarIcon_ = BindStageSelectHUDSprite("stage_select_hud_star_icon", iconTextures[1], {42.0f, rows[1]},
                                                    {48.0f, 48.0f}, {0.0f, 0.5f}, white);
    stageSelectStarXIcon_ = BindStageSelectHUDSprite("stage_select_hud_star_x", "Resources/sprite/ui/hud/xUi.png",
                                                     {94.0f, rows[1]}, {38.0f, 38.0f}, {0.5f, 0.5f}, xColor);
    stageSelectCoinIcon_ = BindStageSelectHUDSprite("stage_select_hud_coin_icon", iconTextures[2], {42.0f, rows[2]},
                                                    {48.0f, 48.0f}, {0.0f, 0.5f}, white);
    stageSelectCoinXIcon_ = BindStageSelectHUDSprite("stage_select_hud_coin_x", "Resources/sprite/ui/hud/xUi.png",
                                                     {94.0f, rows[2]}, {38.0f, 38.0f}, {0.5f, 0.5f}, xColor);

    const float digitStartX = 124.0f;
    const float digitSpacing = 25.0f;
    const engine::Vector2 digitSize = {24.0f, 36.0f};
    for (int i = 0; i < 3; ++i) {
        const float x = digitStartX + digitSpacing * static_cast<float>(i);
        stageSelectCrownDigits_[static_cast<size_t>(i)] = BindStageSelectHUDSprite(
            "stage_select_hud_crown_digit_" + std::to_string(i), "Resources/sprite/number/0.png", {x, rows[0]},
            digitSize, {0.5f, 0.5f}, crownNumber);
        stageSelectStarDigits_[static_cast<size_t>(i)] = BindStageSelectHUDSprite(
            "stage_select_hud_star_digit_" + std::to_string(i), "Resources/sprite/number/0.png", {x, rows[1]},
            digitSize, {0.5f, 0.5f}, starNumber);
        stageSelectCoinDigits_[static_cast<size_t>(i)] = BindStageSelectHUDSprite(
            "stage_select_hud_coin_digit_" + std::to_string(i), "Resources/sprite/number/0.png", {x, rows[2]},
            digitSize, {0.5f, 0.5f}, coinNumber);
    }
}

void GameSelectScene::InitializeStageGateNameLabels() {
    const auto& stages = StageManager::GetInstance()->GetStages();
    // 最後のスロットをチュートリアル専用にし、通常ステージの添字は変えません。
    const size_t tutorialIndex = stages.size();
    stageGateNameLabels_.clear();
    stageGateNameLabels_.resize(stages.size() + 1);
    stageGateStarIcons_.clear();
    stageGateStarIcons_.resize(stages.size() + 1);
    for (size_t index = 0; index <= tutorialIndex; ++index) {
        const bool tutorial = index == tutorialIndex;
        const std::string texturePath = tutorial ? "Resources/sprite/ui/stage_select/tutorial_gate_name.png"
                                                 : stages[index].gateNameTexturePath;
        engine::Object3d* gate = tutorial ? FindObjectByName("TutorialArrivalGate")
                                  : FindStageGateByIndex(static_cast<int>(index));
        if (texturePath.empty() || !gate) {
            continue;
        }

        auto label = std::make_unique<engine::Sprite>();
        label->Initialize(spriteCommon_.get(), texturePath);
        label->SetName("stage_gate_name_" + std::to_string(index));
        label->SetTextureName(ToSpriteRelativePath(texturePath));
        label->SetAnchorPoint({0.5f, 1.0f});
        const engine::Vector2 textureSize = label->GetSize();
        const float width = 350.0f;
        label->SetSize({width, width * textureSize.y / std::max(textureSize.x, 1.0f)});
        label->SetVisible(false);
        label->Update();
        stageGateNameLabels_[index] = std::move(label);

        if (tutorial) {
            continue;
        }
        // 名札と同じスクリーン座標で描き、セーブ済みの3個の収集状況を並べます。
        for (int coinIndex = 0; coinIndex < GameDataManager::kStarCoinsPerStage; ++coinIndex) {
            auto star = std::make_unique<engine::Sprite>();
            star->Initialize(spriteCommon_.get(), "Resources/sprite/ui/hud/stage_star_empty.png");
            star->SetName("stage_gate_star_" + std::to_string(index) + "_" + std::to_string(coinIndex));
            star->SetTextureName("ui/hud/stage_star_empty.png");
            star->SetAnchorPoint({0.5f, 0.5f});
            star->SetSize({42.0f, 42.0f});
            star->SetVisible(false);
            star->Update();
            stageGateStarIcons_[index][static_cast<size_t>(coinIndex)] = std::move(star);
        }
    }
}

void GameSelectScene::UpdateStageGateNameLabels() {
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    const bool showLabels = player_ && camera && !isChangingStage_ && !gateEntryCinematicActive_ &&
                            !gateReturnPresentationActive_ && !crownCountPresentationActive_ &&
                            unlockingStageIndex_ < 0;
    engine::Math math;
    const engine::Matrix4x4 viewProjection = showLabels
        ? math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix())
        : math.MakeIdentity4x4();

    for (size_t index = 0; index < stageGateNameLabels_.size(); ++index) {
        const bool tutorial = index == stageGateNameLabels_.size() - 1;
        engine::Sprite* label = stageGateNameLabels_[index].get();
        if (!label) {
            continue;
        }
        auto hide = [this, index, label]() {
            label->SetVisible(false);
            for (auto& star : stageGateStarIcons_[index]) {
                if (star) {
                    star->SetVisible(false);
                }
            }
        };
        engine::Object3d* gate = showLabels ? (tutorial ? FindObjectByName("TutorialArrivalGate")
                                            : FindStageGateByIndex(static_cast<int>(index))) : nullptr;
        if (!gate) {
            hide();
            continue;
        }

        const engine::Vector3 gatePosition = gate->GetWorldPosition();
        const engine::Vector3 playerPosition = player_->GetWorldPosition();
        const float dx = gatePosition.x - playerPosition.x;
        const float dz = gatePosition.z - playerPosition.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        // 中央から識別できる距離では固定サイズで見せ、橋に差しかかる距離から消します。
        // チュートリアル入口は初期位置の近くにあるため、通常ゲートと同じ40mでは常時隠れます。
        const float hideDistance = tutorial ? 8.0f : gateNameHideDistance_;
        const float fadeDistance = tutorial ? 6.0f : gateNameFadeDistance_;
        const float alpha = std::clamp((distance - hideDistance) /
                                           std::max(fadeDistance, 0.1f), 0.0f, 1.0f);
        if (alpha <= 0.0f) {
            hide();
            continue;
        }

        const AABB bounds = gate->GetModelWorldAABB();
        const engine::Vector3 worldPosition = {gatePosition.x, bounds.max.y + gateNameHeightOffset_, gatePosition.z};
        const float w = worldPosition.x * viewProjection.m[0][3] + worldPosition.y * viewProjection.m[1][3] +
                        worldPosition.z * viewProjection.m[2][3] + viewProjection.m[3][3];
        if (w <= 0.001f) {
            hide();
            continue;
        }
        const engine::Vector3 ndc = engine::Math::Transform(worldPosition, viewProjection);
        if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || !std::isfinite(ndc.z) ||
            ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f ||
            ndc.z < 0.0f || ndc.z > 1.0f) {
            hide();
            continue;
        }

        const float screenWidth = static_cast<float>(WinApp::kClientWidth);
        const float screenHeight = static_cast<float>(WinApp::kClientHeight);
        const float halfWidth = label->GetSize().x * 0.5f;
        const float screenX = std::clamp((ndc.x + 1.0f) * 0.5f * screenWidth, halfWidth, screenWidth - halfWidth);
        const float screenY = (1.0f - ndc.y) * 0.5f * screenHeight;
        label->SetPosition({screenX, screenY - 34.0f});
        label->SetColor({1.0f, 1.0f, 1.0f, alpha});
        label->SetVisible(true);
        label->Update();

        auto* save = GameDataManager::GetInstance();
        if (tutorial) {
            continue;
        }
        for (int coinIndex = 0; coinIndex < GameDataManager::kStarCoinsPerStage; ++coinIndex) {
            engine::Sprite* star = stageGateStarIcons_[index][static_cast<size_t>(coinIndex)].get();
            if (!star) {
                continue;
            }
            const bool collected = save && save->IsStarCoinCollected(static_cast<int>(index), coinIndex);
            const std::string texture = collected ? "ui/hud/stage_star_filled.png" : "ui/hud/stage_star_empty.png";
            if (star->GetTextureName() != texture) {
                star->SetTextureHandle(engine::Sprite::LoadTexture(texture));
                star->SetTextureName(texture);
            }
            star->SetPosition({screenX + (static_cast<float>(coinIndex) - 1.0f) * 50.0f, screenY - 7.0f});
            star->SetColor({1.0f, 1.0f, 1.0f, alpha});
            star->SetVisible(true);
            star->Update();
        }
    }
}

void GameSelectScene::SetStageSelectHUDNumber(std::array<StageSelectHudSprite, 3>& digits, int value,
                                              const engine::Vector4& color, bool visible) {
    // 桁数に応じて先頭のゼロを隠し、固定長の Sprite 配列を再利用する。
    value = std::clamp(value, 0, 999);
    const std::array<int, 3> digitValues = {value / 100, (value / 10) % 10, value % 10};
    const int digitCount = value >= 100 ? 3 : (value >= 10 ? 2 : 1);
    const int visibleStart = 3 - digitCount;

    for (int i = 0; i < 3; ++i) {
        StageSelectHudSprite& state = digits[static_cast<size_t>(i)];
        if (!state.sprite) {
            continue;
        }

        const bool digitVisible = visible && i >= visibleStart;
        state.sprite->SetVisible(digitVisible);
        if (digitVisible) {
            state.sprite->SetTextureHandle(
                engine::Sprite::LoadTexture("number/" + std::to_string(digitValues[static_cast<size_t>(i)]) + ".png"));
            state.sprite->SetColor(color);
        }
        state.sprite->Update();
    }
}

void GameSelectScene::UpdateUI() {
    // 取得数をセーブデータから読み直し、ステージ選択へ戻った直後も最新値を見せる。
    auto* save = GameDataManager::GetInstance();
    if (!save) {
        return;
    }

    const bool visible = true;
    const engine::Vector4 crownNumber = {1.0f, 0.94f, 0.50f, 1.0f};
    const engine::Vector4 starNumber = {1.0f, 0.90f, 0.30f, 1.0f};
    const engine::Vector4 coinNumber = {1.0f, 0.82f, 0.24f, 1.0f};
    const std::array<StageSelectHudSprite*, 6> icons = {&stageSelectCrownIcon_, &stageSelectCrownXIcon_,
                                                        &stageSelectStarIcon_,  &stageSelectStarXIcon_,
                                                        &stageSelectCoinIcon_,  &stageSelectCoinXIcon_};
    for (StageSelectHudSprite* state : icons) {
        if (state && state->sprite) {
            state->sprite->SetVisible(visible);
            state->sprite->Update();
        }
    }

    SetStageSelectHUDNumber(stageSelectCrownDigits_, GetDisplayedCrownCount(), crownNumber, visible);
    SetStageSelectHUDNumber(stageSelectStarDigits_, save->GetCollectedStarCoinCount(), starNumber, visible);
    SetStageSelectHUDNumber(stageSelectCoinDigits_, save->GetCoins(), coinNumber, visible);
    UpdateStageSelectCrownHudReward();
}
