#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Trigger は成立した遷移で消費される、一度限りの条件です。
enum class AnimatorParameterType {
    Float,
    Int,
    Bool,
    Trigger
};

// Greater/Less は Float、Equals/NotEqual は Int、If/IfNot は Bool を参照します。
enum class AnimatorConditionMode {
    Greater,
    Less,
    Equals,
    NotEqual,
    If,
    IfNot,
    Trigger
};

/// <summary>
/// 遷移判定に使うパラメータの宣言と型別の初期値です。
/// type に対応する初期値だけを使用し、Trigger は false から開始します。
/// </summary>
struct AnimatorParameterDefinition {
    std::string name;
    AnimatorParameterType type = AnimatorParameterType::Float;
    float defaultFloat = 0.0f;
    int defaultInt = 0;
    bool defaultBool = false;
};

/// <summary>
/// 状態の再生中に発火するイベントの定義です。
/// normalizedTime は再生開始を 0、末尾を 1 とした発火位置です。
/// </summary>
struct AnimatorEventDefinition {
    std::string name = "FeedbackCue";
    std::string payload;
    float normalizedTime = 0.0f;
};

/// <summary>
/// 1 つの再生状態で使用するクリップ、再生設定、発火イベントをまとめます。
/// </summary>
struct AnimatorStateDefinition {
    std::string name;
    std::string clipName;     // Model 内の Skeletal Animation 名。
    std::string bodyClipName; // Animation Workbench で作成した Body Transform Clip Asset 名。

    float speed = 1.0f;
    bool loop = true;
    float blendDuration = 0.12f;
    int blendEasing = 4;
    float eventTimelineDuration = 1.0f; // クリップ長が不明なときのイベント・exitTime 判定用時間（秒）。

    std::vector<AnimatorEventDefinition> events;
};

/// <summary>
/// Runtime が発火し、呼び出し側へ渡すイベントです。
/// stateName は発火元の状態名です。
/// </summary>
struct AnimatorEventInstance {
    std::string stateName;
    std::string name;
    std::string payload;
    float normalizedTime = 0.0f;
};

/// <summary>
/// 遷移条件で参照するパラメータと比較方法です。
/// threshold は Float の大小比較、または Int の等価比較に使用します。
/// </summary>
struct AnimatorConditionDefinition {
    std::string parameter;
    AnimatorConditionMode mode = AnimatorConditionMode::Greater;
    float threshold = 0.0f;
};

/// <summary>
/// 状態間の遷移と、その成立条件を定義します。
/// fromState が空文字または "*" なら Any State、conditions はすべて満たす必要があります。
/// </summary>
struct AnimatorTransitionDefinition {
    std::string fromState;
    std::string toState;
    float duration = 0.12f;
    int easing = 4;
    bool hasExitTime = false;
    float exitTime = 0.9f; // hasExitTime が true のときに待つ正規化時刻。
    std::vector<AnimatorConditionDefinition> conditions;
};

/// <summary>
/// エディターで編集し、JSON に保存するアニメーション状態機械の定義です。
/// 再生時刻や現在のパラメータ値は保持しません。
/// </summary>
class AnimatorControllerAsset {
public:
    static constexpr int kCurrentVersion = 2;

    /// <summary>アセット定義を既定値へ戻し、状態・遷移・パラメータを空にする。</summary>
    void Clear();

    /// <summary>
    /// 名前が一致する状態の配列位置を探します。
    /// </summary>
    /// <param name="stateName">探す状態名。</param>
    /// <returns>見つかった位置。存在しない場合は -1。</returns>
    int FindStateIndex(const std::string& stateName) const;

    /// <summary>
    /// 名前が一致する状態の定義を探します。
    /// </summary>
    /// <param name="stateName">探す状態名。</param>
    /// <returns>見つかった定義。存在しない場合は nullptr。</returns>
    const AnimatorStateDefinition* FindState(const std::string& stateName) const;

    /// <summary>
    /// JSON から状態・遷移・パラメータの定義を読み込みます。
    /// </summary>
    /// <param name="filePath">読み込む JSON ファイルのパス。</param>
    /// <returns>読み込みに成功した場合は true。解析に失敗した場合は定義を初期化して false。</returns>
    bool Load(const std::string& filePath);

    /// <summary>
    /// 現在の定義を JSON に保存します。必要なら親フォルダーを作成します。
    /// </summary>
    /// <param name="filePath">保存先の JSON ファイルのパス。</param>
    /// <returns>ファイルへの書き込みに成功した場合は true。</returns>
    bool Save(const std::string& filePath) const;

    int version = kCurrentVersion;
    std::string name = "animator_controller";
    std::string entryState; // 初期状態名。未指定・不明な名前なら先頭の状態を採用します。
    std::vector<AnimatorParameterDefinition> parameters;
    std::vector<AnimatorStateDefinition> states;
    std::vector<AnimatorTransitionDefinition> transitions;
};

