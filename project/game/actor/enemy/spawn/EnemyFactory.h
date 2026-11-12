#pragma once
#include <memory>
#include <string>
#include "BaseEnemy.h"
#include "Object3DCommon.h"

// 文字列の敵タイプ名から、対応する敵インスタンスを生成するファクトリ
// 関連：LevelLoaderや生成処理が敵タイプ名を渡す。モデル初期化と共通数値を設定し、所有権を呼出側へ返す。
// 主な入口：GetInstance / CreateEnemy
class EnemyFactory {
  public:
    /// <summary>共有の敵ファクトリを返す。</summary>
    static EnemyFactory* GetInstance();

    /// <summary>種別名に対応する敵を生成し、モデル・ステータス・検知範囲を設定する。</summary>
    /// <returns>未対応の種別名では仮表示用のBaseEnemyを返す。</returns>
    std::unique_ptr<BaseEnemy> CreateEnemy(const std::string& enemyName, engine::Object3DCommon* common);

  private:
    EnemyFactory() = default;

    ~EnemyFactory() = default;

    EnemyFactory(const EnemyFactory&) = delete;

    const EnemyFactory& operator=(const EnemyFactory&) = delete;
};
