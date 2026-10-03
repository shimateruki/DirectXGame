#define NOMINMAX
#include "game/scene/gameplay/GamePlayScene.h"
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "GimmickFactory.h"
#include "ItemFactory.h"
#include "LevelLoader.h"
#include "BaseEnemy.h"
#include "Object3d.h"
#include <algorithm>
#include <cmath>
#include <numbers>

// リプレイの再生成・状態退避・復元。JSONキーと復元順を維持し、保存形式は変更しない。
std::unique_ptr<Object3d> GamePlayScene::CreateReplayObject(const json& descriptor) {
    Object3dCommon* common = GetObject3dCommon();
    if (!common || !descriptor.is_object()) {
        return nullptr;
    }

    const std::string className = descriptor.value("className", std::string{});
    const std::string enemyType = descriptor.value("enemyType", std::string{});
    const std::string gimmickType = descriptor.value("gimmickType", std::string{});
    const std::string itemType = descriptor.value("itemType", std::string{});
    std::unique_ptr<Object3d> object;

    if (!enemyType.empty()) {
        object = EnemyFactory::GetInstance()->CreateEnemy(enemyType, common);
        if (!object || object->GetEnemyType() != enemyType) {
            return nullptr;
        }
    } else if (!gimmickType.empty()) {
        object = GimmickFactory::GetInstance()->CreateGimmick(gimmickType, common);
    } else if (!itemType.empty()) {
        object = ItemFactory::GetInstance()->CreateItem(itemType, common);
        if (!object || object->GetItemType() != itemType) {
            return nullptr;
        }
    } else if (className == "Model" || className == "Object") {
        object = std::make_unique<Object3d>();
        object->Initialize(common);
    }

    if (!object) {
        return nullptr;
    }

    object->SetName(descriptor.value("name", object->GetName()));
    object->SetClassName(className.empty() ? object->GetClassName() : className);
    object->SetSaveCategory(descriptor.value("saveCategory", object->GetSaveCategory()));
    if (!enemyType.empty()) object->SetEnemyType(enemyType);
    if (!gimmickType.empty()) object->SetGimmickType(gimmickType);
    if (!itemType.empty()) object->SetItemType(itemType);
    return object;
}

void GamePlayScene::OnReplayObjectsRecreated() {
    LevelLoader::ConfigureEnemyRuntimeReferences(this);
}

void GamePlayScene::CollectReplaySprites(std::vector<Sprite*>& replaySprites) {
	BaseScene::CollectReplaySprites(replaySprites);
	auto add = [&replaySprites](Sprite* sprite) {
		if (sprite) {
			replaySprites.push_back(sprite);
		}
	};

	add(lockOnSprite_.get());
	add(goalOverlayFlash_.get());
	add(goalOverlayReturnText_.get());
	for (const auto& letter : goalOverlayStageClearLetters_) {
		add(letter.get());
	}
	for (const auto& sparkle : goalOverlaySparkles_) {
		add(sparkle.get());
	}
	if (pauseMenuOverlay_) {
		pauseMenuOverlay_->CollectReplaySprites(replaySprites);
	}
	if (controlsGuideOverlay_) {
		controlsGuideOverlay_->CollectReplaySprites(replaySprites);
	}
	if (settingsOverlay_) {
		settingsOverlay_->CollectReplaySprites(replaySprites);
	}
	if (saveIndicatorOverlay_) {
		saveIndicatorOverlay_->CollectReplaySprites(replaySprites);
	}
}