/// <summary>
/// 再生対象ごとの時刻、遷移、パラメータ、イベントを管理します。
/// Asset は共有参照し、所有しません。
/// </summary>
class AnimatorControllerRuntime {
public:
    // クリップ長を秒で返します。0 以下は長さ不明として扱います。
    using DurationResolver = std::function<float(const AnimatorStateDefinition& state)>;

    /// <summary>
    /// リプレイ等で保存する再生位置です。
    /// 時刻は秒単位で、パラメータ値と未消費イベントは含みません。
    /// </summary>
    struct Snapshot {
        std::string currentState;
        std::string previousState;
        float currentTime = 0.0f;
        float previousTime = 0.0f;
        float transitionElapsed = 0.0f;
        float transitionDuration = 0.0f;
        int transitionEasing = 4;
        bool valid = false; // 再生中の状態があり、復元できる場合に true。
    };

    /// <summary>
    /// 共有する定義を設定し、この Runtime の再生状態を初期化します。
    /// </summary>
    /// <param name="controller">共有する定義。所有しないため Runtime より長く生存させてください。</param>
    /// <param name="playEntryState">true なら初期状態を直ちに再生します。</param>
    void SetController(const AnimatorControllerAsset* controller, bool playEntryState = true);

    /// <summary>設定中の共有定義を返します。未設定なら nullptr です。</summary>
    const AnimatorControllerAsset* GetController() const { return controller_; }

    /// <summary>
    /// 設定済みの定義を保ち、再生時刻・遷移・イベント・パラメータを初期化します。
    /// </summary>
    /// <param name="playEntryState">true なら初期状態を再生します。</param>
    void Reset(bool playEntryState = true);

    /// <summary>
    /// 再生時刻を進め、自動遷移とイベント発火を処理します。
    /// </summary>
    /// <param name="deltaTime">前回更新からの経過秒数。0 以下なら更新しません。</param>
    /// <param name="durationResolver">状態の実クリップ長を秒で返す関数。長さ不明ならイベントに代替時間を使います。</param>
    void Update(float deltaTime, const DurationResolver& durationResolver);

    /// <summary>
    /// 指定状態へ即時に切り替え、進行中のブレンドを終了します。
    /// </summary>
    /// <param name="stateName">再生する状態の名前。</param>
    /// <param name="timeSeconds">開始時刻（秒）。負値は 0 として扱います。</param>
    /// <returns>指定状態が存在した場合は true。</returns>
    bool Play(const std::string& stateName, float timeSeconds = 0.0f);

    /// <summary>
    /// 指定状態へブレンドします。遷移元もブレンド完了まで再生します。
    /// </summary>
    /// <param name="stateName">遷移先の状態名。</param>
    /// <param name="durationOverride">遷移時間（秒）。負値なら遷移定義、次に遷移先状態の既定値を使います。</param>
    /// <param name="easingOverride">補間方法。負値なら遷移定義、次に遷移先状態の既定値を使います。</param>
    /// <returns>指定状態が存在した場合は true。</returns>
    bool CrossFade(const std::string& stateName, float durationOverride = -1.0f, int easingOverride = -1);

    /// <summary>指定名の状態を現在再生しているかを返します。</summary>
    bool IsPlayingState(const std::string& stateName) const;

    /// <summary>遷移元とのブレンドが進行中かを返します。</summary>
    bool IsTransitioning() const { return previousStateIndex_ >= 0 && transitionDuration_ > 0.0f; }

    /// <summary>現在の状態を返します。再生していなければ nullptr です。</summary>
    const AnimatorStateDefinition* GetCurrentState() const;

    /// <summary>ブレンド中の遷移元を返します。遷移していなければ nullptr です。</summary>
    const AnimatorStateDefinition* GetPreviousState() const;

    /// <summary>現在の状態の再生時刻を秒で返します。</summary>
    float GetStateTime() const { return currentTime_; }

    /// <summary>遷移元の再生時刻を秒で返します。</summary>
    float GetPreviousTime() const { return previousTime_; }

    /// <summary>
    /// 現在のブレンド率を求めます。遷移していないときは 1 を返します。
    /// </summary>
    /// <returns>遷移先状態の補間率（0～1）。</returns>
    float GetTransitionWeight() const;

    /// <summary>
    /// 現在の再生時刻を移動し、未消費イベントを破棄します。
    /// 飛ばした区間のイベントは発火しません。
    /// </summary>
    /// <param name="timeSeconds">移動先の時刻（秒）。負値は 0 として扱います。</param>
    void SetCurrentTime(float timeSeconds);

