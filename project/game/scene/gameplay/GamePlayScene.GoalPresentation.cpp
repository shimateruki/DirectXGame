#define NOMINMAX
#include "game/scene/gameplay/GamePlayScene.h"
#include "BaseEnemy.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "SceneManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <fstream>
#include <cstdio>
#ifdef USE_IMGUI
#include "imgui.h"
#include "IconsFontAwesome5.h"
#endif

// ゴール演出のタイムライン・設定・画面表示。演出時間とプレビュー時の保存抑止条件は変更しない。
namespace {

constexpr const char* kGoalPresentationTuningPath = "Resources/json/cinematic/goal_clear.json";
constexpr const char* kGoalCinematicTimelinePath = "Resources/json/scenario/goal_clear_timeline.json";

float GoalClamp01(float value) {
	return std::clamp(value, 0.0f, 1.0f);
}

float GoalEaseOut(float value) {
	value = GoalClamp01(value);
	const float inv = 1.0f - value;
	return 1.0f - inv * inv * inv;
}

float GoalEaseInOut(float value) {
	value = GoalClamp01(value);
	return value * value * (3.0f - 2.0f * value);
}

}
void GamePlayScene::InitializeGoalCinematicTimeline() {
    goalCinematicPlayer_.Initialize(SceneManager::GetInstance());
    goalCinematicPlayer_.SetAnimationCallback(
        [this](Object3d* target, const CinematicAnimationClipData& clip, float localTime, bool isPreview) {
            HandleGoalCinematicAnimation(target, clip, localTime, isPreview);
        });
    goalCinematicPlayer_.SetSignalCallback(
        [this](Object3d* target, const CinematicSignalMarker& signal, bool isPreview) {
            HandleGoalCinematicSignal(target, signal, isPreview);
        });

    goalCinematicTimelineLoaded_ = goalCinematicSequence_.Load(kGoalCinematicTimelinePath);
    if (!goalCinematicTimelineLoaded_) {
        DebugConsole::GetInstance()->AddLog(
            std::string("Goal Timeline load failed: ") + kGoalCinematicTimelinePath);
        goalCinematicSequence_.Clear();
        goalCinematicSequence_.name = "goal_clear_timeline";
        SyncGoalCinematicTimelineFromTuning();
        goalCinematicTimelineLoaded_ = true;
    } else {
        ApplyGoalCinematicTimingFromSequence();
    }
    goalCinematicPlayer_.SetSequence(&goalCinematicSequence_);
}

void GamePlayScene::ApplyGoalCinematicTimingFromSequence() {
    GoalClearPlayerAnimator::Tuning animation = goalClearPlayerAnimator_.GetTuning();
    for (const auto& marker : goalCinematicSequence_.signals) {
        if (!marker.enabled) {
            continue;
        }
        if (marker.signal == "goal.crown_focus_end") {
            goalPresentationTuning_.crownFocusEndTime = marker.time;
        } else if (marker.signal == "goal.crown_move_start") {
            goalPresentationTuning_.crownMoveStartTime = marker.time;
        } else if (marker.signal == "goal.crown_land") {
            animation.crownLandTime = marker.time;
        } else if (marker.signal == "goal.anticipation") {
            animation.anticipationStartTime = marker.time;
        } else if (marker.signal == "goal.jump") {
            animation.jumpStartTime = marker.time;
        } else if (marker.signal == "goal.apex") {
            animation.apexTime = marker.time;
        } else if (marker.signal == "goal.result") {
            animation.resultUiTime = marker.time;
        } else if (marker.signal == "goal.victory_land") {
            animation.victoryLandTime = marker.time;
        } else if (marker.signal == "goal.ready") {
            animation.readyTime = marker.time;
        }
    }
    goalClearPlayerAnimator_.SetTuning(animation);
    SanitizeGoalPresentationTuning();
}

