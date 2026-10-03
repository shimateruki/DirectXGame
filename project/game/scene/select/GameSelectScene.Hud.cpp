#define NOMINMAX
#include "game/scene/select/GameSelectScene.h"
#include <algorithm>

namespace {
constexpr const char* kSpriteResourcePrefix = "Resources/sprite/";
std::string ToSpriteRelativePath(const std::string& path) {
	if (path.rfind(kSpriteResourcePrefix, 0) == 0) {
		return path.substr(std::string(kSpriteResourcePrefix).size());
	}
	return path;
}

}

// HUDはシーン所有のSpriteを借用する。更新時点・保存値の参照先・報酬演出との呼び出し順は変更しない。
Sprite* GameSelectScene::FindSpriteByName(const std::string& name) const {
	for (const auto& sprite : sprites_) {
		if (sprite && sprite->GetName() == name) {
			return sprite.get();
		}
	}
	return nullptr;
}

GameSelectScene::StageSelectHudSprite GameSelectScene::BindStageSelectHUDSprite(
	const std::string& name,
	const std::string& texturePath,
	const Vector2& position,
	const Vector2& size,
	const Vector2& anchor,
	const Vector4& color) {

	Sprite* sprite = FindSpriteByName(name);
	bool createdFromFallback = false;
	if (!sprite) {
		auto newSprite = std::make_unique<Sprite>();
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

	return { sprite, sprite->GetPosition(), sprite->GetSize(), sprite->GetColor() };
}

void GameSelectScene::InitializeStageSelectHUD() {
	const Vector4 white = { 1.0f, 1.0f, 1.0f, 0.96f };
	const Vector4 crownNumber = { 1.0f, 0.94f, 0.50f, 1.0f };
	const Vector4 starNumber = { 1.0f, 0.90f, 0.30f, 1.0f };
	const Vector4 coinNumber = { 1.0f, 0.82f, 0.24f, 1.0f };
	const Vector4 xColor = { 1.0f, 0.95f, 0.58f, 0.96f };
	const std::array<float, 3> rows = { 86.0f, 142.0f, 198.0f };
	const std::array<std::string, 3> iconTextures = {
		"Resources/sprite/ui/title/crown_progress_icon.png",
		"Resources/sprite/ui/hud/stage_star_filled.png",
		"Resources/sprite/ui/hud/coin_icon.png"
	};

	stageSelectCrownIcon_ = BindStageSelectHUDSprite("stage_select_hud_crown_icon", iconTextures[0], { 42.0f, rows[0] }, { 48.0f, 48.0f }, { 0.0f, 0.5f }, white);
	stageSelectCrownXIcon_ = BindStageSelectHUDSprite("stage_select_hud_crown_x", "Resources/sprite/ui/hud/xUi.png", { 94.0f, rows[0] }, { 38.0f, 38.0f }, { 0.5f, 0.5f }, xColor);
	stageSelectStarIcon_ = BindStageSelectHUDSprite("stage_select_hud_star_icon", iconTextures[1], { 42.0f, rows[1] }, { 48.0f, 48.0f }, { 0.0f, 0.5f }, white);
	stageSelectStarXIcon_ = BindStageSelectHUDSprite("stage_select_hud_star_x", "Resources/sprite/ui/hud/xUi.png", { 94.0f, rows[1] }, { 38.0f, 38.0f }, { 0.5f, 0.5f }, xColor);
	stageSelectCoinIcon_ = BindStageSelectHUDSprite("stage_select_hud_coin_icon", iconTextures[2], { 42.0f, rows[2] }, { 48.0f, 48.0f }, { 0.0f, 0.5f }, white);
	stageSelectCoinXIcon_ = BindStageSelectHUDSprite("stage_select_hud_coin_x", "Resources/sprite/ui/hud/xUi.png", { 94.0f, rows[2] }, { 38.0f, 38.0f }, { 0.5f, 0.5f }, xColor);

	const float digitStartX = 124.0f;
	const float digitSpacing = 25.0f;
	const Vector2 digitSize = { 24.0f, 36.0f };
	for (int i = 0; i < 3; ++i) {
		const float x = digitStartX + digitSpacing * static_cast<float>(i);
		stageSelectCrownDigits_[static_cast<size_t>(i)] = BindStageSelectHUDSprite(
			"stage_select_hud_crown_digit_" + std::to_string(i),
			"Resources/sprite/number/0.png",
			{ x, rows[0] },
			digitSize,
			{ 0.5f, 0.5f },
			crownNumber);
		stageSelectStarDigits_[static_cast<size_t>(i)] = BindStageSelectHUDSprite(
			"stage_select_hud_star_digit_" + std::to_string(i),
			"Resources/sprite/number/0.png",
			{ x, rows[1] },
			digitSize,
			{ 0.5f, 0.5f },
			starNumber);
		stageSelectCoinDigits_[static_cast<size_t>(i)] = BindStageSelectHUDSprite(
			"stage_select_hud_coin_digit_" + std::to_string(i),
			"Resources/sprite/number/0.png",
			{ x, rows[2] },
			digitSize,
			{ 0.5f, 0.5f },
			coinNumber);
	}
}

void GameSelectScene::SetStageSelectHUDNumber(std::array<StageSelectHudSprite, 3>& digits, int value, const Vector4& color, bool visible) {
	value = std::clamp(value, 0, 999);
	const std::array<int, 3> digitValues = {
		value / 100,
		(value / 10) % 10,
		value % 10
	};
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
			state.sprite->SetTextureHandle(Sprite::LoadTexture("number/" + std::to_string(digitValues[static_cast<size_t>(i)]) + ".png"));
			state.sprite->SetColor(color);
		}
		state.sprite->Update();
	}
}

void GameSelectScene::UpdateUI() {
	auto* save = GameDataManager::GetInstance();
	if (!save) {
		return;
	}

	const bool visible = true;
	const Vector4 crownNumber = { 1.0f, 0.94f, 0.50f, 1.0f };
	const Vector4 starNumber = { 1.0f, 0.90f, 0.30f, 1.0f };
	const Vector4 coinNumber = { 1.0f, 0.82f, 0.24f, 1.0f };
	const std::array<StageSelectHudSprite*, 6> icons = {
		&stageSelectCrownIcon_, &stageSelectCrownXIcon_,
		&stageSelectStarIcon_, &stageSelectStarXIcon_,
		&stageSelectCoinIcon_, &stageSelectCoinXIcon_
	};
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

