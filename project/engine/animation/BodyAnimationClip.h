#pragma once

#include "engine/utility/math/Math.h"

#include <string>
#include <vector>

// 担当：単体Objectの見た目姿勢をキーフレームとして保持・補間する。
// 関連：AnimationClipEditor（編集）、Object3D（姿勢適用）、AnimatorControllerRuntime（状態再生）
// 主な入口：Evaluate / IsValid / GetDuration
// 移動値は見た目用の補正。物理座標へ適用するかは利用側が決める。
class BodyAnimationClip {
public:
    static constexpr int kCurrentVersion = 2;

    /// <summary>指定時刻の移動、回転、拡縮、せん断と次キーへの補間方法。</summary>
    // 関連：BodyAnimationClip（この型を使う機能の本体）
    struct Key {
        // timeは秒、rotateはラジアン、scaleは基準姿勢への倍率。せん断は見た目だけに適用する。
        float time = 0.0f;
        engine::Vector3 translate = engine::Vector3::ZERO;
        engine::Vector3 rotate = engine::Vector3::ZERO; // 見た目の回転（ラジアン）。
        engine::Vector3 scale = { 1.0f, 1.0f, 1.0f };
        engine::Vector3 shear = {}; // 見た目のせん断量。
        float shearPivotY = 0.0f; // せん断の基準となるローカルY位置。
        int easingToNext = 4; // 次キーまでの補間方式。
    };

    /// <summary>Evaluateが計算した任意時刻の身体姿勢。</summary>
    // 関連：BodyAnimationClip（この型を使う機能の本体）
    struct Sample {
        engine::Vector3 translate = engine::Vector3::ZERO;
        engine::Vector3 rotate = engine::Vector3::ZERO; // 見た目の回転（ラジアン）。
        engine::Vector3 scale = { 1.0f, 1.0f, 1.0f };
        engine::Vector3 shear = {}; // 見た目のせん断量。
        float shearPivotY = 0.0f; // せん断の基準となるローカルY位置。
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
    float duration_ = 0.0f; // クリップ全体の長さ（秒）。
    bool loopDefault_ = true; // 再生開始時の既定ループ指定。
    std::vector<Key> keys_; // 時刻順に評価する姿勢キー。
};