void GamePlayScene::SyncGoalCinematicTimelineFromTuning() {
    const GoalClearPlayerAnimator::Tuning& animation = goalClearPlayerAnimator_.GetTuning();
    auto SetSignalTime = [this](const char* signalId, const char* displayName, float time) {
        for (auto& marker : goalCinematicSequence_.signals) {
            if (marker.signal == signalId) {
                marker.time = time;
                marker.enabled = true;
                return;
            }
        }
        CinematicSignalMarker marker;
        marker.signal = signalId;
        marker.name = displayName;
        marker.time = time;
        goalCinematicSequence_.signals.push_back(marker);
    };

    SetSignalTime("goal.crown_focus_end", "Crown Focus End", goalPresentationTuning_.crownFocusEndTime);
    SetSignalTime("goal.crown_move_start", "Crown Move Start", goalPresentationTuning_.crownMoveStartTime);
    SetSignalTime("goal.crown_land", "Crown Land", animation.crownLandTime);
    SetSignalTime("goal.anticipation", "Anticipation", animation.anticipationStartTime);
    SetSignalTime("goal.jump", "Celebration Bounce", animation.jumpStartTime);
    SetSignalTime("goal.apex", "First Bounce Apex", animation.apexTime);
    SetSignalTime("goal.result", "Result UI", animation.resultUiTime);
    SetSignalTime("goal.victory_land", "First Bounce Landing", animation.victoryLandTime);
    SetSignalTime("goal.ready", "Ready To Return", animation.readyTime);

    bool hasAnimationDriver = false;
    for (auto& clip : goalCinematicSequence_.animationClips) {
        if (clip.driver == "GoalClearPlayer") {
            clip.startTime = 0.0f;
            clip.duration = animation.readyTime;
            clip.binding.targetName = player_ ? player_->GetName() : "player";
            clip.binding.targetEventId = player_ ? player_->GetEventID() : -1;
            hasAnimationDriver = true;
        }
    }
    if (!hasAnimationDriver) {
        CinematicAnimationClipData clip;
        clip.name = "Player Goal Celebration";
        clip.driver = "GoalClearPlayer";
        clip.clipName = "goal_clear";
        clip.duration = animation.readyTime;
        clip.binding.targetName = player_ ? player_->GetName() : "player";
        clip.binding.targetEventId = player_ ? player_->GetEventID() : -1;
        goalCinematicSequence_.animationClips.push_back(clip);
    }

    bool hasCrownFocusVfx = false;
    bool hasCrownVfx = false;
    bool hasResultVfx = false;
    bool hasVictoryLandVfx = false;
    for (auto& track : goalCinematicSequence_.vfxTracks) {
        if (track.sequenceName.rfind("crown_focus", 0) == 0) {
            track.startTime = std::max(0.05f, goalPresentationTuning_.crownFocusEndTime - 0.22f);
            hasCrownFocusVfx = true;
        } else if (track.sequenceName.rfind("crown_get", 0) == 0) {
            track.startTime = animation.crownLandTime;
            hasCrownVfx = true;
        } else if (track.sequenceName.rfind("crown_result", 0) == 0) {
            track.startTime = animation.apexTime;
            hasResultVfx = true;
        } else if (track.sequenceName.rfind("crown_victory_land", 0) == 0) {
            track.startTime = animation.victoryLandTime;
            hasVictoryLandVfx = true;
        }
    }
    Object3d* crown = FindGoalCrownObject();
    if (!hasCrownFocusVfx) {
        CinematicVFXTrackData track;
        track.name = "Crown Focus VFX";
        track.sequenceName = "crown_focus_cue";
        track.startTime = std::max(0.05f, goalPresentationTuning_.crownFocusEndTime - 0.22f);
        track.binding.targetName = crown ? crown->GetName() : "goal";
        track.binding.targetEventId = crown ? crown->GetEventID() : -1;
        goalCinematicSequence_.vfxTracks.push_back(track);
    }
    if (!hasCrownVfx) {
        CinematicVFXTrackData track;
        track.name = "Crown Landing VFX";
        track.sequenceName = "crown_get_cue";
        track.startTime = animation.crownLandTime;
        track.binding.targetName = crown ? crown->GetName() : "goal";
        track.binding.targetEventId = crown ? crown->GetEventID() : -1;
        goalCinematicSequence_.vfxTracks.push_back(track);
    }
    if (!hasResultVfx) {
        CinematicVFXTrackData track;
        track.name = "Result Burst VFX";
        track.sequenceName = "crown_result_cue";
        track.startTime = animation.apexTime;
        track.binding.targetName = player_ ? player_->GetName() : "player";
        track.binding.targetEventId = player_ ? player_->GetEventID() : -1;
        goalCinematicSequence_.vfxTracks.push_back(track);
    }
    if (!hasVictoryLandVfx) {
        CinematicVFXTrackData track;
        track.name = "Victory Landing VFX";
        track.sequenceName = "crown_victory_land_cue";
        track.startTime = animation.victoryLandTime;
        track.binding.targetName = player_ ? player_->GetName() : "player";
        track.binding.targetEventId = player_ ? player_->GetEventID() : -1;
        goalCinematicSequence_.vfxTracks.push_back(track);
    }

    bool hasJumpAudio = false;
    for (auto& clip : goalCinematicSequence_.audioClips) {
        if (clip.audioId == "slime_stretch") {
            clip.startTime = animation.jumpStartTime;
            hasJumpAudio = true;
        }
    }
    if (!hasJumpAudio) {
        CinematicAudioClipData clip;
        clip.name = "Jump Stretch SE";
        clip.audioId = "slime_stretch";
        clip.startTime = animation.jumpStartTime;
        clip.duration = 0.2f;
        clip.volume = 0.85f;
        goalCinematicSequence_.audioClips.push_back(clip);
    }
    goalCinematicSequence_.duration = animation.readyTime;
    goalCinematicSequence_.Sort();
}

void GamePlayScene::HandleGoalCinematicAnimation(
    Object3d* target,
    const CinematicAnimationClipData& clip,
    float localTime,
    bool isPreview) {
    (void)isPreview;
    if (clip.driver != "GoalClearPlayer" || !player_ || (target && target != player_)) {
        return;
    }
    goalClearPlayerAnimator_.Update(localTime);
    goalPlayerPosePosition_ = goalClearPlayerAnimator_.GetPosePosition();
}

void GamePlayScene::HandleGoalCinematicSignal(
    Object3d* target,
    const CinematicSignalMarker& signal,
    bool isPreview) {
    (void)target;
    (void)isPreview;
    if (signal.signal == "goal.ready" && goalPresentationState_ == GoalPresentationState::Celebrating) {
        goalPresentationState_ = GoalPresentationState::ReadyToReturn;
    }
}