void GamePlayScene::CaptureReplaySceneState(json& state) const {
	json pauseState = json::object();
	json controlsGuideState = json::object();
	json settingsState = json::object();
	json saveIndicatorState = json::object();
	if (pauseMenuOverlay_) pauseMenuOverlay_->CaptureReplayState(pauseState);
	if (controlsGuideOverlay_) controlsGuideOverlay_->CaptureReplayState(controlsGuideState);
	if (settingsOverlay_) settingsOverlay_->CaptureReplayState(settingsState);
	if (saveIndicatorOverlay_) saveIndicatorOverlay_->CaptureReplayState(saveIndicatorState);

	state = {
		{ "goal", {
			{ "active", isGoal_ },
			{ "wasStageCleared", goalWasStageCleared_ },
			{ "state", static_cast<int>(goalPresentationState_) },
			{ "timer", goalPresentationTimer_ },
			{ "starEmitTimer", goalStarEmitTimer_ },
			{ "burstEmitTimer", goalBurstEmitTimer_ },
			{ "crownIdleTime", goalCrownIdleTime_ },
			{ "crownSparkleTimer", goalCrownSparkleTimer_ },
			{ "crownSparklePattern", goalCrownSparklePatternIndex_ },
			{ "crownSpringPosition", { goalCrownSpringPosition_.x, goalCrownSpringPosition_.y, goalCrownSpringPosition_.z } },
			{ "crownSpringVelocity", { goalCrownSpringVelocity_.x, goalCrownSpringVelocity_.y, goalCrownSpringVelocity_.z } },
			{ "crownSpringRotation", { goalCrownSpringRotation_.x, goalCrownSpringRotation_.y, goalCrownSpringRotation_.z } },
			{ "crownSpringRotationVelocity", { goalCrownSpringRotationVelocity_.x, goalCrownSpringRotationVelocity_.y, goalCrownSpringRotationVelocity_.z } },
			{ "crownSpringInitialized", goalCrownSpringInitialized_ },
			{ "returnFadeStarted", goalReturnFadeStarted_ },
			{ "landingCuePlayed", goalLandingCuePlayed_ },
			{ "resultCuePlayed", goalResultCuePlayed_ },
			{ "cinematicPlaying", goalCinematicPlayer_.IsPlaying() }
		} },
		{ "hud", {
			{ "stageStarPulseTimers", hudStageStarPulseTimers_ },
			{ "stageStarCollected", hudStageStarVisualCollected_ },
			{ "lifeGainPulseTimer", hudLifeGainPulseTimer_ },
			{ "coinPulseTimer", hudCoinPulseTimer_ },
			{ "previousLives", hudPreviousLives_ },
			{ "previousCoins", hudPreviousCoins_ },
			{ "previousHp", hudPreviousHp_ },
			{ "damagePulseTimer", hudDamagePulseTimer_ },
			{ "hurtIconTimer", hudHurtIconTimer_ },
			{ "hpDamageHoldTimer", hudHpDamageHoldTimer_ },
			{ "hpDelayedRate", hudHpDelayedRate_ },
			{ "hpAnimationTimer", hudHpAnimationTimer_ },
			{ "morphGaugeTimer", hudMorphGaugeTimer_ },
			{ "morphGaugeVisibleTimer", hudMorphGaugeVisibleTimer_ }
		} },
		{ "lifeLost", {
			{ "active", lifeLostPresentationActive_ },
			{ "finished", lifeLostPresentationFinished_ },
			{ "blackHold", lifeLostBlackHold_ },
			{ "numberDropped", lifeLostNumberDropped_ },
			{ "timer", lifeLostPresentationTimer_ },
			{ "beforeLives", lifeLostBeforeLives_ },
			{ "afterLives", lifeLostAfterLives_ },
			{ "revive", lifeLostRevive_ }
		} },
		{ "overlays", {
			{ "pause", std::move(pauseState) },
			{ "controlsGuide", std::move(controlsGuideState) },
			{ "settings", std::move(settingsState) },
			{ "saveIndicator", std::move(saveIndicatorState) }
		} }
	};
}

