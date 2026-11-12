#pragma once
#include "CopyAbilitySupport.h"
#include "CrownLanceSettings.h"
#include "CrownAbilityEffects.h"
#include <array>

// 右クリック回避の経路・二枚のゲート・体の断面を、コピーセッションと同じ寿命で管理します。
// 関連：王冠能力が出口と移動対象を渡し、通過中の座標・ディゾルブ状態を復元する。
// 主な入口：Begin / Update / End
class CrownGateWarp {
    using Vec = engine::Vector3;
    using Portal = std::unique_ptr<engine::Object3D>;
    static constexpr float kDuration = 0.30f;
    static constexpr float kTransit = 0.14f;
    static constexpr float kMinTravel = 1.05f;
    // 回避経路の衝突照会に使う、ワールドAABBの中心・半寸法と探索半径。
    struct Body {
        Vec center, half;
        float radius;
    };
    Portal entrance_, exit_;
    CrownAbilityEffects effects_;
    CrownPresentationSettings presentation_;
    Vec direction_{}, start_{}, destination_{};
    float age_ = 0.0f, cooldown_ = 0.0f; // 回避開始からの経過秒数と、再使用までの残り秒数。
    float halfSpan_ = 1.0f, gateExtent_ = 1.0f, entranceOffset_ = 0.0f;
    bool crossed_ = false, needsFloor_ = false, restoreControl_ = false; // 出口への移行済み・接地保持が必要・開始前の操作許可を表す。

    // Playerのワールド境界から経路照会用の中心・半寸法・半径を求める。
    static Body Measure(Player& player) {
        const auto bounds = player.GetAabb();
        Vec center = (bounds.min + bounds.max) * 0.5f;
        Vec half = (bounds.max - bounds.min) * 0.5f;
        if (player.GetColliderType() == ColliderType::Sphere) {
            const float radius = player.GetCollider()->GetRadius();
            half = {radius, radius, radius};
        }
        // 横に広いスライムでも検索形状が足元より下へ膨らまないよう、三軸を別々に扱います。
        half = {(std::max)(0.05f, half.x - 0.02f), (std::max)(0.05f, half.y - 0.025f),
                (std::max)(0.05f, half.z - 0.02f)};
        return {center, half, (std::min)({half.x, half.y, half.z})};
    }

    // 基本距離で中心を越えても出口が偽王の体内なら、反対側の体表までを候補にします。
    // 実際に採用する距離は、この後で全区間の壁と足場を検査してから決めます。
    static float BeyondCrownArmor(Player& player, const Body& body, Vec dir, float distance) {
        PhysicsQueryFilter enemies;
        enemies.mask = CollisionCategories::Get("enemy");
        enemies.ignoredObject = &player;
        const Vec end = body.center + dir * distance;
        for (const auto& hit : CollisionManager::GetInstance()->OverlapAabb({end-body.half,end+body.half}, enemies)) {
            auto* king = dynamic_cast<EnemyFalseKingSlime*>(CopyAbilityDetail::FindEnemyRoot(hit.object));
            if (!king || king->GetBattlePhase() != 3 || king->IsCrownInterlude()) continue;
            const auto bounds = king->GetAabb();
            const auto center = (bounds.max + bounds.min) * 0.5f;
            const float along = engine::Math::Dot(center - body.center, dir);
            if (along <= 0.0f || along > distance) continue;
            const auto extent = (bounds.max - bounds.min) * 0.5f + body.half;
            // 軸平行境界のうち進行方向に最初に抜ける面まで求めます。
            float extra = distance;
            for (auto [offset, half, speed] : {std::array{end.x-center.x,extent.x,dir.x},
                                              std::array{end.z-center.z,extent.z,dir.z}}) {
                if (std::abs(speed) > 0.0001f)
                    extra = (std::min)(extra, (half - std::copysign(1.0f,speed) * offset) / std::abs(speed));
            }
            return distance + (std::max)(0.0f, extra) + 0.05f;
        }
        return distance;
    }

    // 地形は全経路、敵は出口だけを塞ぐ。地上からの回避では足場のある出口を選びます。
    // 候補経路の遮蔽物を調べ、身体を通せる距離へ短縮する。
    static float SafeDistance(Player& player, const Body& body, Vec dir, float wanted, bool needsFloor) {
        auto* world = CollisionManager::GetInstance();
        PhysicsQueryFilter terrain;
        terrain.ignoredObject = &player;
        terrain.mask = CollisionCategories::Get("ground");
        // 各区間全体を覆う箱で薄い壁も拾い、最後に通れた位置を出口候補にします。
        // 開けた場所は一回の問い合わせで完了します。遮蔽物のある区間だけ細分化します。
        auto blocked = [&](float from, float to) {
            const Vec mid = body.center + dir * ((from + to) * 0.5f);
            const float span = (to - from) * 0.5f;
            const Vec half = body.half + Vec{std::abs(dir.x) * span,0,std::abs(dir.z) * span};
            return !world->OverlapAabb({mid - half, mid + half}, terrain).empty();
        };
        if (wanted < kMinTravel) return 0.0f;
        float distance = wanted;
        if (blocked(0.0f, wanted)) {
            distance = 0.0f;
            while (distance < wanted) {
                const float next = (std::min)(wanted, distance + 0.20f);
                if (blocked(distance, next)) break;
                distance = next;
            }
        }
        PhysicsQueryFilter solid = terrain;
        solid.mask |= CollisionCategories::Get("enemy");
        for (; distance >= kMinTravel; distance -= 0.20f) {
            const auto delta = dir * distance;
            const Vec mid = body.center + delta;
            if (!world->OverlapAabb({mid - body.half, mid + body.half}, solid).empty())
                continue;
            if (needsFloor) {
                const auto floor = world->SphereCast(mid + Vec{0,body.radius - body.half.y + 0.08f,0},
                    body.radius, {0,-1,0}, 0.55f, terrain);
                if (!floor.isHit || floor.normal.y < 0.65f)
                    continue;
            }
            return distance;
        }
        return 0.0f;
    }