void GamePlayScene::InitializeGoalPresentationOverlay() {
	if (!spriteCommon_) {
		return;
	}

	const uint32_t whiteHandle = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
	const uint32_t splashHandle = TextureManager::GetInstance()->Load("Resources/sprite/common/circle2.png");
	const uint32_t returnTextHandle = TextureManager::GetInstance()->Load("Resources/sprite/ui/result/clear/returning_select_text.png");

	goalOverlayFlash_ = std::make_unique<Sprite>();
	goalOverlayFlash_->Initialize(spriteCommon_.get(), whiteHandle);
	goalOverlayFlash_->SetName("GoalClear_Flash");
	goalOverlayFlash_->SetAnchorPoint({ 0.5f, 0.5f });

	static constexpr std::array<const char*, 10> kLetterTexturePaths = {
		"Resources/sprite/ui/result/clear/slime_letters/letter_s.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_t.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_a.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_g.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_e.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_c.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_l.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_e.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_a.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_r.png"
	};
	for (size_t i = 0; i < goalOverlayStageClearLetters_.size(); ++i) {
		auto& letter = goalOverlayStageClearLetters_[i];
		letter = std::make_unique<Sprite>();
		letter->Initialize(spriteCommon_.get(), TextureManager::GetInstance()->Load(kLetterTexturePaths[i]));
		letter->SetName("GoalClear_Letter_" + std::to_string(i));
		letter->SetAnchorPoint({ 0.5f, 0.5f });
	}

	goalOverlayReturnText_ = std::make_unique<Sprite>();
	goalOverlayReturnText_->Initialize(spriteCommon_.get(), returnTextHandle);
	goalOverlayReturnText_->SetName("GoalClear_ReturnText");
	goalOverlayReturnText_->SetAnchorPoint({ 0.5f, 0.5f });

	for (size_t i = 0; i < goalOverlaySparkles_.size(); ++i) {
		auto& sparkle = goalOverlaySparkles_[i];
		sparkle = std::make_unique<Sprite>();
		sparkle->Initialize(spriteCommon_.get(), splashHandle);
		sparkle->SetName("GoalClear_LetterSplash_" + std::to_string(i));
		sparkle->SetAnchorPoint({ 0.5f, 0.5f });
	}

	UpdateGoalPresentationOverlay();
}

