#pragma once
#include <memory>
#include <string>
#include "BaseGimmick.h"
#include "Object3dCommon.h"

// 文字列のギミックタイプ名から、対応するギミックを生成するファクトリ
class GimmickFactory {
  public:
    /// <summary>共有のギミックファクトリを返す。</summary>
    static GimmickFactory* GetInstance();

    /// <summary>種別名に対応するギミックを生成し、基本初期化する。</summary>
    /// <returns>未対応の種別名では仮表示用のBaseGimmickを返す。</returns>
    std::unique_ptr<BaseGimmick> CreateGimmick(const std::string& gimmickName, Object3dCommon* common);

  private:
    GimmickFactory() = default;
    ~GimmickFactory() = default;
    GimmickFactory(const GimmickFactory&) = delete;
    const GimmickFactory& operator=(const GimmickFactory&) = delete;
};
