#pragma once
#include "BaseBossAttack.h"
#include "engine/utility/math/Math.h"
#include <vector>

class Object3d;

class BossAttack8_Final : public BaseBossAttack {
private:
    Vector3 animStartPos_ = { 0,0,0 };
    Vector3 animTargetPos_ = { 0,0,0 };

    // フェーズ80・85（巨大メテオ）用
    std::vector<Object3d*> meteors_;
    std::vector<Object3d*> areaWarnings_;
    Object3d* dropWarningLine_ = nullptr;
    float rainTimer_ = 0.0f;
    int rainCount_ = 0;

    // フェーズ81（突進）用
    int rushCount_ = 0;
    std::vector<Vector3> blockStartPos_;
    std::vector<Vector3> blockTargetPos_;
    std::vector<Vector3> blockStartScale_;

    // フェーズ82（ボム＆ファンネル）用
    std::vector<Object3d*> activeLasers_;
    std::vector<Object3d*> activeCoreLasers_;
    std::vector<int> funnelStates_;
    std::vector<float> funnelTimers_;
    std::vector<float> laserLengths_;
    std::vector<float> laserDelayTimers_;
    int spawnCount_ = 0;
    float spawnTimer_ = 0.0f;

    // フェーズ83（壁＆4分割ビーム）用
    int wallStep_ = 0;
    std::vector<Object3d*> coreBeams_;
    std::vector<Object3d*> coreBeamCores_;

public:
    ~BossAttack8_Final();

    void Initialize(BossCore* boss) override;
    void Update(BossCore* boss, float deltaTime) override;
    void Finalize() override; // クリーンアップ用
};