void GamePlayScene::UpdateGoalPresentationOverlay() {
	const bool visible = isGoal_ && goalPresentationState_ != GoalPresentationState::Inactive;
	const float screenW = static_cast<float>(WinApp::kClientWidth);
	const float screenH = static_cast<float>(WinApp::kClientHeight);
	const float t = goalPresentationTimer_;
	const GoalClearPlayerAnimator::Tuning& animation = goalClearPlayerAnimator_.GetTuning();
	const GoalPresentationTuning& presentation = goalPresentationTuning_;
	const bool replayClear = goalWasStageCleared_;
	const Vector3 themePrimary = replayClear
		? Vector3{ 0.90f, 0.92f, 0.96f }
		: Vector3{ 1.0f, 0.72f, 0.12f };
	const Vector3 themeSecondary = replayClear
		? Vector3{ 0.58f, 0.64f, 0.74f }
		: Vector3{ 1.0f, 0.34f, 0.06f };
	const float baseScale = (std::min)(screenW / 1280.0f, screenH / 720.0f);
	const float uiScale = baseScale * presentation.resultUiScale;
	const float centerX = screenW * presentation.resultUiCenterX;
	const float baselineY = screenH * presentation.resultUiCenterY;
	const float returnStartTime = std::max(animation.resultUiTime + 0.52f, animation.readyTime - 0.48f);
	const float returnIn = GoalEaseInOut((t - returnStartTime) / 0.34f);
	const float flashIn = GoalEaseOut((t - animation.resultUiTime) / 0.04f);
	const float flashOut = 1.0f - GoalEaseOut((t - animation.resultUiTime - 0.04f) / 0.20f);
	const float flash = visible ? flashIn * flashOut : 0.0f;
	if (goalOverlayFlash_) {
		goalOverlayFlash_->SetPosition({ screenW * 0.5f, screenH * 0.5f });
		goalOverlayFlash_->SetSize({ screenW + 8.0f, screenH + 8.0f });
		goalOverlayFlash_->SetColor({ themePrimary.x, themePrimary.y, themePrimary.z, flash * 0.075f });
		goalOverlayFlash_->Update();
	}

	static constexpr std::array<float, 10> kAdvanceWidths = {
		62.0f, 60.0f, 66.0f, 68.0f, 56.0f, 68.0f, 54.0f, 56.0f, 66.0f, 64.0f
	};
	static constexpr std::array<float, 10> kSpriteWidths = {
		88.0f, 90.0f, 96.0f, 100.0f, 84.0f, 96.0f, 82.0f, 84.0f, 96.0f, 92.0f
	};
	static constexpr std::array<float, 10> kInitialTilts = {
		-0.10f, 0.075f, -0.055f, 0.095f, -0.070f, 0.060f, -0.085f, 0.050f, -0.060f, 0.080f
	};
	constexpr float kLetterGap = 6.0f;
	constexpr float kWordGap = 28.0f;
	float totalWidth = kWordGap + kLetterGap * 9.0f;
	for (float advance : kAdvanceWidths) {
		totalWidth += advance;
	}
	float cursorX = centerX - totalWidth * 0.5f * uiScale;

	for (size_t i = 0; i < goalOverlayStageClearLetters_.size(); ++i) {
		Sprite* letter = goalOverlayStageClearLetters_[i].get();
		Sprite* splash = goalOverlaySparkles_[i].get();
		const float letterCenterX = cursorX + kAdvanceWidths[i] * 0.5f * uiScale;
		const float localTime = t - animation.resultUiTime - static_cast<float>(i) * presentation.resultLetterStagger;
		float letterY = baselineY - presentation.resultLetterDropHeight * uiScale;
		float scaleX = 0.82f;
		float scaleY = 1.18f;
		float rotation = kInitialTilts[i];
		float letterAlpha = 0.0f;

		if (localTime >= 0.0f) {
			letterAlpha = GoalEaseOut(localTime / 0.10f);
			if (localTime < presentation.resultLetterFallDuration) {
				const float fallRate = GoalClamp01(localTime / presentation.resultLetterFallDuration);
				const float acceleratedFall = fallRate * fallRate;
				letterY = baselineY - presentation.resultLetterDropHeight * (1.0f - acceleratedFall) * uiScale;
				const float approach = GoalEaseInOut(fallRate);
				scaleX = 0.82f + 0.18f * approach;
				scaleY = 1.18f - 0.18f * approach;
				rotation = kInitialTilts[i] * (1.0f - approach);
			} else {
				const float bounceTime = localTime - presentation.resultLetterFallDuration;
				const float damping = std::exp(-bounceTime * presentation.resultLetterBounceDamping);
				const float bouncePhase = bounceTime * presentation.resultLetterBounceFrequency;
				const float rebound = std::abs(std::sin(bouncePhase)) * damping;
				const float contactWave = (std::max)(0.0f, std::cos(bouncePhase));
				const float contact = std::pow(contactWave, 7.0f) * damping;
				letterY = baselineY - presentation.resultLetterBounceHeight * rebound * uiScale;
				scaleX = 1.0f + contact * 0.34f - rebound * 0.075f;
				scaleY = 1.0f - contact * 0.26f + rebound * 0.11f;
				rotation = kInitialTilts[i] * std::cos(bouncePhase) * damping * 0.70f;

				const float idleBlend = GoalEaseInOut((bounceTime - 1.15f) / 0.45f);
				const float idleWobble = std::sin(bounceTime * 2.7f + static_cast<float>(i) * 0.62f);
				scaleX += idleWobble * 0.018f * idleBlend;
				scaleY -= idleWobble * 0.014f * idleBlend;
				letterY += std::sin(bounceTime * 2.2f + static_cast<float>(i) * 0.35f) * 1.4f * idleBlend * uiScale;
			}
		}

		if (letter) {
			letter->SetPosition({ letterCenterX, letterY });
			letter->SetSize({ kSpriteWidths[i] * scaleX * uiScale, 176.0f * scaleY * uiScale });
			letter->SetRotation(rotation);
			letter->SetColor({
				themePrimary.x,
				themePrimary.y,
				themePrimary.z,
				visible ? letterAlpha * presentation.resultBackdropAlpha : 0.0f
			});
			letter->Update();
		}

		if (splash) {
			const float impactAge = localTime - presentation.resultLetterFallDuration;
			const float splashRate = GoalClamp01(impactAge / 0.30f);
			const float splashAlpha = impactAge >= 0.0f
				? std::sin(splashRate * 3.1415926535f) * presentation.resultGlowAlpha
				: 0.0f;
			splash->SetPosition({ letterCenterX, baselineY + 45.0f * uiScale });
			splash->SetSize({
				(20.0f + 48.0f * splashRate) * uiScale,
				(13.0f - 9.0f * splashRate) * uiScale
			});
			splash->SetRotation(0.0f);
			splash->SetColor({
				themeSecondary.x,
				themeSecondary.y,
				themeSecondary.z,
				visible ? splashAlpha : 0.0f
			});
			splash->Update();
		}

		cursorX += (kAdvanceWidths[i] + kLetterGap) * uiScale;
		if (i == 4) {
			cursorX += kWordGap * uiScale;
		}
	}

	if (goalOverlayReturnText_) {
		goalOverlayReturnText_->SetPosition({ centerX, baselineY + (100.0f + 8.0f * (1.0f - returnIn)) * uiScale });
		goalOverlayReturnText_->SetSize({ 278.0f * uiScale, 33.0f * uiScale });
		goalOverlayReturnText_->SetRotation(0.0f);
		goalOverlayReturnText_->SetColor({ 0.92f, 0.98f, 1.0f, visible ? returnIn * 0.90f : 0.0f });
		goalOverlayReturnText_->Update();
	}
}

void GamePlayScene::DrawGoalPresentationOverlay() {
	if (!isGoal_ || goalPresentationState_ == GoalPresentationState::Inactive) {
		return;
	}

	if (goalOverlayFlash_) {
		goalOverlayFlash_->Draw();
	}
	for (auto& sparkle : goalOverlaySparkles_) {
		if (sparkle) {
			sparkle->Draw();
		}
	}
	for (auto& letter : goalOverlayStageClearLetters_) {
		if (letter) {
			letter->Draw();
		}
	}
	if (goalOverlayReturnText_) {
		goalOverlayReturnText_->Draw();
	}
}