    /// <summary>
    /// 発火済みイベントを取り出し、内部キューを空にします。
    /// </summary>
    /// <returns>前回の取り出し以降に発火したイベント。</returns>
    std::vector<AnimatorEventInstance> ConsumeEvents();

    /// <summary>Float パラメータの現在値を設定します。</summary>
    void SetFloat(const std::string& name, float value);

    /// <summary>Int パラメータの現在値を設定します。</summary>
    void SetInt(const std::string& name, int value);

    /// <summary>Bool パラメータの現在値を設定します。</summary>
    void SetBool(const std::string& name, bool value);

    /// <summary>
    /// 一度限りの遷移条件を立てます。成立した遷移で使用された場合は解除されます。
    /// </summary>
    /// <param name="name">Trigger パラメータの名前。</param>
    void SetTrigger(const std::string& name);

    /// <summary>指定した Trigger を解除します。</summary>
    void ResetTrigger(const std::string& name);

    /// <summary>Float パラメータを返します。未設定なら 0 です。</summary>
    float GetFloat(const std::string& name) const;

    /// <summary>Int パラメータを返します。未設定なら 0 です。</summary>
    int GetInt(const std::string& name) const;

    /// <summary>Bool パラメータを返します。未設定なら false です。</summary>
    bool GetBool(const std::string& name) const;

    /// <summary>指定した Trigger が立っているかを返します。</summary>
    bool IsTriggerSet(const std::string& name) const;

    /// <summary>
    /// 再生位置とブレンド状態を保存します。パラメータ値と未消費イベントは含みません。
    /// </summary>
    Snapshot CaptureSnapshot() const;

    /// <summary>
    /// 再生位置を復元します。パラメータ値は維持し、未消費イベントは破棄します。
    /// </summary>
    /// <param name="snapshot">復元する再生位置。無効な場合は何もしません。</param>
    void RestoreSnapshot(const Snapshot& snapshot);

private:
    // Asset の宣言から型別の現在値を作り直し、Trigger を解除します。
    void InitializeParameters();

    // time を speed と deltaTime で進め、長さが分かる状態ではループまたは末尾で制限します。
    void AdvanceStateTime(
        float& time,
        const AnimatorStateDefinition* state,
        float deltaTime,
        const DurationResolver& durationResolver) const;

    // 折り返し前の時刻を再生割合に変換します。クリップ長不明時は代替時間軸を使います。
    float GetNormalizedTime(
        const AnimatorStateDefinition* state,
        float time,
        const DurationResolver& durationResolver) const;

    // コールバックからクリップ長（秒）を得て、無効な長さは 0 にします。
    float ResolveStateDuration(
        const AnimatorStateDefinition* state,
        const DurationResolver& durationResolver) const;

    // 状態へ入った瞬間に発火する normalizedTime = 0 のイベントをキューへ積みます。
    void QueueEntryEvents(const AnimatorStateDefinition* state);

    // previousTime からループ処理前の rawNewTime までに通過したイベントを集めます。
    void CollectCrossedEvents(
        const AnimatorStateDefinition* state,
        float previousTime,
        float rawNewTime,
        float duration);

    // 明示的な CrossFade 用に、状態固有の設定を優先して遷移定義を探します。
    const AnimatorTransitionDefinition* FindRequestedTransition(int fromIndex, int toIndex) const;

    // exitTime と条件を満たす自動遷移を、状態固有、Any State の順に探します。
    const AnimatorTransitionDefinition* FindAutomaticTransition(float normalizedTime) const;

    // 条件をすべて評価します。条件が空なら exitTime を持つ遷移のみ許可します。
    bool AreConditionsMet(const AnimatorTransitionDefinition& transition) const;

    // 成立した遷移の条件に使われた Trigger を解除します。
    void ConsumeTriggers(const AnimatorTransitionDefinition& transition);

    const AnimatorControllerAsset* controller_ = nullptr; // 共有定義への非所有参照。

    int currentStateIndex_ = -1;  // Asset::states 内の現在位置。-1 は未再生。
    int previousStateIndex_ = -1; // Asset::states 内のブレンド元位置。-1 は遷移なし。

    // 再生位置とブレンド経過は秒単位です。
    float currentTime_ = 0.0f;
    float previousTime_ = 0.0f;
    float transitionElapsed_ = 0.0f;
    float transitionDuration_ = 0.0f;
    int transitionEasing_ = 4; // AnimationInterpolation::EasingType の番号。

    // パラメータは型別に保持し、Asset の初期値から Runtime ごとに更新します。
    std::unordered_map<std::string, float> floatParameters_;
    std::unordered_map<std::string, int> intParameters_;
    std::unordered_map<std::string, bool> boolParameters_;
    std::unordered_map<std::string, bool> triggerParameters_;

    std::vector<AnimatorEventInstance> pendingEvents_; // ConsumeEvents まで保持する発火済みイベント。
};
