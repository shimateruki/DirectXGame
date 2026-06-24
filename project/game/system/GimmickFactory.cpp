#include "GimmickFactory.h"
#include "SceneManager.h"
#include "GimmickMovingFloor.h"
#include "GimmickTrampoline.h"
#include "GimmickChikuwaBlock.h"
#include "GimmickBlinkBlock.h"
#include "GimmickBreakableBlock.h"
#include "GimmickCoin.h"
#include "GimmickHookAnchor.h"
#include "GimmickSinkingFloor.h"
#include "GimmickSeesawFloor.h"
#include "GimmickDashPanel.h"
#include "GimmickIceFloor.h"
#include "GimmickTimedSwitch.h"
#include "GimmickAppearingFloor.h"
#include "GimmickSwitch.h"
#include "GimmickEventReceiver.h"
#include "GimmickHookPullBlock.h"
#include "GimmickOneWayFloor.h"
#include "GimmickLiquidLevel.h"
#include "GimmickChainCollapseFloor.h"
#include "GimmickRotatingObject.h"
#include "GimmickPhaseFlipFloor.h"
#include "GimmickLaserEmitter.h"
#include "GimmickLaserNode.h"
#include "GimmickStageGate.h"
#include "GimmickFireCannon.h"

GimmickFactory* GimmickFactory::GetInstance() {
    static GimmickFactory instance;
    return &instance;
}