void GamePlayScene::LoadGoalPresentationTuning() {
    std::ifstream file(kGoalPresentationTuningPath, std::ios::binary);
    if (!file.is_open()) {
        SanitizeGoalPresentationTuning();
        return;
    }

    try {
        nlohmann::json root;
        file >> root;
        GoalClearPlayerAnimator::Tuning animation = goalClearPlayerAnimator_.GetTuning();

        if (const auto it = root.find("timeline"); it != root.end() && it->is_object()) {
            const auto& j = *it;
            goalPresentationTuning_.crownFocusEndTime = j.value("crownFocusEndTime", goalPresentationTuning_.crownFocusEndTime);
            goalPresentationTuning_.crownMoveStartTime = j.value("crownMoveStartTime", goalPresentationTuning_.crownMoveStartTime);
            animation.crownLandTime = j.value("crownLandTime", animation.crownLandTime);
            animation.anticipationStartTime = j.value("anticipationStartTime", animation.anticipationStartTime);
            animation.jumpStartTime = j.value("jumpStartTime", animation.jumpStartTime);
            animation.apexTime = j.value("apexTime", animation.apexTime);
            animation.resultUiTime = j.value("resultUiTime", animation.resultUiTime);
            animation.victoryLandTime = j.value("victoryLandTime", animation.victoryLandTime);
            animation.readyTime = j.value("readyTime", animation.readyTime);
        }
        if (const auto it = root.find("player"); it != root.end() && it->is_object()) {
            const auto& j = *it;
            animation.jumpHeight = j.value("jumpHeight", animation.jumpHeight);
            animation.forwardDistance = j.value("forwardDistance", animation.forwardDistance);
            animation.anticipationDepth = j.value("anticipationDepth", animation.anticipationDepth);
            animation.landingSquash = j.value("landingSquash", animation.landingSquash);
            animation.anticipationSquash = j.value("anticipationSquash", animation.anticipationSquash);
            animation.takeoffStretch = j.value("takeoffStretch", animation.takeoffStretch);
            animation.resultStretch = j.value("resultStretch", animation.resultStretch);
            animation.resultYawBias = j.value("resultYawBias", animation.resultYawBias);
        }
        if (const auto it = root.find("crown"); it != root.end() && it->is_object()) {
            const auto& j = *it;
            goalPresentationTuning_.crownDropHeight = j.value("dropHeight", goalPresentationTuning_.crownDropHeight);
            goalPresentationTuning_.crownSeatDepth = j.value("seatDepth", goalPresentationTuning_.crownSeatDepth);
        }
        if (const auto it = root.find("camera"); it != root.end() && it->is_object()) {
            const auto& j = *it;
            goalPresentationTuning_.crownFocusDistance = j.value("crownFocusDistance", goalPresentationTuning_.crownFocusDistance);
            goalPresentationTuning_.crownFocusSide = j.value("crownFocusSide", goalPresentationTuning_.crownFocusSide);
            goalPresentationTuning_.crownFocusHeight = j.value("crownFocusHeight", goalPresentationTuning_.crownFocusHeight);
            goalPresentationTuning_.landingCameraDistance = j.value("landingDistance", goalPresentationTuning_.landingCameraDistance);
            goalPresentationTuning_.landingCameraSide = j.value("landingSide", goalPresentationTuning_.landingCameraSide);
            goalPresentationTuning_.landingCameraHeight = j.value("landingHeight", goalPresentationTuning_.landingCameraHeight);
            goalPresentationTuning_.jumpCameraDistance = j.value("jumpDistance", goalPresentationTuning_.jumpCameraDistance);
            goalPresentationTuning_.jumpCameraSide = j.value("jumpSide", goalPresentationTuning_.jumpCameraSide);
            goalPresentationTuning_.jumpCameraHeight = j.value("jumpHeight", goalPresentationTuning_.jumpCameraHeight);
            goalPresentationTuning_.resultCameraDistance = j.value("resultDistance", goalPresentationTuning_.resultCameraDistance);
            goalPresentationTuning_.resultCameraSide = j.value("resultSide", goalPresentationTuning_.resultCameraSide);
            goalPresentationTuning_.resultCameraHeight = j.value("resultHeight", goalPresentationTuning_.resultCameraHeight);
            goalPresentationTuning_.resultTargetSide = j.value("resultTargetSide", goalPresentationTuning_.resultTargetSide);
            goalPresentationTuning_.crownFocusFov = j.value("crownFocusFov", goalPresentationTuning_.crownFocusFov);
            goalPresentationTuning_.landingFov = j.value("landingFov", goalPresentationTuning_.landingFov);
            goalPresentationTuning_.jumpFov = j.value("jumpFov", goalPresentationTuning_.jumpFov);
            goalPresentationTuning_.resultFov = j.value("resultFov", goalPresentationTuning_.resultFov);
        }
        if (const auto it = root.find("ui"); it != root.end() && it->is_object()) {
            const auto& j = *it;
            goalPresentationTuning_.resultUiCenterX = j.value("centerX", goalPresentationTuning_.resultUiCenterX);
            goalPresentationTuning_.resultUiCenterY = j.value("centerY", goalPresentationTuning_.resultUiCenterY);
            goalPresentationTuning_.resultUiScale = j.value("scale", goalPresentationTuning_.resultUiScale);
            goalPresentationTuning_.resultBackdropAlpha = j.value("backdropAlpha", goalPresentationTuning_.resultBackdropAlpha);
            goalPresentationTuning_.resultGlowAlpha = j.value("glowAlpha", goalPresentationTuning_.resultGlowAlpha);
            goalPresentationTuning_.resultLetterStagger = j.value("letterStagger", goalPresentationTuning_.resultLetterStagger);
            goalPresentationTuning_.resultLetterFallDuration = j.value("letterFallDuration", goalPresentationTuning_.resultLetterFallDuration);
            goalPresentationTuning_.resultLetterDropHeight = j.value("letterDropHeight", goalPresentationTuning_.resultLetterDropHeight);
            goalPresentationTuning_.resultLetterBounceHeight = j.value("letterBounceHeight", goalPresentationTuning_.resultLetterBounceHeight);
            goalPresentationTuning_.resultLetterBounceDamping = j.value("letterBounceDamping", goalPresentationTuning_.resultLetterBounceDamping);
            goalPresentationTuning_.resultLetterBounceFrequency = j.value("letterBounceFrequency", goalPresentationTuning_.resultLetterBounceFrequency);
        }

        goalClearPlayerAnimator_.SetTuning(animation);
        SanitizeGoalPresentationTuning();
    } catch (const std::exception& e) {
        DebugConsole::GetInstance()->AddLog(std::string("Goal presentation settings load failed: ") + e.what());
        SanitizeGoalPresentationTuning();
    }
}

