#pragma once

#include "CinematicPlayer.h"
#include "CinematicSequence.h"
#include "engine/utility/math/Math.h"
#include "json.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class BaseScene;
class BaseEnemy;
class GimmickStageGate;
class InputManager;
namespace engine { class Object3d; }
class Player;
namespace engine { class Sprite; }

// チュートリアルの手順、短時間演出、操作表示、チェックポイントをJSONから進行します。
class TutorialDirector {
  public:
    using CompletionCallback = std::function<void()>;

    /// <summary>進行 JSON を読み込み、対象シーンとプレイヤーに紐付けます。</summary>
    /// <param name="flowPath">チュートリアルの手順と演出を記述した JSON</param>
    /// <returns>参照先と JSON を準備できた場合は true。scene、player、inputManager は借用します。</returns>
    bool Initialize(BaseScene* scene, Player* player, InputManager* inputManager,
                    const std::string& flowPath = "Resources/json/tutorial/tutorial_flow.json");
    /// チュートリアルの演出と借用参照を解放する。
    void Finalize();
    // 経過秒数で課題・演出・案内を進める。0 秒なら編集停止中の開始演出を起動しない。
    void Update(float deltaTime);

    // UI とロックオンの外部イベントを、現在の手順の達成条件へ反映します。
    void NotifyControlsGuideOpened();
    /// ロックオン対象を現在手順の達成条件へ通知する。
    void NotifyLockOnTarget(const engine::Object3d* target);
    /// 進行完了を保存する呼び出し側の処理を登録する。
    void SetCompletionCallback(CompletionCallback callback) { completionCallback_ = std::move(callback); }

    bool IsLoaded() const { return loaded_; }
    bool IsCompleted() const { return completed_; }
    /// <summary>完了後の出口をセーブ成功時だけ開けます。</summary>
    void SetCompletionGateEnabled(bool enabled) {
        if (completed_) SetGateTransitionEnabled(enabled);
    }
    bool IsCinematicPlaying() const { return cinematicActive_; }
    int GetCurrentStepIndex() const { return currentStepIndex_; }
    /// 現在手順の JSON 上の ID を返す。
    const std::string& GetCurrentStepId() const;

    // リプレイに必要な進行状態だけを保存・復元します。
    void CaptureReplayState(nlohmann::json& state) const;
    void RestoreReplayState(const nlohmann::json& state);

    // 最初から開始するか、編集用に指定した手順へ移動します。
    void Restart();
    // 該当 ID がなければ false。移動時は手順に応じて敵の生成状態も復元する。
    bool JumpToStep(const std::string& stepId);

#ifdef USE_IMGUI
    /// 手順移動と条件の確認用 UI を表示する。
    void DrawImGui();
#endif

  private:
    enum class CompletionCondition {
        kTimer,
        kCinematicFinished,
        kMoveDistance,
        kActionTriggered,
        kTargetLocked,
        kReachPosition,
        kCarryEnemy,
        kEnemyThrown,
        kEnemiesDefeated,
        kMorphActive,
        kMorphReleased,
        kControlsGuideOpened,
    };

    // JSON の一手順。完了条件と、その間に表示・生成・解除する対象をまとめます。
    struct Step {
        std::string id;
        CompletionCondition condition = CompletionCondition::kTimer;
        std::string action;
        std::string cinematicPath;
        std::string promptLabel;
        std::string objectiveTarget;
        std::vector<std::string> promptIcons;
        std::vector<std::string> promptIconsGamepad;
        std::vector<std::string> objectiveTargets;
        std::vector<std::string> targetEnemyObjects;
        std::vector<std::string> spawnEnemyObjects;
        std::vector<std::string> despawnEnemyObjects;
        engine::Vector3 targetPosition = {0.0f, 0.0f, 0.0f};
        engine::Vector3 checkpointPosition = {0.0f, 0.0f, 0.0f};
        float threshold = 0.0f;
        float radius = 3.0f;
        float objectiveOffsetY = 0.75f;
        float minimumDisplayTime = 0.12f;
        float completionDelay = 0.0f;
        bool hasTargetPosition = false;
        bool hasCheckpoint = false;
        bool showPrompt = true;
        bool requireMorph = false;
        bool previewGateUnlock = false;
        bool unlockGateOnComplete = false;
        bool spawnEnemy = false;
        bool spawnEnemiesDormant = false;
        bool allowCarriedThrow = true;
        bool allowCarriedAbsorb = true;
        std::string spawnEnemyObject;
        std::string despawnEnemyObject;
        std::string wrongActionRetryStep;
    };

    // 手順を戻した際にも同じ敵を再利用できるよう、初期状態を控えます。
    struct SpawnEnemySlot {
        std::string objectName;
        BaseEnemy* enemy = nullptr;
        engine::Vector3 initialPosition = {0.0f, 0.0f, 0.0f};
        uint32_t collisionAttribute = 0;
        uint32_t collisionMask = 0;
        float maxHpOverride = -1.0f;
        float detectionRangeOverride = -1.0f;
        bool active = false;
    };

