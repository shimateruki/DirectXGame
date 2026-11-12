#pragma once

#include "BaseGimmick.h"

// ボス戦開始時に上から閉じる、実体モデル付きの格子ゲートです。
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnTrigger / OnSwitchEvent
class GimmickBossGate final : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "BossGate";
    static constexpr const char* kModelPath = "Stages/high_crown/high_crown_gate.obj";

    // 閉鎖中の門を壁として扱い、開く方向と所要時間の既定値を用意する。
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // エディターでは配置位置へ戻し、プレイ中のみ閉鎖率に応じて門を動かします。
    void Update(float deltaTime) override;

    // イベントからの起動はスイッチ ON と同じ閉鎖経路へ渡す。
    void OnTrigger() override;

    // シネマティック制御中は外部スイッチで門を上書きしない。
    void OnSwitchEvent(bool active) override;

    // 編集配置を複製し、実行時の閉鎖率は新しい門で計算する。
    std::unique_ptr<engine::Object3D> Clone() const override;

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

    engine::Vector3 authoredPosition_{}; // Editorで指定した位置。Play終了時の復元に使う。
    engine::Vector3 authoredScale_{1.0f, 1.0f, 1.0f}; // Editorで指定した倍率。表示の開閉変形と分離して退避する。
    uint32_t authoredCollisionAttribute_ = 0; // Play開始前の衝突属性。
    uint32_t authoredCollisionMask_ = 0; // Play開始前の衝突マスク。
    float closure_ = 0.0f; // 現在の閉鎖率。0が開放、1が閉鎖。
    float targetClosure_ = 0.0f; // スイッチなどから要求された閉鎖率。
    bool authoredStateCaptured_ = false; // 復元用の配置・衝突情報を退避済みか。
    bool initializedForPlay_ = false; // 現在のPlayセッションで初期化済みか。
    bool activationRequested_ = false;
    bool cinematicOverride_ = false; // シーン演出の直接制御を通常の開閉処理より優先する。
    bool introTimelineControl_ = false; // 登場Timelineの制御中は通常の位置補間と競合させない。
};