    // 接触する偽王の装甲を回避通過で開くための通知を行う。
    static void CrossArmor(Player& player, const Body& body, Vec dir, float distance) {
        PhysicsQueryFilter enemies;
        enemies.ignoredObject = &player;
        enemies.mask = CollisionCategories::Get("enemy");
        std::unordered_set<EnemyFalseKingSlime*> opened;
        for (const auto& hit : CollisionManager::GetInstance()->OverlapCapsule(
                body.center, body.center + dir * distance, body.radius, enemies)) {
            auto* king = dynamic_cast<EnemyFalseKingSlime*>(CopyAbilityDetail::FindEnemyRoot(hit.object));
            if (!king || !opened.insert(king).second)
                continue;
            const auto bounds = king->GetAabb();
            const float along = engine::Math::Dot((bounds.min + bounds.max) * 0.5f - body.center, dir);
            if (along > 0.0f && along < distance)
                king->OpenCrownArmor();
        }
    }

    static float Ease(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    Vec PortalNormal(bool exit) const {
        return engine::Math::Normalize(direction_ * (exit ? -1.0f : 1.0f) + Vec{0,-0.58f,0});
    }
    void PosePortal(engine::Object3D& gate, Vec center, float open, float opacity, float deltaTime) {
        gate.SetIsVisible(opacity > 0.005f);
        gate.SetTranslate(center);
        const float extent = gateExtent_;
        gate.SetScale({extent * (std::max)(0.01f, open), extent * (0.83f + 0.17f * open), extent});
        if (auto* material = gate.GetMeshRenderer()->GetWaterParamData()) {
            material->effectType = -1.0f; // 回避専用。中央を透かし、縁と渦に光を残します。
            material->effectIntensity = 0.16f + 1.65f * open;
            material->uvOffsetY = open;
            material->uvOffsetX = opacity;
        }
        gate.Update(deltaTime);
    }
    // 現在の進行度に合わせ、Playerと両ゲートの位置・断面表示を同時に適用する。
    void Pose(Player& player, float deltaTime) {
        const auto body = Measure(player);
        // 水平位置は固定し、描画クリップの踏み込みでゲート面を越えます。空中では高さだけ追従します。
        Vec entry = start_ + direction_ * entranceOffset_;
        const float travel = engine::Math::Dot(destination_ - start_, direction_);
        Vec end = destination_ - direction_ * (std::min)(halfSpan_ * 0.54f, travel * 0.26f);
        entry.y = end.y = body.center.y + 0.17f * halfSpan_;
        const float entryFade = 1.0f - Ease((age_ - 0.115f) / 0.055f);
        const float exitCue = Ease((age_ - 0.085f) / 0.033f);
        const float exitOpen = Ease((age_ - kTransit + 0.005f) / 0.055f);
        const float exitFade = 1.0f - Ease((age_ - 0.212f) / 0.076f);
        PosePortal(*entrance_, entry, Ease((age_ + 0.002f) / 0.045f) * entryFade, entryFade, deltaTime);
        PosePortal(*exit_, end, (0.045f + 0.955f * exitOpen) * exitFade, exitCue * exitFade, deltaTime);
        engine::Object3D::PortalClipSettings clip;
        clip.enabled = true;
        clip.center = crossed_ ? end : entry;
        // 入口で高い王冠を最後に、出口では最初に見せる傾き。描画面も同じ法線で作ります。
        clip.normal = PortalNormal(crossed_);
        clip.progress = 0.47f;
        clip.edgeWidth = 0.12f;
        clip.dissolve = 0.055f;
        clip.color = {1.6f,1.25f,0.76f,1.0f};
        player.SetPortalClip(clip); // 覇王の王冠はプレイヤーモデルと同じメッシュです。
    }

public:
    bool Active() const { return entrance_ != nullptr; }
    bool Ready() const { return !Active() && cooldown_ <= 0.0f; }
    // 安全に移動できる経路を確保できた場合だけ操作を停止し、回避無敵を開始する。
    template<class MakeGate>
    bool Begin(Player& player, const CrownLanceSettings& settings, MakeGate makeGate) {
        if (!Ready() || (!player.IsControlActive() && !player.IsEditorInternal()))
            return false;
        direction_ = player.GetMoveInputDirection();
        if (engine::Math::Length(direction_) < 0.001f)
            direction_ = player.GetForwardDirection();
        const Body body = Measure(player);
        start_ = player.GetWorldPosition();
        // 隔離プレビューは仮想の平らな床を使い、シーンに物理床を登録しません。
        needsFloor_ = player.IsGrounded() && !player.IsEditorInternal();
        const float wanted = BeyondCrownArmor(player, body, direction_, settings.warpDistance);
        const float distance = SafeDistance(player, body, direction_, wanted, needsFloor_);
        if (distance < kMinTravel)
            return false;
        auto entry = makeGate(PortalNormal(false));
        auto end = makeGate(PortalNormal(true));
        if (!entry || !end)
            return false;
        entrance_ = std::move(entry);
        exit_ = std::move(end);
        destination_ = start_ + direction_ * distance;
        halfSpan_ = (std::max)(0.80f, (std::max)({body.half.x,body.half.z,body.half.y}) * 1.25f);
        entranceOffset_ = (std::min)(halfSpan_ * 0.94f, distance * 0.35f);
        presentation_ = CrownPresentationSettings::Get(); presentation_.Sanitize();
        gateExtent_ = halfSpan_ * settings.warpGateSize * presentation_.warpGateScale;
        const engine::Vector4 membrane{1.0f, 0.80f, 0.48f, settings.warpMembraneOpacity};
        entrance_->SetColor(membrane);
        exit_->SetColor(membrane);
        age_ = 0.0f;
        crossed_ = false;
        cooldown_ = settings.warpCooldown;
        restoreControl_ = player.IsControlActive();
        player.SetIsControlActive(false);
        player.SetVelocity({0,player.GetVelocity().y,0});
        player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
        player.SetSlimeAnimationDirection(direction_);
        player.TriggerSlimeImpulse({0.82f,0.84f,1.40f},0.12f);
        player.StartEvasionInvincibility(settings.warpInvincibility, Player::EvasionFeedback::PortalEdge);
        Pose(player,0.0f);
        const Vec cue = start_ + direction_ * entranceOffset_ + Vec{0,body.center.y-start_.y,0};
        CrownAbilityEffects::Emit("player_crown_portal_dust", cue, direction_, gateExtent_);
        effects_.Ring(cue, PortalNormal(false), gateExtent_ * 1.08f);
        return true;
    }
    // dtは秒。通過時に出口へ切り替え、終了・死亡・演出ロックでは一時状態を戻す。
    void Update(Player& player, float deltaTime) {
        effects_.Update(deltaTime);
        cooldown_ = (std::max)(0.0f, cooldown_ - deltaTime);
        if (!Active())
            return;
        age_ += deltaTime;
        if (!crossed_ && age_ >= kTransit) {
            const auto body = Measure(player);
            const auto from = player.GetWorldPosition();
            const float wanted = engine::Math::Dot(destination_ - from, direction_);
            const float distance = SafeDistance(player, body, direction_, wanted, needsFloor_);
            if (distance < kMinTravel) {
                End(player);
                return;
            }
            destination_ = from + direction_ * distance;
            player.PlaySlimeAbilityMotion("player_ability_crown_warp_exit", false, 1.0f, 0.0f, 0.03f);
            CrossArmor(player, body, direction_, distance);
            player.SetTranslate(destination_);
            player.UpdateLocalMatrix();
            player.UpdateWorldMatrix(true);
            player.TriggerSlimeImpulse({1.16f,0.82f,1.20f},0.13f);
            crossed_ = true;
            const Vec cue = body.center + direction_ * distance;
            CrownAbilityEffects::Emit("player_crown_portal_exit", cue, direction_, presentation_.particleScale);
            effects_.Ring(cue, PortalNormal(true), gateExtent_ * 1.10f);
        }
        if (age_ >= kDuration) {
            End(player);
            return;
        }
        Pose(player,deltaTime);
    }
    // 回避の残光を解放する。Playerの操作や通過位置はここでは変えない。
    void ClearEffects() { effects_.Clear(); }
    void DrawEffects(ID3D12Resource* point, ID3D12Resource* spot, Camera* camera, int index) {
        effects_.Draw(point,spot,camera,index);
    }
    void Draw(Camera* camera, uint32_t depth, uint32_t grab, int index) {
        if (!Active()) return;
        for (auto* gate : {entrance_.get(), exit_.get()}) {
            if (!gate->GetIsVisible()) continue;
            gate->UpdateWorldMatrix(true);
            gate->DrawSpecialMaterialForCamera(camera,depth,grab,index);
        }
    }
    // 断面表示・能力モーション・ゲートを片付け、許される場合だけ操作を復元する。
    void End(Player& player) {
        if (!Active()) return;
        player.ClearPortalClip();
        player.StopSlimeAbilityMotion(0.03f);
        if (restoreControl_ && !player.IsDead() && !player.IsCinematicLocked())
            player.SetIsControlActive(true);
        entrance_.reset();
        exit_.reset();
    }
};
