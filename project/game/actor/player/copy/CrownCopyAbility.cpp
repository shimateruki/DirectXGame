#include "CopyAbilitySupport.h"
#include "CrownLanceSettings.h"
#include "CrownLanceTrail.h"
#include "CrownGateWarp.h"
#include "CrownMeleeCombo.h"
#include "CameraManager.h"
#include <array>

namespace {
using namespace CopyAbilityDetail;

// 王冠能力の溜めゲート槍・近接コンボ・ゲート回避と、その表示物の寿命を管理する。
// PlayerCopyAbilityControllerが所有する王冠の槍・ゲート回避の実装。入力はProcessInput、進行はUpdate、一時表示の回収はCancelが担当する。
class CrownCopyAbility final : public ICopyAbilitySession {
  public:
    using ICopyAbilitySession::ICopyAbilitySession;

    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) override {
        // 演出中は解除し、近接コンボ・ゲート回避・ゲート槍の入力を分けます。
        if (player.IsCinematicLocked() || player.IsDead()) {
            Cancel(player);
            return;
        }
        if (warp_.Active())
            return;
        if (melee_.Active()) {
            if (input.secondaryTriggered && melee_.CanDodge() && warp_.Ready()) {
                // 回避の経路確認に成功した時だけ近接を終了します。壁際で失敗したら近接を続けます。
                player.SetIsControlActive(true);
                if (BeginWarp(player)) {
                    melee_.End(player, false);
                    player.PlaySlimeAbilityMotion("player_ability_crown_warp_enter", false, 1.0f, 0.0f, 0.0f);
                    return;
                }
                player.SetIsControlActive(false);
            }
            if (input.specialTriggered) melee_.QueueNext();
            return;
        }
        if (input.secondaryTriggered && BeginWarp(player))
            return;
        if (input.specialTriggered && melee_.Ready()) {
            StopLanceCharge(player);
            melee_.Begin(player, [this](const char* model) { return MakeVisual(model); },
                          [this] { return MakeGatePortal(); });
            return;
        }

        // 押した時に形成を始め、離した時だけ発射予約を作ります。
        if (!input.specialTriggered && !input.secondaryTriggered && input.primaryTriggered && lanceCooldown_ <= 0.0f &&
            !charging_ && gates_.empty())
            BeginLanceCharge(player);
        if (charging_ && !input.primaryHeld)
            ReleaseLances(player);
    }

    void Update(Player& player, float deltaTime) override {
        // 近接は槍の弧、飛び道具は移動線分を検査してフレーム間のすり抜けを防ぎます。
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f)
            return;
        if (player.IsCinematicLocked() || player.IsDead()) {
            Cancel(player);
            return;
        }
        time_ += deltaTime;
        effects_.Update(deltaTime);
        readyFlashTime_ = (std::max)(0.0f, readyFlashTime_ - deltaTime);
        lanceCooldown_ = std::max(0.0f, lanceCooldown_ - deltaTime);
        warp_.Update(player, deltaTime);
        melee_.Update(player, deltaTime);
        UpdateLanceCharge(player, deltaTime);
        const auto& tuning = CrownFinaleTuning::Get();

