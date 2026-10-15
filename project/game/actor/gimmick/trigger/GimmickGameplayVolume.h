#pragma once

#include "BaseGimmick.h"
#include "LightManager.h"

class Player;

// 配置した範囲への進入・退出を、イベントやチェックポイントなどのゲーム処理へ変換します。
class GimmickGameplayVolume : public BaseGimmick {
  public:
    /// 範囲の種類と Scene JSON の設定を初期化する。
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    /// 進入・退出と再起動待ちを更新する。
    void Update(float deltaTime) override;
    /// 編集中の範囲表示を描画する。
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    /// プレイヤーとの接触を今回フレームの進入判定へ記録する。
    bool OnCollision(engine::Object3d* other) override;
    /// 外部トリガーから範囲効果を起動する。
    void OnTrigger() override;
    /// スイッチに応じて範囲効果を有効・無効にする。
    void OnSwitchEvent(bool active) override;

    /// 配置用の範囲ギミック複製を作る。
    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    enum class Mode {
        Event = 0,
        Checkpoint,
        FallDeath,
        FeedbackCue,
        Bgm,
        Environment,
    };

    /// Play 開始時の占有状態と環境設定を準備する。
    void InitializeForPlay();
    /// Editor に戻るとき一時的な範囲効果を解除する。
    void ResetForEditor();
    /// プレイヤーの範囲内への進入を処理する。
    void HandleEnter(Player* player);
    /// プレイヤーの範囲外への退出を処理する。
    void HandleExit(Player* player);
    /// 種別に応じたイベント、チェックポイント、音、環境を適用する。
    void Activate(Player* player);
    /// 適用中の範囲効果を解除する。
    void Deactivate();
    /// 現在の起動条件を満たすか返す。
    bool CanActivate() const;
    /// Scene JSON の設定から範囲効果の種別を返す。
    Mode GetMode() const;

    bool initializedForPlay_ = false;
    bool runtimeEnabled_ = true;
    bool occupied_ = false;
    bool collisionObserved_ = false;
    bool triggered_ = false;
    float rearmTimer_ = 0.0f;
    // シーンが所有するプレイヤーへの非所有参照。退出検出後はクリアします。
    Player* occupant_ = nullptr;
    LightManager::EnvironmentProfileState previousEnvironment_{};
    bool hasPreviousEnvironment_ = false;
};
