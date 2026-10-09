#pragma once
#include "game/actor/combat/AttackSequence.h"
#include "BaseEnemy.h"

// AIは候補を選んで開始するだけ。予備動作から後隙まで同じ時計で進めます。
// ownerはこの部品を所有するActorで、Cancelまたは部品の破棄まで生存する必要があります。
class EnemyAttackAction : public AttackSequence {
public:
    ~EnemyAttackAction() { Cancel(); }
    bool Start(const EnemyAttackDefinition& definition, const AttackSequenceContext& context) {
        if (IsRunning()) return false;
        Restore();
        owner_ = dynamic_cast<BaseEnemy*>(context.owner);
        if (definition.sequence.actorControl.enabled && !owner_) return false;
        if (owner_) originalColor_ = owner_->GetColor();
        aim_ = context;
        aim_.owner = nullptr;
        cuePlayed_ = false;
        controlled_ = definition.sequence.actorControl.enabled;
        if (!AttackSequence::Start(definition, context)) { Restore(); return false; }
        ApplyPresentation(context);
        return true;
    }
    void Update(float dt, const AttackSequenceContext& context) {
        if (!IsRunning() || !std::isfinite(dt) || dt < 0) return;
        const auto& control = GetDefinition().sequence.actorControl;
        if (controlled_ && context.hasTarget && GetElapsed() < control.aimLockTime) {
            aim_ = context;
            aim_.owner = nullptr;
        }
        auto execution = context;
        if (controlled_) {
            execution.forward = aim_.forward;
            execution.targetPosition = aim_.targetPosition;
            execution.targetDistance = aim_.targetDistance;
            // 対象消失時の条件判定は現在の情報を使い、固定座標で命令を発行します。
            ApplyPresentation(context);
            if (!cuePlayed_ && GetElapsed() + dt >= control.cueTime) {
                cuePlayed_ = true;
                if (control.telegraph && owner_) owner_->TriggerAttackTelegraphCue(ToColor(control.telegraphColor));
            }
        }
        AttackSequence::Update(dt, execution);
        if (!IsRunning()) Restore();
        else ApplyPresentation(context);
    }
    void Cancel() { AttackSequence::Cancel(); Restore(); }
    bool ControlsActor() const { return controlled_ && IsRunning(); }
private:
    static Vector4 ToColor(const std::array<float, 4>& c) { return {c[0], c[1], c[2], c[3]}; }
    void ApplyPresentation(const AttackSequenceContext& context) {
        if (!controlled_ || !owner_ || !IsRunning()) return;
        const auto& c = GetDefinition().sequence.actorControl;
        if (c.stopMovement) {
            auto v = owner_->GetVelocity(); v.x = v.z = 0; owner_->SetVelocity(v);
        }
        if (c.faceTarget) owner_->SetRotationY(std::atan2(aim_.forward.x, aim_.forward.z) + c.modelYawOffset * 0.01745329252f);
        if (c.tint) owner_->SetColor(GetElapsed() < c.telegraphEnd ? ToColor(c.color) : originalColor_);
        if (c.telegraph && GetElapsed() < c.telegraphEnd) {
            const auto& d = GetDefinition();
            owner_->ShowAttackTelegraphLine(context.position, aim_.forward,
                std::clamp(aim_.targetDistance, d.minRange, d.maxRange), c.telegraphWidth,
                GetElapsed() / (std::max)(0.001f, c.telegraphEnd), ToColor(c.telegraphColor));
        } else owner_->HideAttackTelegraph();
    }
    void Restore() {
        if (controlled_ && owner_) { owner_->SetColor(originalColor_); owner_->HideAttackTelegraph(); }
        owner_ = nullptr;
        controlled_ = false;
    }
    BaseEnemy* owner_ = nullptr;
    Vector4 originalColor_{1, 1, 1, 1};
    AttackSequenceContext aim_;
    bool controlled_ = false, cuePlayed_ = false;
};
