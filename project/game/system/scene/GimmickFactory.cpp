#include "game/system/scene/GimmickFactory.h"
#include "GimmickMovingFloor.h"
#include "GimmickHazardRideFloor.h"
#include "GimmickTrampoline.h"
#include "GimmickChikuwaBlock.h"
#include "GimmickBlinkBlock.h"
#include "GimmickBreakableBlock.h"
#include "MeshRenderer.h"
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
#include "GimmickGuideSign.h"
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
#include "GimmickContactHazard.h"
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
    // 通常の破壊ブロックを流用し、ダッシュパネル経由の衝突専用に薄い判定と個別の材質を設定します。
    RegisterGimmick<GimmickBreakableBlock>("白ガラス（ダッシュパネル専用）", "DashPanelWhiteGlass",
        "Stages/star_garden_glass_panel", [](GimmickBreakableBlock& object) {
            object.ResetParameters();
            object.SetParameter(&engine::EntityParameter::actionMode, GimmickBreakableBlock::kDashPanelOnly);
            object.SetScale({4.0f, 3.0f, 1.0f});
            engine::Object3D::ColliderConfig collider;
            collider.type = ColliderType::Obb;
            collider.size = {0.5f, 0.5f, 0.08f};
            object.SetColliderConfig(collider);

            // 既存の屈折表示を使い、奥の道が透ける乳白色のガラスにします。
            object.SetMaterialType(10);
            object.SetBlendMode(BlendMode::Normal);
            object.SetCastShadow(false);
            object.SetColor({0.98f, 0.99f, 1.0f, 0.36f});
            if (auto* glass = object.GetMeshRenderer()->GetWaterParamData()) {
                glass->effectType = 1.0f;
                glass->effectIntensity = 0.22f;
                glass->effectScale = 0.38f;
                glass->effectScaleX = 0.5f;
                glass->effectScaleY = 0.5f;
                glass->effectScaleZ = 0.08f;
                glass->effectSoftness = 0.58f;
            }
        });
    RegisterGimmick<GimmickCoin>("コイン");
    RegisterGimmick<GimmickSeesawFloor>("シーソー床");
    RegisterGimmick<GimmickDashPanel>("ダッシュパネル");
    // 加速用とは別の保存IDと青い見た目にし、プレイヤーだけを検出する解除パネルを登録します。
    RegisterGimmick<GimmickDashPanel>("ダッシュ解除パネル（青）", GimmickDashPanel::kStopTypeId,
        "Stages/star_garden_dash_panel", [](GimmickDashPanel& object) {
            object.SetName("Gimmick_DashPanelStop");
            object.SetScale({1.0f, 1.0f, 1.0f});
            object.SetColor({0.04f, 0.38f, 1.0f, 1.0f});
            object.SetMaterialType(0);
            object.SetShaderMaterialId("dash_panel_stop");
            // 走路のパネルと同じ寸法で、上面に触れたプレイヤーを検出します。
            engine::Object3D::ColliderConfig collider;
            collider.type = ColliderType::Obb;
            collider.center = {0.0f, -0.08f, 0.0f};
            collider.size = {8.35f, 0.20f, 2.52f};
            object.SetColliderConfig(collider);
            object.SetCollisionAttribute(CollisionCategories::Get("trigger"));
            object.SetCollisionMask(CollisionCategories::Get("player"));
        });
    RegisterGimmick<GimmickTimedSwitch>("時限スイッチ床");
    RegisterGimmick<GimmickAppearingFloor>("出現床");
    RegisterGimmick<GimmickSwitch>("汎用スイッチ");
    RegisterGimmick<GimmickEventReceiver>("イベント受信ギミック");
    RegisterGimmick<GimmickArenaEncounter>("中ボス遭遇管理");
    RegisterGimmick<GimmickGameplayVolume>("ゲームプレイボリューム");
    RegisterGimmick<GimmickGuideSign>("案内看板");
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
    RegisterGimmick<GimmickContactHazard>("接触ダメージ障害物");
    RegisterGimmick<GimmickMagmaGeyser>("マグマ・水の噴出口");
    RegisterGimmick<GimmickStageGate>("ステージゲート");
    RegisterGimmick<GimmickSinkingFloor>("沈む床");
    // 噴出口の実装は共通とし、水用のIDから作った場合だけ停止時の戻り方と動作モードを上書きします。
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
    // 型情報を失った仮オブジェクトでは、保存済みの未知IDを保持します。
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
    // 同じクラスの派生設定を区別できるよう、実際に要求されたIDを個体へ残します。
    gimmick->SetGimmickType(gimmickName);
    return gimmick;
}
