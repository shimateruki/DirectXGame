#pragma once
#include <string>
#include <vector>
#include "engine/utility/math/Math.h"
#include "Transform.h"

// タイムライン上の1つの発火イベント
struct VFXEvent {
    std::string presetName; // 呼び出すパーティクルのプリセット名
    float triggerTime;      // Play開始から何秒後に発火するか
    Vector3 offset;         // 対象からの位置ズレ（足元、頭上など）
    bool hasFired = false;  // 実行済みフラグ（内部用）
};

// ==========================================================
// 複数のエフェクトを時間差で連続再生する最強の演出コンポーネント
// ==========================================================
class VFXSequencer {
public:
    VFXSequencer() = default;
    ~VFXSequencer() = default;

    // 初期化（追従する対象のTransformをセット）
    void Initialize(Transform* targetTransform = nullptr);

    // 毎フレーム呼ぶ（内部でタイマーを進めてエフェクトを発火）
    void Update(float deltaTime);

    // 再生コントロール
    void Play();
    void Stop();
    void Reset();

    // 演出をタイムラインに追加する！
    // 例: AddEvent("MagicCircle", 0.0f) -> 0秒後に魔法陣展開
    //     AddEvent("Explosion", 1.5f)   -> 1.5秒後に大爆発
    void AddEvent(const std::string& presetName, float triggerTime, const Vector3& offset = { 0.0f, 0.0f, 0.0f });

    bool IsPlaying() const { return isPlaying_; }
    void Save(const std::string& sequenceName);
    void Load(const std::string& sequenceName);
    std::vector<VFXEvent>& GetEvents() { return events_; } // エディタから配列をいじる用
private:
    std::vector<VFXEvent> events_;
    Transform* targetTransform_ = nullptr;

    float currentTime_ = 0.0f;
    bool isPlaying_ = false;
};