void GamePlayScene::RestoreReplaySceneState(const json& state) {
	if (!state.is_object()) {
		return;
	}

	auto restoreVector3 = [](const json& object, const char* key, Vector3& value) {
		const auto found = object.find(key);
		if (found != object.end() && found->is_array() && found->size() >= 3) {
			value = { (*found)[0].get<float>(), (*found)[1].get<float>(), (*found)[2].get<float>() };
		}
	};

	if (const auto found = state.find("goal"); found != state.end() && found->is_object()) {
		const json& goal = *found;
		isGoal_ = goal.value("active", isGoal_);
		goalWasStageCleared_ = goal.value("wasStageCleared", goalWasStageCleared_);
		const int stateValue = std::clamp(goal.value("state", static_cast<int>(goalPresentationState_)), 0, 3);
		goalPresentationState_ = static_cast<GoalPresentationState>(stateValue);
		goalPresentationTimer_ = goal.value("timer", goalPresentationTimer_);
		goalStarEmitTimer_ = goal.value("starEmitTimer", goalStarEmitTimer_);
		goalBurstEmitTimer_ = goal.value("burstEmitTimer", goalBurstEmitTimer_);
		goalCrownIdleTime_ = goal.value("crownIdleTime", goalCrownIdleTime_);
		goalCrownSparkleTimer_ = goal.value("crownSparkleTimer", goalCrownSparkleTimer_);
		goalCrownSparklePatternIndex_ = goal.value("crownSparklePattern", goalCrownSparklePatternIndex_);
		restoreVector3(goal, "crownSpringPosition", goalCrownSpringPosition_);
		restoreVector3(goal, "crownSpringVelocity", goalCrownSpringVelocity_);
		restoreVector3(goal, "crownSpringRotation", goalCrownSpringRotation_);
		restoreVector3(goal, "crownSpringRotationVelocity", goalCrownSpringRotationVelocity_);
		goalCrownSpringInitialized_ = goal.value("crownSpringInitialized", goalCrownSpringInitialized_);
		goalReturnFadeStarted_ = goal.value("returnFadeStarted", goalReturnFadeStarted_);
		goalLandingCuePlayed_ = goal.value("landingCuePlayed", goalLandingCuePlayed_);
		goalResultCuePlayed_ = goal.value("resultCuePlayed", goalResultCuePlayed_);

		if (goalCinematicTimelineLoaded_ && isGoal_ && goalPresentationState_ != GoalPresentationState::Inactive) {
			const bool shouldPlay = goal.value("cinematicPlaying", goalCinematicPlayer_.IsPlaying());
			goalCinematicPlayer_.SetTime(goalPresentationTimer_, false);
			if (shouldPlay && !goalCinematicPlayer_.IsPlaying()) {
				goalCinematicPlayer_.Resume(false);
			} else if (!shouldPlay && goalCinematicPlayer_.IsPlaying()) {
				goalCinematicPlayer_.Pause();
			}
		}
	}

	if (const auto found = state.find("hud"); found != state.end() && found->is_object()) {
		const json& hud = *found;
		if (const auto values = hud.find("stageStarPulseTimers"); values != hud.end() && values->is_array()) {
			for (size_t i = 0; i < hudStageStarPulseTimers_.size() && i < values->size(); ++i) {
				hudStageStarPulseTimers_[i] = (*values)[i].get<float>();
			}
		}
		if (const auto values = hud.find("stageStarCollected"); values != hud.end() && values->is_array()) {
			for (size_t i = 0; i < hudStageStarVisualCollected_.size() && i < values->size(); ++i) {
				hudStageStarVisualCollected_[i] = (*values)[i].get<bool>();
			}
		}
		hudLifeGainPulseTimer_ = hud.value("lifeGainPulseTimer", hudLifeGainPulseTimer_);
		hudCoinPulseTimer_ = hud.value("coinPulseTimer", hudCoinPulseTimer_);
		hudPreviousLives_ = hud.value("previousLives", hudPreviousLives_);
		hudPreviousCoins_ = hud.value("previousCoins", hudPreviousCoins_);
		hudPreviousHp_ = hud.value("previousHp", hudPreviousHp_);
		hudDamagePulseTimer_ = hud.value("damagePulseTimer", hudDamagePulseTimer_);
		hudHurtIconTimer_ = hud.value("hurtIconTimer", hudHurtIconTimer_);
		hudHpDamageHoldTimer_ = hud.value("hpDamageHoldTimer", hudHpDamageHoldTimer_);
		hudHpDelayedRate_ = hud.value("hpDelayedRate", hudHpDelayedRate_);
		hudHpAnimationTimer_ = hud.value("hpAnimationTimer", hudHpAnimationTimer_);
		hudMorphGaugeTimer_ = hud.value("morphGaugeTimer", hudMorphGaugeTimer_);
		hudMorphGaugeVisibleTimer_ = hud.value("morphGaugeVisibleTimer", hudMorphGaugeVisibleTimer_);
	}

	if (const auto found = state.find("lifeLost"); found != state.end() && found->is_object()) {
		const json& lifeLost = *found;
		lifeLostPresentationActive_ = lifeLost.value("active", lifeLostPresentationActive_);
		lifeLostPresentationFinished_ = lifeLost.value("finished", lifeLostPresentationFinished_);
		lifeLostBlackHold_ = lifeLost.value("blackHold", lifeLostBlackHold_);
		lifeLostNumberDropped_ = lifeLost.value("numberDropped", lifeLostNumberDropped_);
		lifeLostPresentationTimer_ = lifeLost.value("timer", lifeLostPresentationTimer_);
		lifeLostBeforeLives_ = lifeLost.value("beforeLives", lifeLostBeforeLives_);
		lifeLostAfterLives_ = lifeLost.value("afterLives", lifeLostAfterLives_);
		lifeLostRevive_ = lifeLost.value("revive", lifeLostRevive_);
	}

	if (const auto found = state.find("overlays"); found != state.end() && found->is_object()) {
		const json& overlays = *found;
		if (pauseMenuOverlay_ && overlays.contains("pause")) {
			pauseMenuOverlay_->RestoreReplayState(overlays["pause"]);
		}
		if (controlsGuideOverlay_ && overlays.contains("controlsGuide")) {
			controlsGuideOverlay_->RestoreReplayState(overlays["controlsGuide"]);
		}
		if (settingsOverlay_ && overlays.contains("settings")) {
			settingsOverlay_->RestoreReplayState(overlays["settings"]);
		}
		if (saveIndicatorOverlay_ && overlays.contains("saveIndicator")) {
			saveIndicatorOverlay_->RestoreReplayState(overlays["saveIndicator"]);
		}
	}
}

