#pragma once
#include <string>
#include <vector>
#include "engine/utility/math/Math.h"
#include "Transform.h"
#include "Object3d.h"

/// <summary>タイムラインから起動できる演出の種類です。</summary>
enum class VFXEventType {
    GPUParticle = 0,
    MeshEffect = 1,
    SoundEffect = 2,
    MovingParticle = 3,
    CameraShake = 4,
    PostEffectPulse = 5,
    LightPulse = 6,
    HitStop = 7,
    ControllerRumble = 8,
    CameraFovPulse = 9
};

/// <summary>発火時刻と種類別パラメータをまとめたタイムラインイベントです。</summary>
struct VFXEvent {
    // 以下の hasFired/isFinished/applied* は再生時の状態で、Asset 設定値とは分けて扱います。
    VFXEventType type = VFXEventType::GPUParticle;
    std::string presetName;
    float triggerTime = 0.0f;
    engine::Vector3 offset = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 rotation = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 scale = { 1.0f, 1.0f, 1.0f };
    bool hasFired = false;
    engine::Vector3 controlPoint = { 0.0f, 5.0f, 0.0f };
    engine::Vector3 endOffset = { 0.0f, 0.0f, 10.0f };
    float duration = 1.0f;
    int easingType = 0;
    bool isFinished = false;

    float intensity = 0.2f;
    float secondaryIntensity = 0.7f;
    float frequency = 24.0f;
    float attackRatio = 0.12f;
    float radialIntensity = 0.0f;
    float damageFlash = 0.0f;
    float chromaticAberration = 0.0f;
    float wobbleIntensity = 0.0f;
    float bloomIntensity = 0.0f;
    float lightRadius = 9.0f;
    float lightDecay = 1.4f;
    engine::Vector4 lightColor = { 1.0f, 0.58f, 0.12f, 1.0f };

    bool hasAppliedPostPulse = false;
    float appliedRadialIntensity = 0.0f;
    float appliedDamageFlash = 0.0f;
    float appliedChromaticAberration = 0.0f;
    float appliedWobbleIntensity = 0.0f;
    float appliedBloomIntensity = 0.0f;
};

/// <summary>時刻に合わせて VFX イベントを発火し、OneShot 演出も管理します。</summary>
class VFXSequencer {
public:
    VFXSequencer() = default;
    ~VFXSequencer() = default;

    /// <summary>対象オブジェクトを設定し、シーケンス再生の準備をします。</summary>
    void Initialize(engine::Object3d* targetObject = nullptr);

    /// <summary>再生時間を進め、通過したイベントを発火します。</summary>
    void Update(float deltaTime);

    /// <summary>現在のイベント列の再生を開始します。</summary>
    void Play();

    /// <summary>再生を停止します。</summary>
    void Stop();

    /// <summary>再生位置とイベント発火状態を初期化します。</summary>
    void Reset();

    /// <summary>指定時刻に発火するイベントをタイムラインへ追加します。</summary>
    /// <param name="type">発火する演出の種類。</param>
    /// <param name="presetName">参照するプリセットまたはアセット名。</param>
    /// <param name="triggerTime">再生開始からの秒数。</param>
    /// <param name="offset">基準位置からの移動量。</param>
    /// <param name="rotation">追加する回転。</param>
    /// <param name="scale">追加する拡大率。</param>
    void AddEvent(
        VFXEventType type,
        const std::string& presetName,
        float triggerTime,
        const engine::Vector3& offset = { 0.0f, 0.0f, 0.0f },
        const engine::Vector3& rotation = { 0.0f, 0.0f, 0.0f },
        const engine::Vector3& scale = { 1.0f, 1.0f, 1.0f });

    /// <summary>現在再生中か返します。</summary>
    bool IsPlaying() const { return isPlaying_; }

    /// <summary>現在の再生位置を秒単位で返します。</summary>
    float GetCurrentTime() const { return currentTime_; }

    /// <summary>イベント列から計算したシーケンスの長さを返します。</summary>
    float GetDuration() const;

    /// <summary>シーケンスを指定名で保存します。</summary>
    void Save(const std::string& sequenceName);

    /// <summary>指定名のシーケンスを読み込みます。</summary>
    void Load(const std::string& sequenceName);

    /// <summary>編集可能なイベント列を返します。</summary>
    std::vector<VFXEvent>& GetEvents() { return events_; }

    /// <summary>イベント列を読み取り専用で返します。</summary>
    const std::vector<VFXEvent>& GetEvents() const { return events_; }

    /// <summary>イベントの追従対象を設定します。</summary>
    void SetTargetObject(engine::Object3d* targetObject) { targetObject_ = targetObject; }

    /// <summary>現在の追従対象を返します。</summary>
    engine::Object3d* GetTargetObject() const { return targetObject_; }

    /// <summary>対象を持たない演出の基準位置を設定します。</summary>
    void SetRootPosition(const engine::Vector3& rootPosition);

    /// <summary>基準位置に適用する拡大率を設定します。</summary>
    void SetRootScale(const engine::Vector3& rootScale);

    /// <summary>基準位置に適用する回転を設定します。</summary>
    void SetRootRotation(const engine::Vector3& rootRotation);

    /// <summary>明示的な基準位置を解除します。</summary>
    void ClearRootPosition();

    /// <summary>位置を指定して一度だけシーケンスを再生します。</summary>
    static void PlayOneShot(const std::string& sequenceName, const engine::Vector3& position);

    /// <summary>位置と拡大率を指定して一度だけ再生します。</summary>
    static void PlayOneShot(const std::string& sequenceName, const engine::Vector3& position, const engine::Vector3& scale);

    /// <summary>位置、拡大率、回転を指定して一度だけ再生します。</summary>
    static void PlayOneShot(const std::string& sequenceName, const engine::Vector3& position, const engine::Vector3& scale, const engine::Vector3& rotation);

    /// <summary>オブジェクトへ追従する一度きりの演出を再生します。</summary>
    static void PlayOneShotOnTarget(const std::string& sequenceName, engine::Object3d* targetObject);

    /// <summary>対象のローカル変換を指定して一度きりの演出を再生します。</summary>
    static void PlayOneShotOnTarget(const std::string& sequenceName, engine::Object3d* targetObject, const engine::Vector3& localOffset, const engine::Vector3& scale, const engine::Vector3& rotation);

    /// <summary>全 OneShot の再生時間を進めます。</summary>
    static void UpdateOneShots(float deltaTime);

    /// <summary>全 OneShot を停止して破棄します。</summary>
    static void ClearOneShots();

    /// <summary>指定時間のヒットストップを要求します。</summary>
    static void RequestHitStop(float duration, float timeScale = 0.0f);

    /// <summary>ヒットストップやフィードバックの経過時間を更新します。</summary>
    static void UpdateFeedbackRuntime(float unscaledDeltaTime);

    /// <summary>ヒットストップを反映したゲーム時間倍率を返します。</summary>
    static float GetGameplayTimeScale();

private:
    // イベントの定義・再生状態と、対象または明示位置に適用するルート変換です。
    std::vector<VFXEvent> events_;
    engine::Object3d* targetObject_ = nullptr;
    engine::Vector3 rootPosition_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 rootScale_ = { 1.0f, 1.0f, 1.0f };
    engine::Vector3 rootRotation_ = { 0.0f, 0.0f, 0.0f };

    float currentTime_ = 0.0f;
    bool isPlaying_ = false;
    bool useRootPosition_ = false;
    std::string sequenceName_;
};