void GamePlayScene::SaveGoalPresentationTuning() const {
    const GoalClearPlayerAnimator::Tuning& a = goalClearPlayerAnimator_.GetTuning();
    const GoalPresentationTuning& g = goalPresentationTuning_;
    nlohmann::json root;
    root["version"] = 1;
    root["timeline"] = {
        {"crownFocusEndTime", g.crownFocusEndTime}, {"crownMoveStartTime", g.crownMoveStartTime},
        {"crownLandTime", a.crownLandTime},
        {"anticipationStartTime", a.anticipationStartTime}, {"jumpStartTime", a.jumpStartTime},
        {"apexTime", a.apexTime}, {"resultUiTime", a.resultUiTime},
        {"victoryLandTime", a.victoryLandTime}, {"readyTime", a.readyTime}
    };
    root["player"] = {
        {"jumpHeight", a.jumpHeight}, {"forwardDistance", a.forwardDistance},
        {"anticipationDepth", a.anticipationDepth}, {"landingSquash", a.landingSquash},
        {"anticipationSquash", a.anticipationSquash}, {"takeoffStretch", a.takeoffStretch},
        {"resultStretch", a.resultStretch}, {"resultYawBias", a.resultYawBias}
    };
    root["crown"] = {
        {"dropHeight", g.crownDropHeight}, {"seatDepth", g.crownSeatDepth}
    };
    root["camera"] = {
        {"crownFocusDistance", g.crownFocusDistance}, {"crownFocusSide", g.crownFocusSide},
        {"crownFocusHeight", g.crownFocusHeight}, {"landingDistance", g.landingCameraDistance},
        {"landingSide", g.landingCameraSide}, {"landingHeight", g.landingCameraHeight},
        {"jumpDistance", g.jumpCameraDistance}, {"jumpSide", g.jumpCameraSide},
        {"jumpHeight", g.jumpCameraHeight}, {"resultDistance", g.resultCameraDistance},
        {"resultSide", g.resultCameraSide}, {"resultHeight", g.resultCameraHeight},
        {"resultTargetSide", g.resultTargetSide}, {"crownFocusFov", g.crownFocusFov},
        {"landingFov", g.landingFov}, {"jumpFov", g.jumpFov}, {"resultFov", g.resultFov}
    };
    root["ui"] = {
        {"centerX", g.resultUiCenterX}, {"centerY", g.resultUiCenterY},
        {"scale", g.resultUiScale}, {"backdropAlpha", g.resultBackdropAlpha},
        {"glowAlpha", g.resultGlowAlpha},
        {"letterStagger", g.resultLetterStagger},
        {"letterFallDuration", g.resultLetterFallDuration},
        {"letterDropHeight", g.resultLetterDropHeight},
        {"letterBounceHeight", g.resultLetterBounceHeight},
        {"letterBounceDamping", g.resultLetterBounceDamping},
        {"letterBounceFrequency", g.resultLetterBounceFrequency}
    };

    std::ofstream file(kGoalPresentationTuningPath, std::ios::binary);
    if (file.is_open()) {
        file << root.dump(4);
    }
}

