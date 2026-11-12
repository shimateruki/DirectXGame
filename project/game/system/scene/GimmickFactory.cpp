#include "game/system/scene/GimmickFactory.h"
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

GimmickFactory::GimmickFactory() {
    // 保存ID・日本語名・生成クラスを一度だけ対応付け、Editorもこの一覧を使用します。
    registered_.push_back({{"Default", "通常", [](const engine::Object3D& object) {
        return typeid(object) == typeid(BaseGimmick);
    }}, [](engine::Object3DCommon* common) {
        auto object = std::make_unique<BaseGimmick>();
        object->Initialize(common, "Primitives/cube");
        return object;
    }});
    RegisterGimmick<GimmickMovingFloor>("移動床", GimmickMovingFloor::kTypeId, GimmickMovingFloor::kModelPath,
        [](GimmickMovingFloor& object) { object.SetParameter(&engine::EntityParameter::speed, 0.05f); });
    RegisterGimmick<GimmickHazardRideFloor>("妨害付き輸送床");
    RegisterGimmick<GimmickTrampoline>("トランポリン");
    RegisterGimmick<GimmickChikuwaBlock>("ちくわブロック");
    RegisterGimmick<GimmickBlinkBlock>("点滅・ジャンプ反転ブロック");
    RegisterGimmick<GimmickBreakableBlock>("破壊ブロック");
    RegisterGimmick<GimmickCoin>("コイン");
    RegisterGimmick<GimmickSeesawFloor>("シーソー床");
    RegisterGimmick<GimmickDashPanel>("ダッシュパネル");
    RegisterGimmick<GimmickTimedSwitch>("時限スイッチ床");
    RegisterGimmick<GimmickAppearingFloor>("出現床");
    RegisterGimmick<GimmickSwitch>("汎用スイッチ");
    RegisterGimmick<GimmickEventReceiver>("イベント受信ギミック");
    RegisterGimmick<GimmickArenaEncounter>("中ボス遭遇管理");
    RegisterGimmick<GimmickGameplayVolume>("ゲームプレイボリューム");
    RegisterGimmick<GimmickCopyMemoryStation>("コピー記憶台");
    RegisterGimmick<GimmickPrismBarrier>("プリズム障壁");
    RegisterGimmick<GimmickBossGate>("ボス闘技場・格子ゲート");
    RegisterGimmick<GimmickHookPullBlock>("フック可動ブロック");
    RegisterGimmick<GimmickLiquidLevel>("水位・マグマ上下");
    RegisterGimmick<GimmickChainCollapseFloor>("連鎖崩れ床");
    RegisterGimmick<GimmickRotatingObject>("回転床", "RotatingFloor", GimmickRotatingObject::kModelPath,
        [](GimmickRotatingObject& object) {
            object.SetName("Gimmick_RotatingFloor");
            object.SetScale({3.0f, 0.3f, 1.2f});
            object.SetParameter(&engine::EntityParameter::actionMode, 1);
            object.SetParameter(&engine::EntityParameter::speed, 45.0f);
            object.SetParameter(&engine::EntityParameter::startActive, true);
        });
    RegisterGimmick<GimmickRotatingObject>("回転柱", "RotatingPillar", GimmickRotatingObject::kModelPath,
        [](GimmickRotatingObject& object) {
            object.SetName("Gimmick_RotatingPillar");
            object.SetScale({0.75f, 3.0f, 0.75f});
            object.SetParameter(&engine::EntityParameter::actionMode, 1);
            object.SetParameter(&engine::EntityParameter::speed, 60.0f);
            object.SetParameter(&engine::EntityParameter::startActive, true);
        });
    RegisterGimmick<GimmickPhaseFlipFloor>("順番反転床");
    RegisterGimmick<GimmickFireCannon>("火球砲台");
    RegisterGimmick<GimmickMagmaHazard>("マグマダメージ床");
    RegisterGimmick<GimmickMagmaGeyser>("マグマ・水の噴出口");
    RegisterGimmick<GimmickStageGate>("ステージゲート");
    RegisterGimmick<GimmickSinkingFloor>("沈む床");
    RegisterGimmick<GimmickMagmaGeyser>("水の噴出口", "WaterGeyser", "Stages/magma_pressure_column",
        [](GimmickMagmaGeyser& object) {
            object.SetParameter(&engine::EntityParameter::startActive, false);
            object.SetParameter(&engine::EntityParameter::returnOnOff, true);
            object.SetParameter(&engine::EntityParameter::actionMode, 3);
            object.SetParameter(&engine::EntityParameter::detectionRange, 4.0f);
        });
}

GimmickFactory* GimmickFactory::GetInstance() {
    static GimmickFactory instance;
    return &instance;
}

bool GimmickFactory::IsKnownType(const std::string& type) const {
    return std::any_of(registered_.begin(), registered_.end(), [&](const Entry& entry) { return entry.info.id == type; });
}

std::string GimmickFactory::GetCreationType(const BaseGimmick& object) const {
    // 同じC++クラスの別名は保存IDを優先します。不一致データだけ実クラスへ戻します。
    for (const auto& entry : registered_)
        if (entry.info.id == object.GetGimmickType() && entry.info.matches(object)) return entry.info.id;
    if (typeid(object) == typeid(BaseGimmick)) return object.GetGimmickType();
    for (const auto& entry : registered_) if (entry.info.matches(object)) return entry.info.id;
    return object.GetGimmickType();
}

std::unique_ptr<BaseGimmick> GimmickFactory::CreateGimmick(const std::string& gimmickName, engine::Object3DCommon* common) {
    std::unique_ptr<BaseGimmick> gimmick;
    for (const auto& entry : registered_) if (entry.info.id == gimmickName) { gimmick = entry.create(common); break; }
    if (!gimmick) {
        // 旧シーンに含まれる未知IDは、従来と同じ仮Objectとして保持します。
        gimmick = std::make_unique<BaseGimmick>();
        gimmick->Initialize(common, "Primitives/cube");
    }
    gimmick->SetGimmickType(gimmickName);
    return gimmick;
}
