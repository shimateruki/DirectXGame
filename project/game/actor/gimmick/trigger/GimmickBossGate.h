#pragma once

#include "BaseGimmick.h"

// ボス戦開始時に上から閉じる、実体モデル付きの格子ゲートです。
class GimmickBossGate final : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "BossGate";
    static constexpr const char* kModelPath = "Stages/high_crown/high_crown_gate.obj";

    // 閉鎖中の門を壁として扱い、開く方向と所要時間の既定値を用意する。
    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    // エディターでは配置位置へ戻し、プレイ中のみ閉鎖率に応じて門を動かします。
    void Update(float deltaTime) override;

    // イベントからの起動はスイッチ ON と同じ閉鎖経路へ渡す。
    void OnTrigger() override;

    // シネマティック制御中は外部スイッチで門を上書きしない。
    void OnSwitchEvent(bool active) override;

    // 編集配置を複製し、実行時の閉鎖率は新しい門で計算する。
    std::unique_ptr<engine::Object3d> Clone() const override;

    // 開門中の退避座標ではなく、シーンで編集した閉鎖位置を返します。
    engine::Vector3 GetClosedPosition();

    // 崩壊演出中はシーン側に配置制御を渡し、開閉状態は保持します。
    void SetCinematicOverride(bool enabled) { cinematicOverride_ = enabled; }

    // 登場タイムラインに移動を渡し、閉鎖後は通常の開閉状態へ引き継ぎます。
    void SetIntroTimelineControl(bool enabled);

  private:
    // 実行時の移動や衝突切替で、編集時の Transform とマスクを失わないよう保存する。
    void CaptureAuthoredState(bool force = false);

    // 再生停止時は演出上書きと閉鎖率を消し、編集で決めた位置を復元する。
    void ResetForEditor();

    // 閉鎖率に応じて門を上下させ、十分閉じるまで壁の接触を有効にしない。
    void ApplyRuntimeState();

    void SetCollisionEnabled(bool enabled);

    float GetTransitionDuration() const;

    float GetTravelDistance() const;

    engine::Vector3 authoredPosition_{};
    engine::Vector3 authoredScale_{1.0f, 1.0f, 1.0f};
    uint32_t authoredCollisionAttribute_ = 0;
    uint32_t authoredCollisionMask_ = 0;
    float closure_ = 0.0f;
    float targetClosure_ = 0.0f;
    bool authoredStateCaptured_ = false;
    bool initializedForPlay_ = false;
    bool activationRequested_ = false;
    bool cinematicOverride_ = false;
    bool introTimelineControl_ = false;
};