void GamePlayScene::SanitizeGoalPresentationTuning() {
    GoalClearPlayerAnimator::Tuning a = goalClearPlayerAnimator_.GetTuning();
    GoalPresentationTuning& g = goalPresentationTuning_;
    g.crownFocusEndTime = std::clamp(g.crownFocusEndTime, 0.20f, 1.50f);
    g.crownMoveStartTime = std::max(g.crownMoveStartTime, g.crownFocusEndTime + 0.12f);
    a.crownLandTime = std::max(a.crownLandTime, g.crownMoveStartTime + 0.70f);
    a.anticipationStartTime = std::max(a.anticipationStartTime, a.crownLandTime + 0.06f);
    a.jumpStartTime = std::max(a.jumpStartTime, a.anticipationStartTime + 0.10f);
    a.apexTime = std::max(a.apexTime, a.jumpStartTime + 0.36f);
    a.resultUiTime = std::max(a.resultUiTime, a.apexTime + 0.02f);
    a.victoryLandTime = std::max(a.victoryLandTime, a.apexTime + 0.36f);
    a.readyTime = std::max(a.readyTime, a.victoryLandTime + 0.55f);
    a.jumpHeight = std::clamp(a.jumpHeight, 1.0f, 7.0f);
    a.forwardDistance = std::clamp(a.forwardDistance, 0.0f, 3.0f);
    a.anticipationDepth = std::clamp(a.anticipationDepth, 0.02f, 0.65f);
    a.landingSquash = std::clamp(a.landingSquash, 0.0f, 0.40f);
    a.anticipationSquash = std::clamp(a.anticipationSquash, 0.0f, 0.50f);
    a.takeoffStretch = std::clamp(a.takeoffStretch, 0.0f, 0.65f);
    a.resultStretch = std::clamp(a.resultStretch, 0.0f, 0.35f);
    g.crownDropHeight = std::clamp(g.crownDropHeight, 0.30f, 4.0f);
    g.crownSeatDepth = std::clamp(g.crownSeatDepth, 0.0f, 0.45f);
    g.crownFocusFov = std::clamp(g.crownFocusFov, 0.30f, 1.10f);
    g.landingFov = std::clamp(g.landingFov, 0.30f, 1.10f);
    g.jumpFov = std::clamp(g.jumpFov, 0.30f, 1.10f);
    g.resultFov = std::clamp(g.resultFov, 0.30f, 1.10f);
    g.resultUiCenterX = std::clamp(g.resultUiCenterX, 0.30f, 0.70f);
    g.resultUiCenterY = std::clamp(g.resultUiCenterY, 0.12f, 0.82f);
    g.resultUiScale = std::clamp(g.resultUiScale, 0.60f, 1.35f);
    g.resultBackdropAlpha = std::clamp(g.resultBackdropAlpha, 0.0f, 1.0f);
    g.resultGlowAlpha = std::clamp(g.resultGlowAlpha, 0.0f, 1.0f);
    g.resultLetterStagger = std::clamp(g.resultLetterStagger, 0.02f, 0.20f);
    g.resultLetterFallDuration = std::clamp(g.resultLetterFallDuration, 0.18f, 0.75f);
    g.resultLetterDropHeight = std::clamp(g.resultLetterDropHeight, 80.0f, 520.0f);
    g.resultLetterBounceHeight = std::clamp(g.resultLetterBounceHeight, 8.0f, 120.0f);
    g.resultLetterBounceDamping = std::clamp(g.resultLetterBounceDamping, 1.0f, 8.0f);
    g.resultLetterBounceFrequency = std::clamp(g.resultLetterBounceFrequency, 4.0f, 18.0f);
    goalClearPlayerAnimator_.SetTuning(a);
}

