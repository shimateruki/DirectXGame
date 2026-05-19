#include "GimmickFactory.h"
#include "SceneManager.h"
#include "GimmickMovingFloor.h"
#include "GimmickTrampoline.h"
#include "GimmickChikuwaBlock.h"
#include "GimmickBlinkBlock.h"
#include "GimmickBreakableBlock.h"
#include "GimmickCoin.h"

GimmickFactory* GimmickFactory::GetInstance() {
    static GimmickFactory instance;
    return &instance;
}

std::unique_ptr<BaseGimmick> GimmickFactory::CreateGimmick(const std::string& gimmickName, Object3dCommon* common) {
    std::unique_ptr<BaseGimmick> newGimmick = nullptr;

    // 例：動く床の場合
    if (gimmickName == "MovingFloor") {
        auto floor = std::make_unique<GimmickMovingFloor>();
        floor->Initialize(common, "block");
        
        if (!floor->param_.has_value()) floor->param_.emplace();
        auto& p = floor->param_.value();
        p.speed = 0.05f; // 動くスピードなど
        
        newGimmick = std::move(floor);
    }
    // ジャンプ台の場合
    else if (gimmickName == "Trampoline") {
        auto trampoline = std::make_unique<GimmickTrampoline>();
        trampoline->Initialize(common, "cube"); // とりあえず立方体
        newGimmick = std::move(trampoline);
    }
    // ちくわブロックの場合
    else if (gimmickName == "ChikuwaBlock") {
        auto chikuwa = std::make_unique<GimmickChikuwaBlock>();
        chikuwa->Initialize(common, "block"); // 足場用のモデル
        newGimmick = std::move(chikuwa);
    }
    // 点滅ブロックの場合
    else if (gimmickName == "BlinkBlock") {
        auto blink = std::make_unique<GimmickBlinkBlock>();
        blink->Initialize(common, "block");
        newGimmick = std::move(blink);
    }
    // 壊せるブロックの場合
    else if (gimmickName == "BreakableBlock") {
        auto breakable = std::make_unique<GimmickBreakableBlock>();
        breakable->Initialize(common, "block");
        newGimmick = std::move(breakable);
    }
    // コインの場合
    else if (gimmickName == "Coin") {
        auto coin = std::make_unique<GimmickCoin>();
        coin->Initialize(common, "sphere"); // sphereモデルをデフォルトに設定
        newGimmick = std::move(coin);
    }

    // 該当するギミックがない場合、またはベースを直接生成する場合
    if (!newGimmick) {
        newGimmick = std::make_unique<BaseGimmick>();
        newGimmick->Initialize(common, "cube");
    }

    // 名前や種類を設定
    newGimmick->SetGimmickType(gimmickName);

    return newGimmick;
}
