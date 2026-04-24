#pragma once
#include "IEditable.h"
#include "TrailEmitter.h"
#include <vector>
#include <string>

class SceneManager;
class Object3d;
class BaseScene;

// ============================================================
//  TrailEmitterEditor
//   ImGuiでトレイルエミッターの設定を行うエディタ。
//   - シーン内オブジェクトリストから追従対象を選択
//   - メッシュエフェクト / GPUパーティクルプリセットの選択
//   - Emit距離・スケール・向き自動合わせ の設定
//   - 設定の保存・読み込み
//   - 「追従開始/停止」でリアルタイム追跡プレビュー
//   - サイン波往復ダミープレビュー (対象未選択時)
// ============================================================
class TrailEmitterEditor : public IEditable {
public:
    void Initialize(SceneManager* sceneManager);
    void Update(float deltaTime);
    void DrawImGui() override;
    std::string GetName() override { return "Trail Emitter Editor"; }

    void RefreshFileLists();

private:
    void Save(const std::string& name);
    void Load(const std::string& name);

    SceneManager* sceneManager_ = nullptr;
    BaseScene*    lastScene_    = nullptr;  // シーン切り替え検知用

    // エミッター本体
    TrailEmitter emitter_;

    // 追従対象 (シーンのオブジェクトリストから選択)
    Object3d* targetObject_ = nullptr;
    bool      isTracking_   = false;  // 追従中フラグ

    // 設定名バッファ
    char configNameBuf_[64] = "myTrail";

    // ダミープレビュー (サイン波)
    bool    isDummyRunning_  = false;
    float   previewTime_     = 0.0f;
    float   previewDuration_ = 2.0f;
    float   previewRange_    = 5.0f;
    Vector3 dummyPos_        = { 0.0f, 0.0f, 0.0f };
    Vector3 lastDummyPos_    = { 0.0f, 0.0f, 0.0f };
    bool    dummyFirstFrame_ = true;

    // プレイ状態監視 (切り替わり検知用)
    bool    wasPlaying_      = false;

    // ファイルリスト
    std::vector<std::string> meshEffectList_;
    std::vector<std::string> gpuParticleList_;
    std::vector<std::string> savedConfigList_;
};
