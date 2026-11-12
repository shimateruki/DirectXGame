#pragma once

#include "BaseGimmick.h"

// 中ボス戦中だけ立ち上がる、当たり判定付きのプリズム障壁です。
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnTrigger / OnSwitchEvent
class GimmickPrismBarrier : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "PrismBarrier";
    static constexpr const char* kModelPath = "Gimmicks/portal_surface";

    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // 再生停止時は編集配置を復元し、実行時のみ開閉補間を進めます。
    void Update(float deltaTime) override;

    // 単発イベントは壁を出すスイッチのオンとして扱います。
    void OnTrigger() override;

    // 無効化の要求時に衝突を先に消し、見た目の縮退とは切り離します。
    void OnSwitchEvent(bool active) override;

    // 初期化後に編集設定を移し、壁の材質と判定を複製先で作り直します。
    std::unique_ptr<engine::Object3D> Clone() const override;

  private:
    // 再生中の変形値で編集時の基準を上書きしないよう、初回のみ記録します。
    void CaptureAuthoredState(bool force = false);

    // 再生中の高さや衝突を持ち越さず、保存された配置へ戻します。
    void ResetForEditor();

    // 壁の底辺を固定して上へ伸ばし、開閉中の見た目と衝突を同期します。
    void ApplyRuntimeVisual();

    void SetCollisionEnabled(bool enabled);

    float GetTransitionDuration() const;

    engine::Vector3 authoredPosition_{}; // Editorで指定した位置。Play終了時の復元に使う。
    engine::Vector3 authoredScale_{1.0f, 1.0f, 1.0f}; // Editorで指定した倍率。表示の開閉変形と分離して退避する。
    engine::Vector4 authoredColor_{0.30f, 0.86f, 1.0f, 0.84f};
    uint32_t authoredCollisionAttribute_ = 0; // Play開始前の衝突属性。
    uint32_t authoredCollisionMask_ = 0; // Play開始前の衝突マスク。
    float activation_ = 0.0f; // 現在の出現率。0が非表示、1が出現完了。
    float targetActivation_ = 0.0f; // 外部通知から要求された出現率。
    float visualTime_ = 0.0f;
    bool authoredStateCaptured_ = false; // 復元用の配置・衝突情報を退避済みか。
    bool initializedForPlay_ = false; // 現在のPlayセッションで初期化済みか。
    bool activationRequestReceived_ = false;
};