    // JSON 配置を維持したまま、手順中の表示とフェードだけ更新します。
    struct PromptVisual {
        engine::Sprite* sprite = nullptr;
        engine::Vector2 basePosition = {0.0f, 0.0f};
        engine::Vector2 baseSize = {0.0f, 0.0f};
        engine::Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
        bool requestedVisible = false;
    };

    // 読み込みと条件名の解析。失敗した場合は未ロード状態のままにします。
    bool LoadFlow(const std::string& flowPath);
    /// JSON の条件名を内部の判定種別へ変換する。
    static CompletionCondition ParseCondition(const std::string& conditionName);
    /// JSON 配列から三次元座標を読み取れたか返す。
    static bool ReadVector3(const nlohmann::json& value, engine::Vector3& destination);

    // ステップの入退場で敵、ゲート、演出の状態を切り替えます。
    void BeginFlowIfNeeded();
    /// 指定手順の敵、操作案内、演出を有効にする。
    void EnterStep(int stepIndex);
    /// 現在手順だけで使う演出と表示を片付ける。
    void LeaveCurrentStep();
    /// 現在手順を終了して次の手順へ進む。
    void AdvanceStep();
    /// 現在手順の完了条件を満たしたか返す。
    bool EvaluateCurrentStep() const;
    /// 手順が指定する敵をすべて倒したか返す。
    bool AreTargetEnemiesDefeated(const Step& step) const;
    /// 誤った運搬操作からやり直し手順へ戻せたか返す。
    bool RecoverFromWrongCarriedAction(const Step& step);
    /// 指定操作の入力が発生したか返す。
    bool IsActionTriggered(const std::string& action) const;
    /// 最終手順を確定し、完了コールバックを呼ぶ。
    void CompleteFlow();

    // ステップ専用の短い演出を再生し、終了時に通常操作へ戻します。
    void BeginStepCinematic(const Step& step);
    void EndStepCinematic(bool skipped);
    void UpdateCinematic(float deltaTime);
    void UpdateGateStateForStep(const Step& step);
    void SetGateTransitionEnabled(bool enabled);
    void SetSpawnEnemyActive(const std::string& objectName, bool active);
    void SetSpawnEnemyDormant(const std::string& objectName, bool dormant);
    void SetStepSpawnEnemiesDormant(const Step& step, bool dormant);
    void SetAllSpawnEnemiesActive(bool active);
    void RestoreSpawnEnemyStateBeforeStep(int stepIndex);
    engine::Object3d* FindObjectByName(const std::string& name) const;

    // 操作案内と目標マーカーは JSON から取得した Sprite を更新します。
    void BindPromptSprites();
    void ApplyStepPrompt(const Step* step);
    void UpdatePrompt(float deltaTime);
    void SetPromptVisible(bool visible);
    void ApplyStepObjective(const Step* step);
    void UpdateObjectiveMarker(float deltaTime);
    void SetObjectiveMarkerVisible(bool visible);

  private:
    // シーン上のオブジェクトは所有せず、実体の寿命はシーンに従います。
    BaseScene* scene_ = nullptr;
    Player* player_ = nullptr;
    InputManager* inputManager_ = nullptr;
    GimmickStageGate* exitGate_ = nullptr;

    std::string flowPath_;
    std::string exitGateObjectName_ = "Gimmick_StageGate";
    std::string spawnEnemyObjectName_ = "Tutorial_PinkSlime";
    std::vector<std::string> spawnEnemyObjectNames_;
    std::unordered_map<std::string, float> spawnEnemyMaxHpOverrides_;
    std::unordered_map<std::string, float> spawnEnemyDetectionRangeOverrides_;
    // 読み込み済みの敵スロットと現在の手順。再生中の巻き戻しでも保持します。
    std::vector<SpawnEnemySlot> spawnEnemies_;
    std::vector<Step> steps_;
    int currentStepIndex_ = -1;
    float stepTimer_ = 0.0f;
    float conditionSatisfiedTimer_ = 0.0f;
    engine::Vector3 stepStartPosition_ = {0.0f, 0.0f, 0.0f};
    bool started_ = false;
    bool loaded_ = false;
    bool completed_ = false;
    bool cinematicActive_ = false;
    bool controlsGuideOpened_ = false;
    std::string lockedTargetName_;
    bool stepConditionSatisfied_ = false;
    bool morphWasActiveOnStepEntry_ = false;
    engine::Object3d* carriedEnemyOnStepEntry_ = nullptr;

    // ステップごとの演出再生と、操作案内・目標表示の独立した状態です。
    CinematicSequence cinematicSequence_;
    CinematicPlayer cinematicPlayer_;

    PromptVisual promptPanel_;
    PromptVisual promptLabel_;
    std::array<PromptVisual, 4> promptIcons_{};
    float promptAlpha_ = 0.0f;
    bool promptRequestedVisible_ = false;
    bool promptGamepadMode_ = false;
    std::string promptBindingSignature_;

    std::array<PromptVisual, 3> objectiveMarkers_{};
    std::array<std::string, 3> objectiveTargetNames_{};
    float objectiveOffsetY_ = 0.75f;
    std::array<float, 3> objectiveAlphas_{};
    float objectiveAnimationTime_ = 0.0f;
    bool objectiveRequestedVisible_ = false;

    CompletionCallback completionCallback_;
};
