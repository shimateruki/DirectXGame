#pragma once
#include <memory>
#include <string>
#include "BaseGimmick.h"
#include "Object3dCommon.h"

class GimmickFactory {
public:
    // シングルトンインスタンス取得
    static GimmickFactory* GetInstance();

    // 名前からギミックを作成する関数
    std::unique_ptr<BaseGimmick> CreateGimmick(const std::string& gimmickName, Object3dCommon* common);

private:
    GimmickFactory() = default;
    ~GimmickFactory() = default;
    GimmickFactory(const GimmickFactory&) = delete;
    const GimmickFactory& operator=(const GimmickFactory&) = delete;
};
