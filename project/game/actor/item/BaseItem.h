#pragma once
#include "Object3D.h"
#include <memory>
#include <string>

class Player;

// プレイヤーが拾って効果を発動するアイテムの基底クラス
// 関連：Object3dの配置と衝突基盤を使い、取得などの固有処理は派生側へ任せる。
// 主な入口：Initialize / Update / OnCollision
class BaseItem : public engine::Object3D {
  public:
    virtual ~BaseItem() = default;

    virtual void Initialize(engine::Object3DCommon* common, const std::string& modelName);

    // 取得済みのアイテムは同じフレーム中にも再取得できないようにします。
    void Update(float deltaTime) override;

    // 属性だけで確定せず、相手の型と実際の重なりを確認してから取得します。
    bool OnCollision(engine::Object3D* other) override;

    // 初期化で描画・判定資源を作り直してから、編集可能な設定値を引き継ぎます。
    std::unique_ptr<engine::Object3D> Clone() const override;

  protected:
    // 派生アイテムで取得効果を実装し、MarkCollected で重複取得を防ぎます。
    virtual void Collect(Player* player);

    // 死亡フラグが反映される前にも、表示と判定を即時に止めます。
    void MarkCollected();

    /// <summary>取得済み状態の参照と、複製・Replay復元を行う。</summary>
    bool IsCollected() const { return isCollected_; }
    // 取得済み状態だけを復元する。取得イベントや報酬を再発生させない。
    void RestoreCollected(bool collected) { isCollected_ = collected; }
private:

    bool isCollected_ = false;
    float rotationSpeed_ = 2.4f;
};
