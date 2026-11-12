#pragma once
#include "BaseGimmick.h"

// プレイヤーが取得できるコインギミック
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnCollision
class GimmickCoin : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "Coin";
    static constexpr const char* kModelPath = "Primitives/sphere";

    virtual ~GimmickCoin() = default;

    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // 取得時の跳ね上がりと、一時ドロップの落下・寿命は通常の回転より優先します。
    void Update(float deltaTime) override;

    // 取得済みなら同じ接触から枚数を二度加算しません。
    bool OnCollision(engine::Object3D* other) override;

    // 複製先の描画資源を初期化した上で、設定されたコインの見た目を引き継ぎます。
    std::unique_ptr<engine::Object3D> Clone() const override;

    // 敵からの一時ドロップに切り替えます。速度はワールド単位/秒、各時刻は秒、groundY はワールド Y 座標です。
    // 寿命は最低 0.2 秒に補正され、blinkStartTime は寿命を超えない値に丸められます。
    void ConfigureTemporaryDrop(const engine::Vector3& initialVelocity, float lifetime, float blinkStartTime, float groundY);

  protected:
    // 取得演出と一時ドロップの進行を保存し、Replay 再開後も同じ位置から続けます。
    void CaptureReplayCustomState(json& state) const override;

    // 欠けたキーは既定値のままにして、古い Replay も読めるようにします。
    void RestoreReplayCustomState(const json& state) override;

  private:
    // 取得演出を開始してからセーブ側の枚数を更新します。
    void Collect();

    // 落下中のコインは地面への接触後に減速し、期限直前だけ点滅させます。
    void UpdateTemporaryDrop(float deltaTime);

    // 描画・衝突の走査中に直接破棄せず、シーンの遅延削除へ依頼します。
    void RequestSelfRemove();

  private:
    bool isCollected_ = false;
    bool isTemporaryDrop_ = false;

    float rotationSpeed_ = 3.0f;
    float collectAnimationTimer_ = 0.0f;

    float dropAge_ = 0.0f;
    float dropLifetime_ = 0.0f;
    float dropBlinkStartTime_ = 0.0f;

    float dropGroundY_ = 0.0f;
    float dropGroundOffset_ = 0.0f;
    float dropSettleTimer_ = 0.0f;
    engine::Vector3 dropVelocity_ = engine::Vector3::ZERO;

    engine::Vector3 collectStartPosition_{};
    engine::Vector3 collectStartScale_{0.6f, 0.6f, 0.15f};
};
