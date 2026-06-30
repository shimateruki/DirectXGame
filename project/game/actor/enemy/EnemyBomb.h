#pragma once
#include "BaseEnemy.h"

// 霑代▼縺上→轤ｹ轣ｫ縺励∵戟縺｡驕九・繧・兜謫ｲ縺ｧ繧ゅき繧ｦ繝ｳ繝医ム繧ｦ繝ｳ辷・匱縺吶ｋ辷・ｼｾ謨ｵ
class EnemyBomb : public BaseEnemy {
public:
    enum class State {
        Chase,      // 繝励Ξ繧､繝､繝ｼ霑ｽ霍｡
        Ignited,    // 轤ｹ轣ｫ繧ｫ繧ｦ繝ｳ繝医ム繧ｦ繝ｳ
        Exploded    // 辷・匱螳御ｺ・
    };

    EnemyBomb() = default;
    ~EnemyBomb() override = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;
    
    void SetCarried(bool isCarried) override;
    void ExecuteAbility(class Player* player) override;
    void Ignite(float fuseTime = 3.0f);

private:
    void UpdateChase(float deltaTime);
    void UpdateIgnited(float deltaTime);
    void Explode();
    void ResolveSweptCollision(const Vector3& previousPosition);

private:
    State state_ = State::Chase;

    // 繧ｫ繧ｦ繝ｳ繝医ム繧ｦ繝ｳ髢｢騾｣
    float fuseTimer_ = 2.0f;       // 辷・匱縺ｾ縺ｧ縺ｮ谿九ｊ譎る俣・・遘抵ｼ・
    float flashTimer_ = 0.0f;      // 襍､濶ｲ轤ｹ貊・畑繧ｿ繧､繝槭・
    bool flashState_ = false;       // 轤ｹ貊・憾諷九・繧ｪ繝ｳ/繧ｪ繝・
    float pulseTimer_ = 0.0f;      // 繝峨け繝ｳ繝峨け繝ｳ縺ｨ髴・∴繧倶ｼｸ邵ｮ繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ逕ｨ繧ｿ繧､繝槭・

    bool isThrown_ = false;        // 繝励Ξ繧､繝､繝ｼ縺ｫ繧医▲縺ｦ謚輔￡繧峨ｌ縺溘°
    bool isAbilityExecuted_ = false; // ExecuteAbility・郁・辷・・蜉幢ｼ峨′逋ｺ蜍輔＆繧後◆縺・
};

