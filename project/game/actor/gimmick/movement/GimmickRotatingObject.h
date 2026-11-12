#pragma once
#include "BaseGimmick.h"
#include <cstdint>

/// <summary>トリガーやスイッチによって回転状態を切り替えるオブジェクトです。</summary>
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnTrigger / OnSwitchEvent
class GimmickRotatingObject : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "RotatingFloor";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickRotatingObject() = default;

    // 回転軸と速度の旧既定値を用意し、地形として足場判定を有効にする。
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // 編集中は回転を進めず、Play開始時に初期姿勢を取り直す。
    void Update(float deltaTime) override;

    // トリガーはスイッチの ON と同じ経路で扱う。
    void OnTrigger() override;

    // OFF で停止するかは設定値で決め、スイッチの状態を床の回転へ反映する。
    void OnSwitchEvent(bool active) override;

    // 回転開始時刻は複製せず、保存済みの配置と設定だけ引き継ぐ。
    std::unique_ptr<engine::Object3D> Clone() const override;

  private:
    bool initializedForPlay_ = false;
    bool active_ = true;
    float pathRotationOffset_ = 0.0f;
    engine::Object3D* supportFloor_ = nullptr;
    engine::Vector3 supportLocalPosition_{};
    engine::Vector3 supportStartRotation_{};
    engine::Vector3 startPosition_{};
    engine::Vector3 startRotation_{};
    uint32_t initialCollisionAttribute_ = 0;
    bool initialVisible_ = true;
};
