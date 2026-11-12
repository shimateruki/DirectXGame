#pragma once

#include "BaseGimmick.h"
#include "game/system/cinematic/GateEntryPresentation.h"

class BaseScene;

/// <summary>ステージ選択ノードと入場ゲートの表示・遷移を管理します。</summary>
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnCollision
class GimmickStageGate : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "StageGate";
    static constexpr const char* kModelPath = "Gimmicks/portal_surface";

    ~GimmickStageGate() override = default;

    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // 目標開放度に滑らかに追従させ、開放の閾値をまたいだ瞬間だけ発光を強めます。
    void Update(float deltaTime) override;

    // ステージ選択用の展示ノードは、触れてもシーン遷移を起こしません。
    bool OnCollision(engine::Object3D* other) override;

    // 複製先の描画資源を初期化した後で、ゲート設定をコピーします。
    std::unique_ptr<engine::Object3D> Clone() const override;

    // エディターで設定された行き先情報とノードの動作モードを取得します。
    int GetStageIndex() const;

    int GetGateMode() const;

    const std::string& GetTargetSceneName() const;

    bool IsStageSelectNodeMode() const;

    /// <summary>選択・解放・クリア状態を設定し、次の更新でゲート表示へ反映します。</summary>
    /// <param name="selected">現在選択中かどうか。</param>
    /// <param name="unlocked">入場可能かどうか。</param>
    /// <param name="cleared">クリア済みかどうか。</param>
    /// <param name="unlocking">解放演出中かどうか。</param>
    void SetGateState(bool selected, bool unlocked, bool cleared, bool unlocking = false);

    // 発光の目標値と、ゲート遷移の許可を外部から指定します。
    void SetGateActivation(float activation);

    void SetTransitionEnabled(bool enabled);

    // ゲートに入った際の発光反応を開始します。
    void TriggerEntryReaction();

    bool IsUnlocked() const { return isUnlocked_; }

    bool IsEntryPresentationActive() const { return entryPresentation_.IsActive(); }
    float GetEntryPresentationProgress() const { return entryPresentation_.GetProgress(); }

    // 通常更新より先に演出だけを進め、操作ガイド・物理・敵の攻撃との競合を防ぎます。
    static bool UpdateSceneEntry(BaseScene& scene, float deltaTime);
    // Player とカメラが生存している Finalize の先頭で、借用参照をすべて外します。
    static void ResetSceneEntries(BaseScene& scene);

  private:
    // 元の縮尺を保存し、点滅演出による縮尺の累積を防ぎます。
    void CaptureBaseScale();

    // 専用材質がない複製・編集中は、シェーダーパラメータへのアクセスを避けます。
    void UpdatePortalMaterial();

    // 選択状態と演出状態を満たした場合にだけシーン遷移します。
    bool CanTriggerTransition() const;

    // Editor で演出中に行き先が変更されても、存在しないシーンへの要求で操作不能にしません。
    void TriggerTransition();

    engine::Vector4 GetTargetColor() const;

    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    float pulseTimer_ = 0.0f;

    float activation_ = 1.0f; // 現在のゲート出現率。
    float targetActivation_ = 1.0f; // 解錠・演出から要求された出現率。
    float activationBurst_ = 0.0f;
    float lastAppliedPulse_ = 1.0f;

    GateEntryPresentation entryPresentation_;
    bool transitionQueued_ = false; // 重複したシーン遷移要求を送らないための予約状態。

    bool hasBaseScale_ = false;
    bool hasTriggeredTransition_ = false; // この入口からシーン遷移を一度要求済みか。

    bool isSelected_ = false;
    bool isUnlocked_ = true;
    bool isCleared_ = false;
    bool isUnlocking_ = false;
};
