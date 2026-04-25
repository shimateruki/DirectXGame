#include "GimmickFactory.h"
#include "SceneManager.h"
#include "GimmickMovingFloor.h"
#include "GimmickTrampoline.h"

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

    // 該当するギミックがない場合、またはベースを直接生成する場合
    if (!newGimmick) {
        newGimmick = std::make_unique<BaseGimmick>();
        newGimmick->Initialize(common, "cube");
    }

    // 名前や種類を設定
    newGimmick->SetGimmickType(gimmickName);

    return newGimmick;
}
