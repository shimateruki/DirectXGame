#pragma once

#include "engine/utility/math/Math.h"

#include <string>
#include <vector>

// Animation Workbenchで作成した単体Object用の見た目Animationを保持します。
// PositionはVisual Offsetとして保存し、物理座標へ適用するかは利用側が決定します。
class BodyAnimationClip {
public:
    static constexpr int kCurrentVersion = 2;

    /// <summary>指定時刻の移動、回転、拡縮、せん断と次キーへの補間方法。</summary>
    struct Key {
        float time = 0.0f;
        Vector3 translate = { 0.0f, 0.0f, 0.0f };
        Vector3 rotate = { 0.0f, 0.0f, 0.0f };
        Vector3 scale = { 1.0f, 1.0f, 1.0f };
        Vector3 shear = {};
        float shearPivotY = 0.0f;
        int easingToNext = 4;
    };

    /// <summary>Evaluateが計算した任意時刻の身体姿勢。</summary>
    struct Sample {
        Vector3 translate = { 0.0f, 0.0f, 0.0f };
        Vector3 rotate = { 0.0f, 0.0f, 0.0f };
        Vector3 scale = { 1.0f, 1.0f, 1.0f };
        Vector3 shear = {};
        float shearPivotY = 0.0f;
    };

    /// <summary>クリップのキーと再生時間を初期化する。</summary>
    void Clear();

    /// <summary>既存キーを消してから JSON を読む。開けないか不正な場合は false を返す。</summary>
    bool Load(const std::string& filePath);

    /// <summary>指定秒の姿勢を評価する。loop は末尾での折り返しを指定し、クリップの loopDefault は参照しない。</summary>
    /// <returns>有効なキーがなければ false。この場合 sampleOut は既定値に戻る。</returns>
    bool Evaluate(float timeSeconds, bool loop, Sample& sampleOut) const;

    bool IsValid() const { return !keys_.empty() && duration_ > 0.0f; }
    float GetDuration() const { return duration_; }
    bool IsLoopDefault() const { return loopDefault_; }
    const std::string& GetName() const { return name_; }
    const std::string& GetModelName() const { return modelName_; }
    const std::vector<Key>& GetKeys() const { return keys_; }

    // 名前だけなら現行ディレクトリを優先し、なければ旧ディレクトリを探す。
    // どちらにもない場合も現行側の候補パスを返し、存在確認は呼び出し側に任せる。
    static std::string ResolveAssetPath(const std::string& assetNameOrPath);

private:
    std::string name_;
    std::string modelName_;
    float duration_ = 0.0f;
    bool loopDefault_ = true;
    std::vector<Key> keys_;
};
