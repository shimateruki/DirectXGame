#pragma once
#include "engine/utility/math/Math.h"
#include <cstdint>
#include <string>
class Object3d;

// ステージセレクト内部の共通計算。状態やObjectの所有権は保持しない。
// 定数値と呼び出し側の時間換算を維持する。
namespace StageSelectDetail {
constexpr float kUnlockPresentationDuration = 5.90f;
constexpr float kUnlockCameraIntroDuration = 1.05f;
constexpr float kUnlockKeyAppearStartTime = 1.18f;
constexpr float kUnlockKeyAppearDuration = 0.42f;
constexpr float kUnlockKeyApproachStartTime = 1.28f;
constexpr float kUnlockKeyApproachEndTime = 2.72f;
constexpr float kUnlockKeyTurnStartTime = 2.82f;
constexpr float kUnlockKeyTurnEndTime = 3.42f;
constexpr float kUnlockLockReleaseTime = 3.48f;
constexpr float kUnlockLockHideTime = 4.18f;
constexpr float kUnlockGateActivationStartTime = 3.78f;
constexpr float kUnlockGateActivationEndTime = 4.52f;
constexpr float kUnlockCameraRestoreStartTime = 4.72f;
constexpr float kUnlockCameraRestoreDuration = 1.08f;
constexpr uint32_t kStageSelectSolidMask = 0xFFFFFFFFu;
constexpr int kStageIslandCollisionStripCount = 15;
constexpr const char* kStageClearCrownPrefix = "__Editor_StageClearCrown_";
constexpr const char* kStageClearCrownModel = "Stages/crown";
constexpr float kStageClearCrownBaseScale = 0.42f;
constexpr float kStageClearCrownHeightOffset = 0.85f;
constexpr const char* kCrownIdleParticlePreset = "crown_idle_sparkle";
constexpr const char* kCrownUnlockParticlePreset = "crown_get_burst";
constexpr const char* kCrownUnlockRayParticlePreset = "crown_get_rays";
constexpr const char* kCrownUnlockFountainParticlePreset = "crown_get_twinkle_fountain";
constexpr const char* kCrownUnlockAfterglowParticlePreset = "crown_get_afterglow";
constexpr const char* kGateUnlockCoreParticlePreset = "gate_entry_core_mist";
constexpr const char* kGateUnlockSparkParticlePreset = "gate_entry_pull_sparks";
constexpr const char* kStageGateLockPrefix = "StageSelect_LockSeal_";
constexpr const char* kStageGateCrownKeyPrefix = "StageSelect_CrownKey_";
constexpr const char* kCrownAuraEffectPath = "Resources/json/effect/effect_crown_aura_ring.json";
constexpr const char* kCrownRayEffectPath = "Resources/json/effect/effect_crown_ray_plane.json";
constexpr const char* kCrownUnlockFlashRingEffectPath = "Resources/json/effect/effect_crown_get_flash_ring.json";
constexpr const char* kCrownUnlockRayFlashEffectPath = "Resources/json/effect/effect_crown_get_ray_flash.json";
constexpr float kCrownCountPresentationDuration = 2.65f;
constexpr float kCrownCountImpactTime = 1.05f;
constexpr float kCrownCountParticleInterval = 0.08f;

// ゲート突入演出は、CameraEditorの保存位置ではなくゲートのTransformから自動生成する。
// ゲートを移動してもカメラ演出を再調整しなくて済むように、距離・高さ・補間時間だけをここで管理する。
constexpr float kGateEntryCinematicDuration = 1.55f;
constexpr float kGateEntryMinPlaneThickness = 0.16f;
constexpr float kGateEntryMaxPlaneThickness = 0.42f;
constexpr float kGateEntryMinHalfWidth = 0.85f;
constexpr float kGateEntryMaxHalfWidth = 2.45f;
constexpr float kGateEntryPlayerTouchRadius = 0.24f;
constexpr float kGateEntrySurfaceOffset = 1.18f;
constexpr float kGateEntryInsideDepth = 1.38f;
constexpr float kGateEntryLiftHeight = 0.18f;
constexpr float kGateEntryCenteringDelay = 0.06f;
constexpr float kGateEntryCenteringDuration = 0.36f;
constexpr float kGateEntryCameraBackDistance = 14.25f;
constexpr float kGateEntryCameraSideOffset = 0.0f;
constexpr float kGateEntryCameraHeight = 0.58f;
constexpr float kGateEntryCameraFocusHeight = 0.92f;
constexpr float kGateEntryCameraBlendTime = 0.34f;
constexpr float kGateEntryAlignEndTime = 0.28f;
constexpr float kGateEntryDiveStartTime = 0.18f;
constexpr float kGateEntryDiveEndTime = 0.88f;
constexpr float kGateEntryDissolveStartTime = 0.10f;
constexpr float kGateEntryDissolveEndTime = 0.74f;
constexpr float kGateReturnEmergingDuration = 1.68f;
constexpr float kGateReturnCameraIntroDuration = 0.24f;
constexpr float kGateReturnCameraRestoreStartTime = 1.62f;
constexpr float kGateReturnCameraRestoreDuration = 1.05f;
constexpr float kGateReturnTotalDuration = kGateReturnCameraRestoreStartTime + kGateReturnCameraRestoreDuration;
constexpr float kGateReturnExitForwardOffset = 5.60f;
constexpr float kGateReturnGroundOffsetFromGateOrigin = 1.50f;
constexpr float kGateReturnGroundProbeHeight = 24.0f;
constexpr float kGateReturnGroundProbeDistance = 80.0f;
constexpr float kGateReturnHopHeight = 2.10f;
constexpr float kGateReturnCameraBackDistance = 20.0f;
constexpr float kGateReturnCameraSideOffset = 4.20f;
constexpr float kGateReturnCameraHeight = 3.00f;
constexpr float kGateReturnCameraFocusHeight = 1.15f;
constexpr float kGateReturnCameraLandingFocusRate = 0.72f;
constexpr float kGateReturnThirdPersonDistance = 16.0f;
constexpr float kGateReturnThirdPersonHeight = 3.20f;
constexpr float kGateReturnThirdPersonTargetHeight = 1.45f;
constexpr float kGateReturnThirdPersonPitch = 0.32f;
constexpr float kGateCinematicBarHeight = 0.105f;
constexpr float kGateCinematicBarOpenSpeed = 5.8f;
constexpr float kGateCinematicBarCloseSpeed = 4.2f;


struct SelectGateEntryRoute {
	Vector3 surface;
	Vector3 center;
	Vector3 inside;
};

float SelectClamp01(float value);
float SelectEaseOutCubic(float value);
Vector3 SelectLookAtRotation(const Vector3& eye, const Vector3& target);
Vector3 SelectNormalizeGateDirectionXZ(const Vector3& value, const Vector3& fallback);
float SelectResolveGateReturnGroundY(const Vector3& probePosition, const Vector3& gatePos, const Vector3& stageNodePosition, const Vector3& playerPosition, const Object3d* ignoredObject);
Vector3 SelectGateEntryDirection(Object3d* gateObject, const Vector3& fallbackDirection);
SelectGateEntryRoute SelectBuildGateEntryRoute(const Vector3& gatePos, const Vector3& direction, float entryY);
float SelectEaseInOutCubic(float value);
Vector3 SelectLerpVector3(const Vector3& a, const Vector3& b, float t);
Vector3 SelectBezierVector3(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t);
Vector3 SelectNormalizeXZ(const Vector3& value, const Vector3& fallback);
float SelectEaseOutBack(float value);
Vector3 SelectLerp(const Vector3& a, const Vector3& b, float t);
Vector4 GetStageIslandColor(int stageIndex, bool unlocked, bool selected, bool unlocking, float pulse);
float GetStageIslandEmissive(bool unlocked, bool selected, bool unlocking, float pulse);
Vector4 GetStageGateModelColor(bool unlocked, bool selected, bool cleared, bool unlocking, float pulse);
float GetStageGateModelEmissive(bool unlocked, bool selected, bool cleared, bool unlocking, float pulse);
std::string MakeStageClearCrownName(int stageIndex);
}