        std::erase_if(remnants_, [deltaTime](Shot& shot) { return UpdateShotDissolve(shot, deltaTime); });
        UpdateGates(player, deltaTime);
        for (auto it = shots_.begin(); it != shots_.end();) {
            // 低fpsでも同じフレーム内の発射順が距離へ反映されるよう、発射後の残り時間だけ進めます。
            const float step = it->firstStep >= 0.0f ? (std::min)(it->firstStep, deltaTime) : deltaTime;
            it->firstStep = -1.0f;
            it->life -= step;
            const engine::Vector3 movement = it->velocity * step;

            // 高速弾は線分で調べ、フレーム間で敵や地形を飛び越えません。
            PhysicsQueryFilter filter;
            filter.mask = CollisionCategories::Get("enemy") | CollisionCategories::Get("ground");
            filter.ignoredObject = &player;
            const float length = engine::Math::Length(movement);
            const auto hit = CollisionManager::GetInstance()->SphereCast(
                it->position, 0.16f * it->scale,
                length > 0.0001f ? movement / length : engine::Vector3{0.0f, -1.0f, 0.0f}, length, filter);
            if (hit.isHit) {
                if (engine::Object3D* enemy = FindEnemyRoot(hit.hitObject)) {
                    const auto generation = cancelGeneration_;
                    DamageEnemy(player, enemy, tuning.lanceDamage * it->damageScale, DamageType::Physical, {});
                    // 命中イベント内でコピー解除されたら、消された弾の参照を使いません。
                    if (generation != cancelGeneration_) return;
                }

                const auto direction = Unit(it->velocity);
                const auto normal = Unit(hit.normal, direction * -1.0f);
                // 接触時の球の中心で槍を固定し、残り時間だけ消散の時計を進めます。
                const float travel = std::clamp(hit.distance, 0.0f, length);
                it->position += direction * travel;
                it->visual->SetTranslate(it->position);
                EmitLanceImpact(hit.hitPoint, normal, it->isFullChargeCenter, it->presentation);
                BeginShotDissolve(*it);
                const float remaining = step - travel / (std::max)(0.001f, engine::Math::Length(it->velocity));
                if (!UpdateShotDissolve(*it, (std::max)(0.0f, remaining)))
                    remnants_.push_back(std::move(*it));
                it = shots_.erase(it);
                continue;
            }

            it->position += movement;
            it->visual->SetTranslate(it->position);
            it->visual->Update(step);
            const auto direction = Unit(it->velocity);
            if (it->emerging && engine::Math::Dot(it->position - it->gateOrigin, direction) >= RearOffset(it->scale)) {
                it->visual->ClearPortalClip();
                it->emerging = false;
            }
            UpdateTrail(*it, 0.0f, step);
            if (it->life <= 0.0f) {
                BeginShotDissolve(*it);
                UpdateShotDissolve(*it, 0.0f);
                remnants_.push_back(std::move(*it));
                it = shots_.erase(it);
            }
            else
                ++it;
        }

        ambientTimer_ -= deltaTime;