// ギミックタイプ名ごとに専用クラスを作る。新規ギミック追加時はここへ分岐を追加する。
std::unique_ptr<BaseGimmick> GimmickFactory::CreateGimmick(const std::string& gimmickName, Object3dCommon* common) {
    std::unique_ptr<BaseGimmick> newGimmick = nullptr;

    // 足場系
    if (gimmickName == "MovingFloor") {
        auto floor = std::make_unique<GimmickMovingFloor>();
        floor->Initialize(common, "Stages/block");
        
        if (!floor->param_.has_value()) floor->param_.emplace();
        auto& p = floor->param_.value();
        p.speed = 0.05f; // 動くスピードなど
        
        newGimmick = std::move(floor);
    }
    else if (gimmickName == "Trampoline") {
        auto trampoline = std::make_unique<GimmickTrampoline>();
        trampoline->Initialize(common, "Primitives/cube"); // とりあえず立方体
        newGimmick = std::move(trampoline);
    }
    else if (gimmickName == "ChikuwaBlock") {
        auto chikuwa = std::make_unique<GimmickChikuwaBlock>();
        chikuwa->Initialize(common, "Stages/block"); // 足場用のモデル
        newGimmick = std::move(chikuwa);
    }
    else if (gimmickName == "BlinkBlock") {
        auto blink = std::make_unique<GimmickBlinkBlock>();
        blink->Initialize(common, "Stages/block");
        newGimmick = std::move(blink);
    }
    else if (gimmickName == "BreakableBlock") {
        auto breakable = std::make_unique<GimmickBreakableBlock>();
        breakable->Initialize(common, "Stages/bomb_break_block");
        newGimmick = std::move(breakable);
    }
    // 収集/フック/スイッチ系
    else if (gimmickName == "Coin") {
        auto coin = std::make_unique<GimmickCoin>();
        coin->Initialize(common, "Primitives/sphere"); // sphereモデルをデフォルトに設定
        newGimmick = std::move(coin);
    }
    else if (gimmickName == "HookAnchor") {
        auto anchor = std::make_unique<GimmickHookAnchor>();
        anchor->Initialize(common, "Primitives/sphere");
        newGimmick = std::move(anchor);
    }
    else if (gimmickName == "SinkingFloor") {
        auto floor = std::make_unique<GimmickSinkingFloor>();
        floor->Initialize(common, "Stages/block");
        newGimmick = std::move(floor);
    }
    else if (gimmickName == "SeesawFloor") {
        auto floor = std::make_unique<GimmickSeesawFloor>();
        floor->Initialize(common, "Stages/block");
        newGimmick = std::move(floor);
    }
    else if (gimmickName == "DashPanel") {
        auto panel = std::make_unique<GimmickDashPanel>();
        panel->Initialize(common, "Stages/block");
        newGimmick = std::move(panel);
    }
    else if (gimmickName == "IceFloor") {
        auto floor = std::make_unique<GimmickIceFloor>();
        floor->Initialize(common, "Stages/block");
        newGimmick = std::move(floor);
    }
    else if (gimmickName == "TimedSwitch") {
        auto switchFloor = std::make_unique<GimmickTimedSwitch>();
        switchFloor->Initialize(common, "Stages/block");
        newGimmick = std::move(switchFloor);
    }
    else if (gimmickName == "AppearingFloor") {
        auto floor = std::make_unique<GimmickAppearingFloor>();
        floor->Initialize(common, "Stages/block");
        newGimmick = std::move(floor);
    }
    else if (gimmickName == "Switch") {
        auto switchObj = std::make_unique<GimmickSwitch>();
        switchObj->Initialize(common, "Stages/block");
        newGimmick = std::move(switchObj);
    }
    else if (gimmickName == "EventReceiver") {
        auto receiver = std::make_unique<GimmickEventReceiver>();
        receiver->Initialize(common, "Stages/block");
        newGimmick = std::move(receiver);
    }
    else if (gimmickName == "HookPullBlock") {
        auto block = std::make_unique<GimmickHookPullBlock>();
        block->Initialize(common, "Stages/block");
        newGimmick = std::move(block);
    }
    else if (gimmickName == "OneWayFloor") {
        auto floor = std::make_unique<GimmickOneWayFloor>();
        floor->Initialize(common, "Stages/block");
        newGimmick = std::move(floor);
    }
    else if (gimmickName == "LiquidLevel") {
        auto liquid = std::make_unique<GimmickLiquidLevel>();
        liquid->Initialize(common, "Stages/block");
        newGimmick = std::move(liquid);
    }
    else if (gimmickName == "ChainCollapseFloor") {
        auto floor = std::make_unique<GimmickChainCollapseFloor>();
        floor->Initialize(common, "Stages/block");
        newGimmick = std::move(floor);
    }
    else if (gimmickName == "RotatingFloor") {
        auto floor = std::make_unique<GimmickRotatingObject>();
        floor->Initialize(common, "Stages/block");
        floor->SetGimmickType("RotatingFloor");
        floor->SetName("Gimmick_RotatingFloor");
        floor->SetScale({ 3.0f, 0.3f, 1.2f });
        if (!floor->param_.has_value()) floor->param_.emplace();
        floor->param_->actionMode = 1;
        floor->param_->speed = 45.0f;
        floor->param_->startActive = true;
        newGimmick = std::move(floor);
    }
    else if (gimmickName == "RotatingPillar") {
        auto pillar = std::make_unique<GimmickRotatingObject>();
        pillar->Initialize(common, "Stages/block");
        pillar->SetGimmickType("RotatingPillar");
        pillar->SetName("Gimmick_RotatingPillar");
        pillar->SetScale({ 0.75f, 3.0f, 0.75f });
        if (!pillar->param_.has_value()) pillar->param_.emplace();
        pillar->param_->actionMode = 1;
        pillar->param_->speed = 60.0f;
        pillar->param_->startActive = true;
        newGimmick = std::move(pillar);
    }
    else if (gimmickName == "PhaseFlipFloor") {
        auto floor = std::make_unique<GimmickPhaseFlipFloor>();
        floor->Initialize(common, "Stages/block");
        newGimmick = std::move(floor);
    }
    else if (gimmickName == "LaserEmitter") {
        auto emitter = std::make_unique<GimmickLaserEmitter>();
        emitter->Initialize(common, "Primitives/cube");
        newGimmick = std::move(emitter);
    }
    else if (gimmickName == "LaserNode") {
        auto node = std::make_unique<GimmickLaserNode>();
        node->Initialize(common, "Primitives/sphere");
        newGimmick = std::move(node);
    }
    else if (gimmickName == "FireCannon") {
        auto cannon = std::make_unique<GimmickFireCannon>();
        cannon->Initialize(common, "Primitives/cube");
        newGimmick = std::move(cannon);
    }
    else if (gimmickName == "StageGate") {
        auto gate = std::make_unique<GimmickStageGate>();
        gate->Initialize(common, "Gimmicks/portal_surface");
        newGimmick = std::move(gate);
    }

    // 該当するギミックがない場合でも、配置確認できる仮のキューブを出す
    if (!newGimmick) {
        newGimmick = std::make_unique<BaseGimmick>();
        newGimmick->Initialize(common, "Primitives/cube");
    }

    // 生成後にタイプ名を保存して、エディタやイベント連携で参照できるようにする
    newGimmick->SetGimmickType(gimmickName);

    return newGimmick;
}
