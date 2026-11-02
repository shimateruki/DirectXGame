#define NOMINMAX
#include "game/scene/select/GameSelectScene.h"
#include "Player.h"
#include "CameraManager.h"
#include "game/system/progress/StageManager.h"
#include "Log.h"
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

GameSelectScene::StageSelectHudSprite
GameSelectScene::BindStageSelectHUDSprite(const std::string& name) {
    // レイアウトの静的な画像・配置・色を保持し、数値と表示だけを更新する。
    engine::Sprite* sprite = GetSpriteByName(name);
    if (!sprite) {
        LOG_ERROR("stageSelect_sprite.json: Sprite '{}' was not found", name);
        return {};
    }

    sprite->SetVisible(true);
    sprite->Update();

    return {name, sprite->GetPosition(), sprite->GetSize(), sprite->GetColor()};
}

engine::Sprite* GameSelectScene::ResolveStageSelectHUDSprite(const StageSelectHudSprite& state) {
    // Editor や Replay が Sprite を削除・再生成しても、古い借用ポインタを使わない。
    return state.name.empty() ? nullptr : GetSpriteByName(state.name);
}

void GameSelectScene::InitializeStageSelectHUD() {
    // 全部品を配置JSONの名前だけで結ぶ。画像と位置はEditorで調整できる。
    stageSelectCrownIcon_ = BindStageSelectHUDSprite("stage_select_hud_crown_icon");
    stageSelectCrownXIcon_ = BindStageSelectHUDSprite("stage_select_hud_crown_x");
    stageSelectStarIcon_ = BindStageSelectHUDSprite("stage_select_hud_star_icon");
    stageSelectStarXIcon_ = BindStageSelectHUDSprite("stage_select_hud_star_x");
    stageSelectCoinIcon_ = BindStageSelectHUDSprite("stage_select_hud_coin_icon");
    stageSelectCoinXIcon_ = BindStageSelectHUDSprite("stage_select_hud_coin_x");

    // 桁数に応じた表示と画像の切り替えだけを更新側へ残す。
    for (int i = 0; i < 3; ++i) {
        stageSelectCrownDigits_[static_cast<size_t>(i)] = BindStageSelectHUDSprite(
            "stage_select_hud_crown_digit_" + std::to_string(i));
        stageSelectStarDigits_[static_cast<size_t>(i)] = BindStageSelectHUDSprite(
            "stage_select_hud_star_digit_" + std::to_string(i));
        stageSelectCoinDigits_[static_cast<size_t>(i)] = BindStageSelectHUDSprite(
            "stage_select_hud_coin_digit_" + std::to_string(i));
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

        if (tutorial || !stages[index].hasStarCoins) {
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
		// NDC座標をスクリーン座標に変換し、ラベルの中心が画面内に収まるように制限します。
        const float screenWidth = static_cast<float>(WinApp::GetClientWidth());
        const float screenHeight = static_cast<float>(WinApp::GetClientHeight());
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

void GameSelectScene::SetStageSelectHUDNumber(std::array<StageSelectHudSprite, 3>& digits, int value, bool visible) {
    // 桁数に応じて先頭のゼロを隠し、固定長の Sprite 配列を再利用する。
    value = std::clamp(value, 0, 999);
    const std::array<int, 3> digitValues = {value / 100, (value / 10) % 10, value % 10};
    const int digitCount = value >= 100 ? 3 : (value >= 10 ? 2 : 1);
    const int visibleStart = 3 - digitCount;

    for (int i = 0; i < 3; ++i) {
        StageSelectHudSprite& state = digits[static_cast<size_t>(i)];

        engine::Sprite* sprite = ResolveStageSelectHUDSprite(state);
        if (!sprite) {
            continue;
        }

        const bool digitVisible = visible && i >= visibleStart;
        sprite->SetVisible(digitVisible);

        if (digitVisible) {
            sprite->SetTextureHandle(
                engine::Sprite::LoadTexture("number/" + std::to_string(digitValues[static_cast<size_t>(i)]) + ".png"));
            sprite->SetColor(state.baseColor);
        }

        sprite->Update();
    }
}

void GameSelectScene::UpdateUI() {
    // 取得数をセーブデータから読み直し、ステージ選択へ戻った直後も最新値を見せる。
    auto* save = GameDataManager::GetInstance();

    if (!save) {
        return;
    }

    const bool visible = true;
    const std::array<StageSelectHudSprite*, 6> icons = {&stageSelectCrownIcon_, &stageSelectCrownXIcon_,
                                                        &stageSelectStarIcon_,  &stageSelectStarXIcon_,
                                                        &stageSelectCoinIcon_,  &stageSelectCoinXIcon_};
    for (StageSelectHudSprite* state : icons) {
        if (state) {
            if (engine::Sprite* sprite = ResolveStageSelectHUDSprite(*state)) {
                sprite->SetVisible(visible);
                sprite->Update();
            }
        }
    }

    SetStageSelectHUDNumber(stageSelectCrownDigits_, GetDisplayedCrownCount(), visible);
    SetStageSelectHUDNumber(stageSelectStarDigits_, save->GetCollectedStarCoinCount(), visible);
    SetStageSelectHUDNumber(stageSelectCoinDigits_, save->GetCoins(), visible);
    UpdateStageSelectCrownHudReward();
}