        if (ambientTimer_ <= 0.0f) {
            EmitPreset("crown_goal_idle_sparkle", player.GetWorldPosition() + engine::Vector3{0.0f, 1.8f, 0.0f});
            ambientTimer_ = 0.65f;
        }
    }

    void Draw(Player& player, ID3D12Resource* point, ID3D12Resource* spot, Camera* camera,
              int previewBufferIndex) override {
        melee_.Draw(point, spot, camera, previewBufferIndex);
        effects_.Draw(point, spot, camera, previewBufferIndex);
        warp_.DrawEffects(point, spot, camera, previewBufferIndex);

        for (const auto& gate : gates_) {
            if (!gate.fired)
                DrawVisual(gate.lance.get(), point, spot, camera, previewBufferIndex);
        }
        for (const auto& shot : shots_)
            DrawVisual(shot.visual.get(), point, spot, camera, previewBufferIndex);
        for (const auto& shot : remnants_)
            DrawVisual(shot.visual.get(), point, spot, camera, previewBufferIndex);
        for (auto& shot : shots_)
            shot.trail.Draw(point, spot, camera, previewBufferIndex);
        for (auto& shot : remnants_)
            shot.trail.Draw(point, spot, camera, previewBufferIndex);
    }

    bool HasOwnedVisuals() const override { return melee_.Active() || warp_.Active() || !gates_.empty(); }
    void DrawOwnedVisuals(Player& player, uint32_t depth, uint32_t grab, Camera* camera, int bufferIndex) override {
        // ゲートは深度・画面色を参照する専用パスへ渡し、通常の槍と二重描画しません。
        if (player.IsDead() || player.IsCinematicLocked())
            return;
        if (!camera)
            camera = CameraManager::GetInstance()->GetActiveCamera();
        if (!camera)
            return;
        warp_.Draw(camera, depth, grab, bufferIndex);
        melee_.DrawGate(camera, depth, grab, bufferIndex);
        for (auto& gate : gates_) {
            gate.portal->UpdateWorldMatrix(true);
            gate.portal->DrawSpecialMaterialForCamera(camera, depth, grab, bufferIndex);
        }
    }
    const char* GetStateLabel() const override {
        if (warp_.Active()) return "覇王・ゲート回避";
        if (melee_.Active()) {
            static constexpr const char* kLabels[]{"覇王・横薙ぎ", "覇王・斬り上げ", "覇王・叩きつけ"};
            return kLabels[melee_.Stage()];
        }
        if (charging_)
            return chargeTime_ >= lanceSettings_.fullChargeTime  ? "覇王・最大溜め"
                   : chargeTime_ >= lanceSettings_.fiveGateTime  ? "覇王・5ゲート"
                   : chargeTime_ >= lanceSettings_.threeGateTime ? "覇王・3ゲート"
                                                                 : "覇王・1ゲート";
        return gates_.empty() ? "覇王・待機" : "覇王・ゲート槍発射";
    }
    void NotifyDamageStarted(Player& player) override {
        melee_.End(player);
    }
    void Cancel(Player& player) override {
        ++cancelGeneration_;
        melee_.End(player);
        melee_.ClearEffects();
        effects_.Clear();
        warp_.ClearEffects();
        StopLanceCharge(player);
        player.ClearSlimeAbilityMotion();
        warp_.End(player);
        shots_.clear();
        remnants_.clear();
    }

  private:
    // 王冠能力の弾表示と移動状態。lifeは残り秒、scaleは外観と命中半径の倍率です。
    struct Shot {
        std::unique_ptr<engine::Object3D> visual;
        engine::Vector3 position{}, velocity{}, gateOrigin{};
        CrownLanceTrail trail;
        CrownLanceSettings::Appearance appearance;
        CrownPresentationSettings presentation;
        float dissolveAge = 0.0f;
        int nextFlake = 0;
        bool emerging = true;
        float life = 2.2f;
        float scale = 1.0f, damageScale = 1.0f, firstStep = -1.0f;
        bool isFullChargeCenter = false;
    };

    std::unique_ptr<engine::Object3D> MakeVisual(const char* model) {
        // 弾や翼は表示専用で生成し、物理Colliderの対象には入れない。
        auto* scene = SceneManager::GetInstance()->GetCurrentScene();
        if (!scene || !scene->GetObject3DCommon())
            return {};
        auto visual = std::make_unique<engine::Object3D>();
        visual->Initialize(scene->GetObject3DCommon());
        visual->SetModel(model);
        visual->SetMaterialType(0);
        visual->SetBlendMode(BlendMode::None);
        visual->SetColliderType(ColliderType::None);
        visual->SetCollisionAttribute(0);
        visual->SetCollisionMask(0);
        visual->SetColor({1.0f, 0.92f, 0.58f, 1.0f});
        visual->SetEmissive(1.7f);
        visual->SetEnableLighting(true);
        return visual;
    }

    std::unique_ptr<engine::Object3D> MakeGatePortal() {
        auto portal = MakeVisual("Gimmicks/portal_surface");
        if (!portal) return {};
        portal->SetMaterialType(22);
        portal->SetBlendMode(BlendMode::Normal);
        portal->SetEnableLighting(false);
        portal->SetEmissive(2.5f);
        portal->SetQuaternionMaster(true);
        if (auto* material = portal->GetMeshRenderer()->GetWaterParamData()) {
            material->waveSpeed = 2.25f;
            material->waveHeight = 1.18f;
            material->waveFrequency = 19;
            material->effectSoftness = 0.56f;
            material->effectScale = 1.12f;
            material->effectScaleX = material->effectScaleY = 1;
            material->effectScaleZ = 0.12f;
            material->effectType = 1;
            material->flowSpeedX = material->flowSpeedY = 0;
            material->billboardScale = 1;
        }
        return portal;
    }
    bool BeginWarp(Player& player) {
        if (!warp_.Ready()) return false;
        if (!warp_.Begin(player, CrownLanceSettings::Get(), [this](const auto& normal) {
                auto gate = MakeGatePortal();
                if (gate) {
                    gate->SetQuaternionMaster(false);
                    gate->SetEulerAngles({-std::asin(normal.y), std::atan2(normal.x, normal.z), 0});
                }
                return gate;
            })) return false;
        StopLanceCharge(player);
        player.PlaySlimeAbilityMotion("player_ability_crown_warp_enter", false, 1.0f, 0.0f, 0.0f);
        return true;
    }

    static void DrawVisual(engine::Object3D* visual, ID3D12Resource* point, ID3D12Resource* spot, Camera* camera,
                           int previewBufferIndex) {
        // 指定カメラがあればプレビューへ描き、なければ現在シーンのカメラを使う。
        if (!visual)
            return;
        visual->UpdateWorldMatrix(true);
        if (camera)
            visual->DrawForCamera(camera, point, spot, previewBufferIndex);
        else {
            visual->RefreshRenderCameraData();
            visual->Draw(point, spot);
        }
    }

    // 溜め中は追従し、解除時に射出口・方向・発射時刻を固定します。
    struct Gate {
        std::unique_ptr<engine::Object3D> portal, lance;
        engine::Vector3 position{}, direction{0, 0, 1};
        float age = 0.0f, releaseAt = 0.0f, closeAge = 0.0f, scale = 1.0f;
        int index = 0;
        bool fired = false;
    };
    static engine::Vector3 Unit(const engine::Vector3& v, const engine::Vector3& fallback = {0, 0, 1}) {
        return engine::Math::Length(v) > 0.0001f ? engine::Math::Normalize(v) : fallback;
    }
    static engine::Quaternion AimRotation(const engine::Vector3& axis, const engine::Vector3& direction) {
        const auto to = Unit(direction, axis);
        const float dot = std::clamp(engine::Math::Dot(axis, to), -1.0f, 1.0f);
        if (dot > 0.9999f)
            return {0, 0, 0, 1};
        if (dot < -0.9999f) {
            const auto perpendicular = Unit(engine::Math::Cross(axis, {1, 0, 0}), {0, 1, 0});
            return {perpendicular.x, perpendicular.y, perpendicular.z, 0};
        }
        const auto cross = engine::Math::Cross(axis, to);
        const float q = std::sqrt((1 + dot) * 2);
        return {cross.x / q, cross.y / q, cross.z / q, q * 0.5f};
    }
    // プリセットは共有のまま保持し、今回の位置・方向・大きさだけをコピーへ反映します。
    static void EmitLanceEffect(const char* preset, const engine::Vector3& position,
                                const engine::Vector3& direction = {0, 1, 0}, float scale = 1.0f,
                                bool directed = false) {
        auto* particles = GpuParticleManager::GetInstance();
        if (!particles || !particles->IsInitialized())
            return;
        const auto& presets = particles->GetPresets();
        const auto found = presets.find(preset);
        if (found == presets.end())
            return;
        auto config = found->second;
        config.emitPos = position;
        config.baseSize *= scale;
        config.midSize *= scale;
        config.endSize *= scale;
        config.emitArea = config.emitArea * scale;
        config.shapeRadius *= scale;
        config.fieldRadius *= scale;
        if (directed)
            config.emitVelocity = Unit(direction) * engine::Math::Length(config.emitVelocity);
        particles->EmitFromConfig(config);
    }
    static void EmitLanceImpact(const engine::Vector3& position, const engine::Vector3& normal, bool full,
                                const CrownPresentationSettings& style) {
        // 接触面の手前へ少し出し、敵・壁の中へ閃光が埋まるのを防ぎます。
        const auto origin = position + normal * 0.08f;
        EmitLanceEffect(full ? "player_crown_impact_full" : "player_crown_impact", origin, normal, (full ? 0.65f : 0.46f) * style.particleScale);
        EmitLanceEffect("player_crown_impact_sparks", origin, normal, (full ? 1.4f : 1.1f) * style.particleScale, true);
        if (full)
            EmitLanceEffect("player_crown_impact_shards", origin, normal, style.particleScale, true);
    }
    // 槍モデルのローカルY範囲は -2.02（穂先）～+1.63（柄尻）。縦倍率はモデルと共有します。
    static constexpr float kAxialScale = 0.8f;
    static float RearOffset(float scale) { return 1.63f * kAxialScale * scale; }
    static engine::Object3D::PortalClipSettings GateClip(const Gate& gate,
                                                         const CrownLanceSettings::Appearance& style) {
        engine::Object3D::PortalClipSettings clip;
        clip.enabled = true;
        clip.center = gate.position;
        clip.normal = gate.direction * -1.0f;
        clip.progress = 0.5f;
        clip.edgeWidth = style.edgeWidth;
        clip.dissolve = 0.12f;
        clip.color = {style.edgeColor[0], style.edgeColor[1], style.edgeColor[2], 1.0f};
        return clip;
    }
    static void UpdateTrail(Shot& shot, float fade, float deltaTime) {
        const auto direction = Unit(shot.velocity);
        // 柄がゲートから出る前の帯はゲート面へ畳み、溜め側へ突き抜けないようにします。
        const float fromGate = engine::Math::Dot(shot.position - shot.gateOrigin, direction);
        const auto root = shot.gateOrigin + direction * (std::max)(0.0f, fromGate - RearOffset(shot.scale));
        shot.trail.Update(root, shot.appearance, shot.isFullChargeCenter, fade, deltaTime);
    }
    static void BeginShotDissolve(Shot& shot) {
        shot.visual->ClearPortalClip();
        shot.dissolveAge = 0.0f;
    }
    static bool UpdateShotDissolve(Shot& shot, float deltaTime) {
        shot.dissolveAge += deltaTime;
        const float duration = shot.appearance.dissolveTime * (shot.isFullChargeCenter ? 1.25f : 1.0f);
        // 約2フレームだけ金属の姿を残し、衝撃と崩れ始めを区別します。
        constexpr float kImpactHoldSeconds = 0.03f;
        if (shot.dissolveAge < kImpactHoldSeconds) {
            shot.visual->Update(deltaTime);
            UpdateTrail(shot, 0.0f, deltaTime);
            return false;
        }
        const float t = SmoothStep((shot.dissolveAge - kImpactHoldSeconds) / duration);
        if (t >= 1.0f)
            return true;
        const auto direction = Unit(shot.velocity);
        // 一斉命中時も金属の輪郭が残るよう、ボスの強い発光は最大溜めの中央だけに使います。
        if (shot.isFullChargeCenter && shot.visual->GetMaterialType() != 4) {
            shot.visual->SetMaterialType(4);
            shot.visual->SetBlendMode(BlendMode::Normal);
            shot.visual->SetColor({1, 1, 1, 1});
            shot.visual->SetEmissive(1.35f);
        }
        const auto& tint = shot.appearance.edgeColor;
        engine::Object3D::PortalClipSettings clip;
        clip.enabled = true;
        clip.center = shot.position;
        clip.normal = direction;
        if (shot.isFullChargeCenter) {
            clip.progress = t;
        } else {
            // 材質0のノイズ付き断面を穂先から柄尻へ通します。
            const float tip = 2.02f * kAxialScale * shot.scale;
            const float rear = RearOffset(shot.scale);
            const float margin = 0.30f + shot.appearance.edgeWidth;
            const float cut = std::lerp(tip + margin, -rear - margin, t);
            clip.center = shot.position + direction * cut;
            clip.progress = 0.5f;
            clip.dissolve = 0.38f;
        }
        clip.edgeWidth = shot.appearance.edgeWidth * (1.0f - 0.30f * t);
        clip.color = {tint[0], tint[1], tint[2], 1.0f};
        shot.visual->SetPortalClip(clip);
        shot.visual->Update(deltaTime);
        UpdateTrail(shot, t, deltaTime);
        // 発生回数は槍ごとに制限。穂先から柄へ崩れる位置に小片を置きます。
        constexpr std::array<float, 3> kDissolveFlakeThresholds{0.22f, 0.50f, 0.76f};
        const int count = shot.isFullChargeCenter ? 3 : 1;
        while (shot.nextFlake < count && t >= kDissolveFlakeThresholds[shot.nextFlake]) {
            const float offset = (0.40f - kDissolveFlakeThresholds[shot.nextFlake] * 1.85f) * shot.scale;
            EmitLanceEffect("player_crown_dissolve_flakes", shot.position + direction * offset);
            ++shot.nextFlake;
        }
        return false;
    }
    void BeginLanceCharge(Player& player) {
        lanceSettings_ = CrownLanceSettings::Get();
        lanceSettings_.Sanitize();
        presentation_ = CrownPresentationSettings::Get();
        presentation_.Sanitize();
        charging_ = true;
        fullCuePlayed_ = false;
        chargeVfxTimer_ = readyFlashTime_ = 0.0f;
        chargeTime_ = releaseTime_ = 0;
        player.PlaySlimeAbilityMotion("player_ability_crown_charge", true, 1.0f, 0.05f, 0.08f);
        UpdateLanceCharge(player, 0.0f);
    }
    void StopLanceCharge(Player& player) {
        if (charging_ || !gates_.empty())
            player.StopSlimeAbilityMotion(0.08f);
        charging_ = false;
        chargeTime_ = releaseTime_ = 0;
        gates_.clear();
        chargeVfxTimer_ = readyFlashTime_ = 0.0f;
    }
    void UpdateLanceCharge(Player& player, float deltaTime) {
        if (!charging_)
            return;
        const float previousCharge = chargeTime_;
        chargeTime_ = (std::min)(lanceSettings_.fullChargeTime, chargeTime_ + deltaTime);
        const int count = lanceSettings_.GateCount(chargeTime_);
        while (static_cast<int>(gates_.size()) < count) {
            Gate gate;
            gate.index = static_cast<int>(gates_.size());
            const float birth = gate.index == 0   ? 0.0f
                                : gate.index <= 2 ? lanceSettings_.threeGateTime
                                                  : lanceSettings_.fiveGateTime;
            // 段階を跨いだフレームの途中から開き始め、低fpsでも突然全開にしません。
            gate.age = -std::clamp(birth - previousCharge, 0.0f, deltaTime);
            gate.portal = MakeGatePortal();
            gate.lance = MakeVisual("Effects/false_king_crown_lance");
            if (!gate.portal || !gate.lance)
                break;
            gate.portal->SetIsVisible(false);
            gate.lance->SetIsVisible(false);
            // 本体の金属色を残し、強い発光はゲートと軌跡に受け持たせます。
            gate.lance->SetEmissive(1.05f);
            gate.lance->SetEnableEnvMap(true);
            gate.lance->SetEnvIntensity(0.5f);
            gate.lance->SetRoughness(0.28f);
            gate.lance->SetMetallic(0.55f);
            gate.lance->SetQuaternionMaster(true);
            gates_.push_back(std::move(gate));
        }
        UpdateGateFormation(player);
        if (!fullCuePlayed_ && chargeTime_ >= lanceSettings_.fullChargeTime) {
            fullCuePlayed_ = true;
            readyFlashTime_ = 0.16f;
            if (!gates_.empty())
                EmitLanceEffect("player_crown_ready", gates_.front().position, {0,1,0}, presentation_.particleScale);
        }
        chargeVfxTimer_ -= deltaTime;
        if (chargeVfxTimer_ <= 0.0f) {
            for (const auto& gate : gates_)
                EmitLanceEffect("player_crown_charge", gate.position, gate.direction, presentation_.lanceGateScale);
            // 自動ループを使わず、溜め解除後には新しい粒子を発生させません。
            chargeVfxTimer_ = 0.10f;
        }
    }
    void UpdateGateFormation(Player& player) {
        const auto forward = NormalizePlanar(player.GetForwardDirection());
        const engine::Vector3 right{forward.z, 0, -forward.x};
        const auto aim = Unit(player.GetLockOnAttackDirection(), forward);
        const auto anchor = player.GetWorldPosition() + engine::Vector3{0, 1.2f, 0};
        PhysicsQueryFilter filter;
        filter.mask = CollisionCategories::Get("enemy") | CollisionCategories::Get("ground");
        filter.ignoredObject = &player;
        const auto hit = CollisionManager::GetInstance()->Raycast(anchor, aim, 32, filter);
        const auto target = hit.isHit ? hit.hitPoint : anchor + aim * 18;
        const float full = SmoothStep((chargeTime_ - lanceSettings_.fiveGateTime) /
                                      (lanceSettings_.fullChargeTime - lanceSettings_.fiveGateTime));
        for (auto& gate : gates_) {
            const int column = gate.index == 0 ? 0 : (gate.index + 1) / 2 * (gate.index % 2 ? -1 : 1);
            gate.position = player.GetWorldPosition() - forward * 0.35f +
                            right * (static_cast<float>(column) * lanceSettings_.gateSpacing * std::sqrt(presentation_.lanceGateScale)) +
                            engine::Vector3{0, lanceSettings_.gateHeight - std::abs(column) * 0.30f, 0};
            gate.direction = Unit(target - gate.position, aim);
            gate.scale = (gate.index == 0 ? 1 + (lanceSettings_.fullCenterScale - 1) * full : 1) * presentation_.lanceScale;
        }
    }
    void ReleaseLances(Player& player) {
        if (!charging_)
            return;
        UpdateGateFormation(player);
        charging_ = false;
        releaseTime_ = 0;
        lanceCooldown_ = lanceSettings_.cooldown;
        float readyWait = 0.0f;
        for (const auto& gate : gates_)
            readyWait = (std::max)(readyWait, lanceSettings_.openDuration - gate.age);
        for (auto& gate : gates_) {
            const int rank = gate.index == 0 ? static_cast<int>(gates_.size()) - 1 : gate.index - 1;
            gate.releaseAt = readyWait + rank * lanceSettings_.shotInterval;
        }
        // 発射反動は最初の槍が実際に出る時に始めます。
        player.StopSlimeAbilityMotion(0.08f);
        releasePosePlayed_ = false;
    }
    void FireGate(Player& player, Gate& gate) {
        gate.fired = true;
        gate.closeAge = 0;
        if (!releasePosePlayed_) {
            player.PlaySlimeAbilityMotion("player_ability_crown_release", false, 1.0f, 0.02f, 0.08f);
            releasePosePlayed_ = true;
        }
        if (shots_.size() >= 24)
            return;
        const auto position = gate.position + gate.direction * 0.45f;
        const auto anchor = player.GetWorldPosition() + engine::Vector3{0, 1.2f, 0};
        const auto delta = position - anchor;
        const float distance = engine::Math::Length(delta);
        PhysicsQueryFilter wall;
        wall.mask = CollisionCategories::Get("ground");
        wall.ignoredObject = &player;
        const auto obstruction =
            CollisionManager::GetInstance()->SphereCast(anchor, 0.16f * gate.scale, Unit(delta), distance, wall);
        if (obstruction.isHit) {
            EmitLanceImpact(obstruction.hitPoint, Unit(obstruction.normal, gate.direction * -1.0f), false, presentation_);
            return;
        }
        Shot shot;
        shot.visual = std::move(gate.lance);
        shot.visual->SetIsVisible(true);
        shot.position = gate.position;
        shot.velocity = gate.direction * lanceSettings_.shotSpeed;
        shot.scale = gate.scale;
        // 外観倍率で威力まで増えないよう、溜めによる倍率だけを使います。
        shot.damageScale = gate.scale / presentation_.lanceScale;
        shot.presentation = presentation_;
        shot.isFullChargeCenter = gate.index == 0 && chargeTime_ >= lanceSettings_.fullChargeTime;
        shot.gateOrigin = gate.position;
        shot.appearance = lanceSettings_.appearance;
        shot.appearance.trailWidth *= presentation_.trailScale;
        shot.appearance.trailLength *= std::sqrt(presentation_.lanceScale);
        shot.trail.Initialize(shot.visual->GetObjectCommon(), gate.position, AimRotation({0, -1, 0}, gate.direction));
        shot.visual->SetPortalClip(GateClip(gate, shot.appearance));
        shot.visual->SetQuaternion(AimRotation({0, -1, 0}, gate.direction));
        shot.visual->SetTranslate(shot.position);
        shot.visual->SetScale({0.6f * gate.scale, kAxialScale * gate.scale, 0.6f * gate.scale});
        shots_.push_back(std::move(shot));
        EmitLanceEffect(shots_.back().isFullChargeCenter ? "player_crown_muzzle_full" : "player_crown_muzzle",
                        gate.position, gate.direction, presentation_.particleScale);
        effects_.Ring(gate.position, gate.direction, lanceSettings_.gateScale * presentation_.lanceGateScale * 1.3f);
        EmitLanceEffect("player_crown_launch_streak", gate.position, gate.direction,
                        (shots_.back().isFullChargeCenter ? 1.4f : 1.1f) * presentation_.particleScale, true);
    }
    void UpdateGates(Player& player, float deltaTime) {
        if (!charging_)
            releaseTime_ += deltaTime;
        std::array<Gate*, 5> due{};
        size_t count = 0;
        for (auto& gate : gates_) {
            if (gate.fired)
                gate.closeAge += deltaTime;
            else if (!charging_ && releaseTime_ + 0.00001f >= gate.releaseAt)
                due[count++] = &gate;
        }
        // 同一フレームに複数本が発射されても、予約時刻順に出し中央槍を最後に保ちます。
        std::sort(due.begin(), due.begin() + count,
                  [](const Gate* a, const Gate* b) { return a->releaseAt < b->releaseAt; });
        for (size_t i = 0; i < count; ++i) {
            auto& gate = *due[i];
            const auto before = shots_.size();
            FireGate(player, gate);
            gate.closeAge = std::clamp(releaseTime_ - gate.releaseAt, 0.0f, deltaTime);
            if (shots_.size() > before)
                shots_.back().firstStep = gate.closeAge;
        }
        for (auto& gate : gates_) {
            gate.age += deltaTime;
            const float visibility = gate.fired ? 1 - SmoothStep(gate.closeAge / lanceSettings_.closeDuration)
                                                : SmoothStep(gate.age / lanceSettings_.openDuration);
            const float visibleScale = (std::max)(0.001f, visibility);
            const float size = lanceSettings_.gateScale * gate.scale / presentation_.lanceScale *
                               presentation_.lanceGateScale * visibleScale;
            gate.portal->SetIsVisible(visibility > 0.02f);
            gate.portal->SetTranslate(gate.position);
            gate.portal->SetScale({size, size, size});
            gate.portal->SetQuaternion(AimRotation({0, 0, 1}, gate.direction));
            gate.portal->SetColor({1.0f, 0.70f, 0.22f, 0.92f * visibility});
            if (auto* material = gate.portal->GetMeshRenderer()->GetWaterParamData()) {
                material->effectIntensity =
                    0.08f + 1.6f * visibility + (gate.index == 0 ? 0.8f * readyFlashTime_ / 0.16f : 0.0f);
                material->uvOffsetY = visibility;
                material->uvOffsetX = visibility * (0.45f + 0.55f * chargeTime_ / lanceSettings_.fullChargeTime);
            }
            gate.portal->Update(deltaTime);
            if (!gate.fired) {
                gate.lance->SetIsVisible(visibility > 0.02f);
                const float tip = 2.02f * kAxialScale * gate.scale;
                gate.lance->SetTranslate(gate.position - gate.direction * (tip * (1.0f - visibility)));
                gate.lance->SetScale({0.6f * gate.scale, kAxialScale * gate.scale, 0.6f * gate.scale});
                gate.lance->SetPortalClip(GateClip(gate, lanceSettings_.appearance));
                gate.lance->SetQuaternion(AimRotation({0, -1, 0}, gate.direction));
                gate.lance->Update(deltaTime);
            }
        }
        std::erase_if(gates_,
                      [&](const Gate& gate) { return gate.fired && gate.closeAge >= lanceSettings_.closeDuration; });
    }

    std::vector<Shot> shots_;
    // 命中済みの表示だけを持つ。こちらにはSphereCastやダメージ処理を呼びません。
    std::vector<Shot> remnants_;
    std::vector<Gate> gates_;
    CrownLanceSettings lanceSettings_;
    CrownPresentationSettings presentation_;
    CrownAbilityEffects effects_;
    float releaseTime_ = 0;
    float chargeVfxTimer_ = 0.0f, readyFlashTime_ = 0.0f;
    bool charging_ = false, fullCuePlayed_ = false, releasePosePlayed_ = false;
    CrownGateWarp warp_;
    CrownMeleeCombo melee_;
    float time_ = 0.0f, ambientTimer_ = 0.0f, chargeTime_ = 0.0f;
    float lanceCooldown_ = 0.0f;
    uint64_t cancelGeneration_ = 0;
};
} // namespace

namespace CopyAbilityDetail {
std::unique_ptr<ICopyAbilitySession> CreateCrownCopyAbility(const EnemyAttackProfile& profile) {
    // 王冠能力の一時状態を獲得ごとに独立させる。
    return std::make_unique<CrownCopyAbility>(profile);
}
} // namespace CopyAbilityDetail
