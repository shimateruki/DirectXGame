#pragma once
#include "BaseGimmick.h"

/// <summary>フックに引かれた位置へ移動するブロックです。</summary>
class GimmickHookPullBlock : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "HookPullBlock";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickHookPullBlock() = default;

    // 共通ギミックとして登録した後、フック対象として識別できる型を設定する。
    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    // パラメータ未設定の配置でも既定値を用意してから移動を更新する。
    void Update(float deltaTime) override;

    // 地面属性以外との接触では引き寄せ中の位置補正を行わない。
    bool OnCollision(engine::Object3d* other) override;

    // 新しい実体を初期化してから配置とフック設定を引き継ぐ。
    std::unique_ptr<engine::Object3d> Clone() const override;

    /// <summary>フックを持つキャラクターへ向けてブロックの移動を開始します。</summary>
    /// <param name="hookOwnerPos">引き寄せ先となるフック使用者のワールド座標。</param>
    void StartHookPull(const engine::Vector3& hookOwnerPos);

  private:
    engine::Vector3 velocity_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 pullTarget_ = {0.0f, 0.0f, 0.0f};
    float pullTimer_ = 0.0f;
};
