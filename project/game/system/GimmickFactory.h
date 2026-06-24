#pragma once
#include <memory>
#include <string>
#include "BaseGimmick.h"
#include "Object3dCommon.h"

// 文字列のギミックタイプ名から、対応するギミックを生成するファクトリ
class GimmickFactory {
public:
    // シングルトンインスタンスを取得する
    static GimmickFactory* GetInstance();

    // gimmickName に対応するギミックを生成し、基本初期化まで行う
    std::unique_ptr<BaseGimmick> CreateGimmick(const std::string& gimmickName, Object3dCommon* common);

private:
    GimmickFactory() = default;
    ~GimmickFactory() = default;
    GimmickFactory(const GimmickFactory&) = delete;
    const GimmickFactory& operator=(const GimmickFactory&) = delete;
};
