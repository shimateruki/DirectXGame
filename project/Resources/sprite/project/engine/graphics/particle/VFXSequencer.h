#pragma once
#include <string>
#include <vector>
#include "engine/utility/math/Math.h"
#include "Transform.h"
#include "Object3d.h"
enum class VFXEventType {
    GPUParticle = 0,
    MeshEffect = 1,
    SoundEffect = 2,
    MovingParticle = 3
};
// タイムライン上の1つの発火イベント
struct VFXEvent {
    VFXEventType type = VFXEventType::GPUParticle;

    std::string presetName; // 呼び出すパーティクルのプリセット名
    float triggerTime;      // Play開始から何秒後に発火するか
    Vector3 offset;         // 対象からの位置ズレ（足元、頭上など）
    bool hasFired = false;  // 実行済みフラグ（内部用）
    Vector3 controlPoint = { 0.0f, 5.0f, 0.0f }; // 中間点（カーブを引っ張る方向）
    Vector3 endOffset = { 0.0f, 0.0f, 10.0f };   // 終点
    float duration = 1.0f;                       // 移動にかける時間
    int easingType = 0;                          // イージングの種類
    bool isFinished = false; // 処理が完全に終わったか
};

// ==========================================================
// 複数のエフェクトを時間差で連続再生する最強の演出コンポーネント
// ==========================================================
class VFXSequencer {
public:
    VFXSequencer() = default;
    ~VFXSequencer() = default;
    void Initialize(Object3d* targetObject = nullptr);

    // 毎フレーム呼ぶ（内部でタイマーを進めてエフェクトを発火）
    void Update(float deltaTime);

    // 再生コントロール
    void Play();
    void Stop();
    void Reset();

    // 演出をタイムラインに追加する！
    void AddEvent(VFXEventType type, const std::string& presetName, float triggerTime, const Vector3& offset = { 0.0f, 0.0f, 0.0f });

    bool IsPlaying() const { return isPlaying_; }
    void Save(const std::string& sequenceName);
    void Load(const std::string& sequenceName);
    std::vector<VFXEvent>& GetEvents() { return events_; } // エディタから配列をいじる用
    void SetTargetObject(Object3d* targetObject) { targetObject_ = targetObject; }
    Object3d* GetTargetObject() const { return targetObject_; }
private:
    std::vector<VFXEvent> events_;
    Object3d* targetObject_ = nullptr;

    float currentTime_ = 0.0f;
    bool isPlaying_ = false;
};