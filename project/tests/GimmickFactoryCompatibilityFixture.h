#pragma once
// 移行前の生成設定を固定した比較用Fixtureです。ゲームからは呼び出しません。
#ifdef USE_IMGUI

#include "GimmickMovingFloor.h"
#include "GimmickHazardRideFloor.h"
#include "GimmickTrampoline.h"
#include "GimmickChikuwaBlock.h"
#include "GimmickBlinkBlock.h"
#include "GimmickBreakableBlock.h"
#include "GimmickCoin.h"
#include "GimmickSinkingFloor.h"
#include "GimmickSeesawFloor.h"
#include "GimmickDashPanel.h"
#include "GimmickTimedSwitch.h"
#include "GimmickAppearingFloor.h"
#include "GimmickSwitch.h"
#include "GimmickEventReceiver.h"
#include "GimmickArenaEncounter.h"
#include "GimmickGameplayVolume.h"
#include "GimmickCopyMemoryStation.h"
#include "GimmickPrismBarrier.h"
#include "GimmickBossGate.h"
#include "GimmickHookPullBlock.h"
#include "GimmickLiquidLevel.h"
#include "GimmickChainCollapseFloor.h"
#include "GimmickRotatingObject.h"
#include "GimmickPhaseFlipFloor.h"
#include "GimmickStageGate.h"
#include "GimmickFireCannon.h"
#include "GimmickMagmaHazard.h"
#include "GimmickMagmaGeyser.h"
#include <typeinfo>


namespace LegacyGimmickCreation {


template <typename T>
std::unique_ptr<BaseGimmick> CreateSimpleGimmick(engine::Object3dCommon* common, const char* modelPath) {
    auto gimmick = std::make_unique<T>();
    gimmick->Initialize(common, modelPath);
    return gimmick;
}

using Creator = std::unique_ptr<BaseGimmick> (*)(engine::Object3dCommon*, const char*);

struct SimpleGimmickEntry {
    const char* type;
    const char* modelPath;
    Creator create;
};

// 追加設定が不要な種類は、型名・モデル・生成関数を一か所で対応付ける。
constexpr SimpleGimmickEntry kSimpleGimmicks[] = {
    {"HazardRideFloor", "Stages/star_garden_ride_platform", &CreateSimpleGimmick<GimmickHazardRideFloor>},
    {"Trampoline", "Gimmicks/mushroom_trampoline", &CreateSimpleGimmick<GimmickTrampoline>},
    {"ChikuwaBlock", "Stages/block", &CreateSimpleGimmick<GimmickChikuwaBlock>},
    {"BlinkBlock", "Stages/block", &CreateSimpleGimmick<GimmickBlinkBlock>},
    {"BreakableBlock", "Stages/bomb_break_block", &CreateSimpleGimmick<GimmickBreakableBlock>},
    {"Coin", "Primitives/sphere", &CreateSimpleGimmick<GimmickCoin>},
    {"SeesawFloor", "Stages/block", &CreateSimpleGimmick<GimmickSeesawFloor>},
    {"DashPanel", "Stages/block", &CreateSimpleGimmick<GimmickDashPanel>},
    {"TimedSwitch", "Stages/block", &CreateSimpleGimmick<GimmickTimedSwitch>},
    {"AppearingFloor", "Stages/block", &CreateSimpleGimmick<GimmickAppearingFloor>},
    {"Switch", "Stages/block", &CreateSimpleGimmick<GimmickSwitch>},
    {"EventReceiver", "Stages/block", &CreateSimpleGimmick<GimmickEventReceiver>},
    {"ArenaEncounter", "Primitives/cube", &CreateSimpleGimmick<GimmickArenaEncounter>},
    {"GameplayVolume", "Primitives/cube", &CreateSimpleGimmick<GimmickGameplayVolume>},
    {"CopyMemoryStation", "Gimmicks/copy_memory_station", &CreateSimpleGimmick<GimmickCopyMemoryStation>},
    {"PrismBarrier", "Gimmicks/portal_surface", &CreateSimpleGimmick<GimmickPrismBarrier>},
    {"BossGate", "Stages/high_crown/high_crown_gate.obj", &CreateSimpleGimmick<GimmickBossGate>},
    {"HookPullBlock", "Stages/block", &CreateSimpleGimmick<GimmickHookPullBlock>},
    {"LiquidLevel", "Stages/block", &CreateSimpleGimmick<GimmickLiquidLevel>},
    {"ChainCollapseFloor", "Stages/block", &CreateSimpleGimmick<GimmickChainCollapseFloor>},
    {"PhaseFlipFloor", "Stages/block", &CreateSimpleGimmick<GimmickPhaseFlipFloor>},
    {"FireCannon", "Primitives/cube", &CreateSimpleGimmick<GimmickFireCannon>},
    {"MagmaHazard", "Stages/block", &CreateSimpleGimmick<GimmickMagmaHazard>},
    {"MagmaGeyser", "Stages/magma_vent", &CreateSimpleGimmick<GimmickMagmaGeyser>},
    {"StageGate", "Gimmicks/portal_surface", &CreateSimpleGimmick<GimmickStageGate>},
};

inline std::unique_ptr<BaseGimmick> CreateConfiguredGimmick(const std::string& type, engine::Object3dCommon* common) {
    if (type == "MovingFloor") {
        auto floor = std::make_unique<GimmickMovingFloor>();
        floor->Initialize(common, "Stages/block");
        if (!floor->param_.has_value()) {
            floor->param_.emplace();
        }
        floor->param_->speed = 0.05f;
        return floor;
    }

    if (type == "RotatingFloor" || type == "RotatingPillar") {
        const bool isPillar = type == "RotatingPillar";
        auto rotating = std::make_unique<GimmickRotatingObject>();
        rotating->Initialize(common, "Stages/block");
        rotating->SetName(isPillar ? "Gimmick_RotatingPillar" : "Gimmick_RotatingFloor");
        rotating->SetScale(isPillar ? engine::Vector3{0.75f, 3.0f, 0.75f}
                                    : engine::Vector3{3.0f, 0.3f, 1.2f});
        if (!rotating->param_.has_value()) {
            rotating->param_.emplace();
        }
        rotating->param_->actionMode = 1;
        rotating->param_->speed = isPillar ? 60.0f : 45.0f;
        rotating->param_->startActive = true;
        return rotating;
    }

    if (type == "WaterGeyser") {
        // 噴出口の描画は共用し、水版だけ起動条件を変更して攻撃を無効にする。
        auto geyser = std::make_unique<GimmickMagmaGeyser>();
        geyser->Initialize(common, "Stages/magma_pressure_column");
        geyser->param_->startActive = false;
        geyser->param_->returnOnOff = true;
        geyser->param_->actionMode = 3;
        geyser->param_->detectionRange = 4.0f;
        return geyser;
    }

    return nullptr;
}


inline std::unique_ptr<BaseGimmick> Create(const std::string& type, engine::Object3dCommon* common) {
    auto object = CreateConfiguredGimmick(type, common);
    if (type == "SinkingFloor") object = CreateSimpleGimmick<GimmickSinkingFloor>(common, "Stages/block");
    for (const auto& entry : kSimpleGimmicks) if (!object && type == entry.type) object = entry.create(common, entry.modelPath);
    if (!object) object = CreateSimpleGimmick<BaseGimmick>(common, "Primitives/cube");
    object->SetGimmickType(type);
    return object;
}
} // namespace LegacyGimmickCreation

#endif
