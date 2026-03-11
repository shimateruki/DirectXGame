#pragma once
#include <string>
#include <vector>
#include "Object3d.h"
#include "SceneManager.h"
#include "IEditable.h"

struct ActiveEvent {
    int id = 0;                        // イベントの番号
    Object3d* targetObject = nullptr;  // イベントを起こしたパーツ（キューブ）
};
// 複数のオブジェクト(役者)にパスデータ(演技)を割り当てて一斉再生する監督クラス
class GhostDirector :public IEditable {
public:
    // 1つのオブジェクトに割り当てるパスデータの情報
    struct Track {
        Object3d* target = nullptr;     // 動かす対象のポインタ
        std::string targetName;         // ターゲットの名前
        std::string pathFileName;       // 再生するパスデータ(.json)
        float delayTime = 0.0f;  // 再生開始までの待機時間（ディレイ）
        bool hasStarted = false; // 再生開始済みフラグ（内部用）
    };

    void Initialize(SceneManager* sceneManager);
    void Update(float deltaTime);
    void DrawImGui() override;
    std::string GetName() override { return "GhostDirector(Cinematic/Path)"; }
    void PlayScenario(bool isLoop = false, bool useImguiTime = false);
    void StopScenario();
    void DrawPreview(const Matrix4x4& viewProjection, const Vector2& offset, const Vector2& size);

    void SaveScenario(const std::string& fileName);
    void LoadScenario(const std::string& fileName);
    bool IsFinished() const;
    int GetActiveEventID() const;
    ActiveEvent GetActiveEvent() const;

    // 時間を進める共通処理
    void AdvanceTime(float deltaTime);


private:
    SceneManager* sceneManager_ = nullptr;
    std::vector<Track> tracks_;
    char scenarioNameBuf_[64] = "boss_attack_1";

    // 再生状態管理用
    bool isPlaying_ = false;
    float currentScrubTime_ = 0.0f;
    float playTimer_ = 0.0f;
    bool isLooping_ = false;    // シナリオ全体をループさせるか
    bool useImguiTime_ = false; // ImGuiのDeltaTimeを使うか
};