void GamePlayScene::DrawGoalPresentationEditor() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader(ICON_FA_FILM " ゴール演出シーケンス", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    GoalClearPlayerAnimator::Tuning& a = goalClearPlayerAnimator_.EditTuning();
    GoalPresentationTuning& g = goalPresentationTuning_;
    bool changed = false;
    ImGui::TextUnformatted("タイムライン");
    changed |= ImGui::DragFloat("王冠フォーカス完了", &g.crownFocusEndTime, 0.01f, 0.20f, 1.50f, "%.2f 秒");
    changed |= ImGui::DragFloat("王冠移動開始", &g.crownMoveStartTime, 0.01f, 0.30f, 2.00f, "%.2f 秒");
    changed |= ImGui::DragFloat("王冠着地", &a.crownLandTime, 0.01f, 1.00f, 4.00f, "%.2f 秒");
    changed |= ImGui::DragFloat("溜め開始", &a.anticipationStartTime, 0.01f, 1.10f, 4.50f, "%.2f 秒");
    changed |= ImGui::DragFloat("大ジャンプ開始", &a.jumpStartTime, 0.01f, 1.20f, 5.00f, "%.2f 秒");
    changed |= ImGui::DragFloat("ジャンプ頂点", &a.apexTime, 0.01f, 1.60f, 6.50f, "%.2f 秒");
    changed |= ImGui::DragFloat("リザルト表示", &a.resultUiTime, 0.01f, 1.70f, 7.00f, "%.2f 秒");
    changed |= ImGui::DragFloat("勝利着地", &a.victoryLandTime, 0.01f, 2.00f, 7.50f, "%.2f 秒");
    changed |= ImGui::DragFloat("帰還案内開始", &a.readyTime, 0.01f, 2.20f, 8.00f, "%.2f 秒");

    ImGui::Separator();
    ImGui::TextUnformatted("プレイヤー");
    changed |= ImGui::DragFloat("ジャンプ高さ", &a.jumpHeight, 0.02f, 1.0f, 7.0f, "%.2f");
    changed |= ImGui::DragFloat("前進距離", &a.forwardDistance, 0.01f, 0.0f, 3.0f, "%.2f");
    changed |= ImGui::DragFloat("溜め沈み", &a.anticipationDepth, 0.005f, 0.02f, 0.65f, "%.3f");
    changed |= ImGui::DragFloat("王冠着地の潰れ", &a.landingSquash, 0.005f, 0.0f, 0.40f, "%.3f");
    changed |= ImGui::DragFloat("ジャンプ前の潰れ", &a.anticipationSquash, 0.005f, 0.0f, 0.50f, "%.3f");
    changed |= ImGui::DragFloat("離陸の伸び", &a.takeoffStretch, 0.005f, 0.0f, 0.65f, "%.3f");
    changed |= ImGui::DragFloat("決め姿勢の伸び", &a.resultStretch, 0.005f, 0.0f, 0.35f, "%.3f");

    ImGui::Separator();
    ImGui::TextUnformatted("王冠");
    changed |= ImGui::DragFloat("降下開始高さ", &g.crownDropHeight, 0.01f, 0.30f, 4.0f, "%.2f");
    changed |= ImGui::DragFloat("頭への沈み", &g.crownSeatDepth, 0.005f, 0.0f, 0.45f, "%.3f");

    ImGui::Separator();
    ImGui::TextUnformatted("カメラ");
    changed |= ImGui::DragFloat3("王冠ショット 距離/横/高さ", &g.crownFocusDistance, 0.01f, -10.0f, 10.0f, "%.2f");
    changed |= ImGui::DragFloat3("着地ショット 距離/横/高さ", &g.landingCameraDistance, 0.01f, -12.0f, 12.0f, "%.2f");
    changed |= ImGui::DragFloat3("ジャンプショット 距離/横/高さ", &g.jumpCameraDistance, 0.01f, -14.0f, 14.0f, "%.2f");
    changed |= ImGui::DragFloat3("リザルト 距離/横/高さ", &g.resultCameraDistance, 0.01f, -16.0f, 16.0f, "%.2f");
    changed |= ImGui::DragFloat("リザルト注視点の横ずらし", &g.resultTargetSide, 0.01f, -4.0f, 4.0f, "%.2f");
    changed |= ImGui::DragFloat4("FOV 王冠/着地/ジャンプ/結果", &g.crownFocusFov, 0.005f, 0.30f, 1.10f, "%.3f");

    ImGui::Separator();
    ImGui::TextUnformatted("クリアUI");
    changed |= ImGui::DragFloat("表示位置 X", &g.resultUiCenterX, 0.005f, 0.30f, 0.70f, "%.3f");
    changed |= ImGui::DragFloat("表示位置 Y", &g.resultUiCenterY, 0.005f, 0.12f, 0.82f, "%.3f");
    changed |= ImGui::DragFloat("UIスケール", &g.resultUiScale, 0.005f, 0.60f, 1.35f, "%.3f");
    changed |= ImGui::DragFloat("文字不透明度", &g.resultBackdropAlpha, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::DragFloat("着地しぶき", &g.resultGlowAlpha, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::DragFloat("文字間ディレイ", &g.resultLetterStagger, 0.001f, 0.02f, 0.20f, "%.3f 秒");
    changed |= ImGui::DragFloat("落下時間", &g.resultLetterFallDuration, 0.005f, 0.18f, 0.75f, "%.3f 秒");
    changed |= ImGui::DragFloat("落下距離", &g.resultLetterDropHeight, 1.0f, 80.0f, 520.0f, "%.0f px");
    changed |= ImGui::DragFloat("跳ね返り高さ", &g.resultLetterBounceHeight, 0.5f, 8.0f, 120.0f, "%.1f px");
    changed |= ImGui::DragFloat("跳ね減衰", &g.resultLetterBounceDamping, 0.05f, 1.0f, 8.0f, "%.2f");
    changed |= ImGui::DragFloat("跳ね速度", &g.resultLetterBounceFrequency, 0.05f, 4.0f, 18.0f, "%.2f");

    if (changed) {
        SanitizeGoalPresentationTuning();
        SyncGoalCinematicTimelineFromTuning();
    }

    if (ImGui::Button(ICON_FA_PLAY " 実ステージでプレビュー")) {
        StartGoalPresentationPreview();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STOP " プレビュー停止")) {
        StopGoalPresentationPreview();
    }
    if (ImGui::Button(ICON_FA_SAVE " JSON保存")) {
        SanitizeGoalPresentationTuning();
        SyncGoalCinematicTimelineFromTuning();
        SaveGoalPresentationTuning();
        goalCinematicSequence_.Save(kGoalCinematicTimelinePath);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " JSON再読込")) {
        LoadGoalPresentationTuning();
        SyncGoalCinematicTimelineFromTuning();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(isGoal_);
    if (ImGui::Button(ICON_FA_FILM " Timeline再読込")) {
        goalCinematicTimelineLoaded_ = goalCinematicSequence_.Load(kGoalCinematicTimelinePath);
        if (goalCinematicTimelineLoaded_) {
            ApplyGoalCinematicTimingFromSequence();
            goalCinematicPlayer_.SetSequence(&goalCinematicSequence_);
        }
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("保存先: %s", kGoalPresentationTuningPath);
    ImGui::TextDisabled("Timeline: %s", kGoalCinematicTimelinePath);
    ImGui::TextColored(
        goalCinematicTimelineLoaded_ ? ImVec4(0.45f, 0.92f, 0.55f, 1.0f) : ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
        goalCinematicTimelineLoaded_ ? "Timeline Runtime: Ready" : "Timeline Runtime: Load Failed");
#endif
}

Object3d* GamePlayScene::FindGoalCrownObject() const {
    if (!objectManager_) {
        return nullptr;
    }
    for (const auto& object : objectManager_->GetObjects()) {
        if (object && object->GetEventType() == EventType::Goal) {
            return object.get();
        }
    }
    return nullptr;
}

void GamePlayScene::StartGoalPresentationPreview() {
    if (isGoal_) {
        if (!goalEditorPreviewMode_) {
            DebugConsole::GetInstance()->AddLog("Goal presentation is already running in gameplay.");
            return;
        }
        StopGoalPresentationPreview();
    }
    Object3d* crown = FindGoalCrownObject();
    if (!crown || !player_) {
        DebugConsole::GetInstance()->AddLog("Goal presentation preview requires player and crown.");
        return;
    }
    goalEditorPreviewMode_ = true;
    StartGoalPresentation(crown);
    goalSavePerformed_ = true;
}

void GamePlayScene::StopGoalPresentationPreview() {
    if (!goalEditorPreviewMode_) {
        return;
    }
    Camera* restoreCamera = goalLockedPrimaryCamera_ ? goalLockedPrimaryCamera_ : CameraManager::GetInstance()->GetMainCamera();
    SetIsGoal(false);
    if (restoreCamera) {
        CameraManager::GetInstance()->SetActiveCamera(restoreCamera);
    }
    goalEditorPreviewMode_ = false;
}

