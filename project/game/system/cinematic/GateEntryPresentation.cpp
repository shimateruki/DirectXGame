#define NOMINMAX
#include "game/system/cinematic/GateEntryPresentation.h"
#include "Player.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Object3d.h"
#include "Model.h"
#include "PostEffect.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr float kDuration = 1.55f;
float Clamp(float t) { return std::clamp(t, 0.0f, 1.0f); }
float EaseOut(float t) { t = 1.0f - Clamp(t); return 1.0f - t * t * t; }
float EaseInOut(float t) {
    t = Clamp(t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}
engine::Vector3 Lerp(const engine::Vector3& a, const engine::Vector3& b, float t) {
    return {a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t, a.z + (b.z-a.z)*t};
}
engine::Vector3 LookAt(const engine::Vector3& eye, const engine::Vector3& target) {
    const auto d = target - eye;
    const float length = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
    return length > 0.0001f ? engine::Vector3{-std::asin(std::clamp(d.y/length, -0.98f, 0.98f)),
                                            std::atan2(d.x, d.z), 0.0f} : engine::Vector3{};
}
engine::Object3d* ResolveAnchor(engine::Object3d* gate) {
    // 判定面が見た目の枠の子なら、枠の中心へ入ります。地形などの整理用の親は使いません。
    for (auto* parent = gate->GetParent(); parent; parent = parent->GetParent()) {
        const auto model = parent->GetModelName();
        if (model == "Stages/gate" || model == "Gimmicks/crown_stage_gate") return parent;
    }
    return gate;
}
engine::Vector3 PortalPosition(engine::Object3d* gate, bool useModelCenter) {
    if (!useModelCenter || !gate->GetModel()) return gate->GetWorldPosition();
    const auto bounds = gate->GetModelWorldAABB();
    return (bounds.min + bounds.max) * 0.5f;
}
engine::Vector3 Direction(engine::Object3d* gate, const engine::Vector3& from, bool useModelCenter) {
    auto fallback = PortalPosition(gate,useModelCenter) - from;
    float length = std::sqrt(fallback.x*fallback.x + fallback.z*fallback.z);
    fallback = length > 0.0001f ? engine::Vector3{fallback.x/length, 0.0f, fallback.z/length}
                              : engine::Vector3{0.0f, 0.0f, 1.0f};
    const auto& world = gate->GetTransform()->matWorld;
    int normalAxis = 2;
    if (auto* model = gate->GetModel()) {
        const auto extent = model->GetLocalAabbMax() - model->GetLocalAabbMin();
        normalAxis = extent.x < extent.y ? (extent.x < extent.z ? 0 : 2) : (extent.y < extent.z ? 1 : 2);
    }
    auto direction = engine::Vector3{world.m[normalAxis][0], 0.0f, world.m[normalAxis][2]};
    length = std::sqrt(direction.x*direction.x + direction.z*direction.z);
    // 編集されたゲートが横向き・水平の場合は、接触した側から中心へ入ります。
    if (length <= 0.0001f) return fallback;
    direction = {direction.x/length, 0.0f, direction.z/length};
    if (direction.x*fallback.x + direction.z*fallback.z < 0.0f)
        direction = {-direction.x, 0.0f, -direction.z};
    return direction;
}
void SetCameraPose(Camera* camera, const engine::Vector3& eye, const engine::Vector3& target) {
    const auto rotation = LookAt(eye, target);
    camera->SetInputEnabled(false);
    camera->SetFollowTarget(nullptr);
    camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    camera->ConfigFixedPoint(eye, rotation);
    // ConfigFixedPoint は設定値の保存だけなので、追従対象のないカメラにも現在姿勢を直接渡します。
    camera->SetEye(eye);
    camera->SetTarget(target);
    camera->SetRotation(rotation);
    camera->Update(0.0f);
}
}

struct GateEntryPresentation::State {
    struct MaterialState {
        engine::Object3d* object = nullptr;
        MeshRenderer::MaterialData material{};
        engine::Vector4 color{};
        BlendMode blend = BlendMode::kNormal;
        int materialType = 0;
        float emissive = 0.0f;
        bool hasMaterial = false;
        bool visible = true;
    };
    Player* player = nullptr;
    engine::Object3d* gate = nullptr;
    Camera* camera = nullptr;
    engine::Object3d* cameraFollow = nullptr;
    Camera::FollowMode cameraMode = Camera::FollowMode::kAimable;
    bool cameraInput = true;
    engine::Vector3 start{}, scale{}, rotation{}, direction{};
    engine::Vector3 eye{}, target{}, cameraRotation{};
    bool visible = true;
    bool useModelCenter = false;
    bool manageBars = true;
    float baseBarHeight = 0.0f;
    float timer = 0.0f;
    std::vector<MaterialState> materials;
};

GateEntryPresentation::GateEntryPresentation() = default;
GateEntryPresentation::~GateEntryPresentation() = default;
bool GateEntryPresentation::IsActive() const { return state_ != nullptr; }
bool GateEntryPresentation::IsFinished() const { return state_ && state_->timer >= kDuration; }
float GateEntryPresentation::GetProgress() const { return state_ ? Clamp(state_->timer/kDuration) : 0.0f; }

bool GateEntryPresentation::Start(Player* player, engine::Object3d* gate, bool manageCinemaBars) {
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (state_ || !player || !gate || !camera || player->GetHp() <= 0.0f || player->IsCinematicLocked() ||
        player->IsGateReturnAnimationActive())
        return false;
    auto state = std::make_unique<State>();
    state->player = player;
    state->gate = ResolveAnchor(gate);
    state->useModelCenter = state->gate != gate;
    state->camera = camera;
    state->start = player->GetWorldPosition();
    state->scale = player->GetScale();
    state->rotation = player->GetRotation();
    state->visible = player->GetIsVisible();
    state->direction = Direction(state->gate, state->start, state->useModelCenter);
    state->cameraFollow = camera->GetFollowTarget();
    state->cameraMode = camera->GetFollowMode();
    state->cameraInput = camera->IsInputEnabled();
    state->eye = camera->GetEye();
    state->target = camera->GetTargetPoint();
    state->cameraRotation = camera->GetRotation();
    state->manageBars = manageCinemaBars;
    state->baseBarHeight = PostEffect::GetInstance()->GetParams()->cinemaBarHeight;

    // 衝突フレーム内で操作・被弾・物理を止め、コピー形態はそのままゲートへ入れます。
    player->BeginCinematicLock(true);
    std::vector<engine::Object3d*> pending{player};
    while (!pending.empty()) {
        auto* object = pending.back();
        pending.pop_back();
        State::MaterialState saved;
        saved.object = object;
        saved.color = object->GetColor();
        saved.blend = object->GetBlendMode();
        saved.emissive = object->GetEmissive();
        saved.materialType = object->GetMaterialType();
        saved.visible = object->GetIsVisible();
        if (auto* material = object->GetMaterialData()) {
            saved.material = *material;
            saved.hasMaterial = true;
        }
        state->materials.push_back(saved);
        for (auto* child : object->GetChildren()) if (child) pending.push_back(child);
    }
    state_ = std::move(state);
    Update(0.0f);
    return true;
}

void GateEntryPresentation::Update(float deltaTime) {
    if (!state_) return;
    auto& s = *state_;
    s.timer = (std::min)(kDuration, s.timer + (std::max)(deltaTime, 0.0f));
    const float t = GetProgress();
    const auto gatePos = PortalPosition(s.gate,s.useModelCenter);
    const auto surface = gatePos - s.direction * 1.18f;
    const auto inside = gatePos + s.direction * 1.38f;
    auto centeredStart = s.start;
    centeredStart.y += (surface.y-s.start.y)*EaseInOut((t-0.06f)/0.36f);
    const auto alignedStart = Lerp(centeredStart, surface, EaseOut(s.timer/0.28f));
    auto a = Lerp(surface, gatePos, 0.42f);
    auto b = Lerp(gatePos, inside, 0.58f);
    a.y += 0.18f;
    b.y += 0.18f*0.35f;
    const float dive = EaseInOut((t-0.18f)/(0.88f-0.18f));
    const float u = 1.0f-dive;
    const auto divePos = surface*(u*u*u) + a*(3.0f*u*u*dive) + b*(3.0f*u*dive*dive) + inside*(dive*dive*dive);
    s.player->SetTranslate(Lerp(alignedStart, divePos, Clamp((t-0.08f)/0.28f)));
    s.player->SetScale(s.scale);
    s.player->SetMoveYaw(std::atan2(s.direction.x,s.direction.z));
    s.player->SetSlimeAnimationDirection(s.direction);
    s.player->SetVelocity({0.0f,0.0f,0.0f});
    s.player->SetIsControlActive(false);

    const float clipT = EaseInOut((t-0.10f)/(0.74f-0.10f));
    const float clip = Clamp(0.14f + EaseOut(clipT)*0.86f);
    const float edgePeak = 1.0f-std::abs(clip*2.0f-1.0f);
    const float fade = EaseInOut((clip-0.72f)/0.28f);
    const float heat = Clamp(0.08f+clip*0.18f+edgePeak*0.30f);
    for (const auto& saved : s.materials) {
        auto* object = saved.object;
        object->SetMaterialType(4);
        object->SetBlendMode(BlendMode::kNormal);
        object->SetEmissive((std::max)(saved.emissive,1.25f+clip*0.95f+edgePeak*1.45f));
        object->SetColor({saved.color.x*(1.0f-heat)+heat, saved.color.y*(1.0f-heat)+0.76f*heat,
                          saved.color.z*(1.0f-heat)+0.34f*heat, 1.0f-fade});
        object->SetIsVisible(saved.visible && clipT < 0.995f && t < 0.94f);
        if (auto* material = object->GetMaterialData()) {
            material->portalClipEnabled = 1.0f;
            material->portalClipProgress = clip;
            material->portalClipCenter = gatePos;
            material->portalClipNormal = s.direction;
            material->portalClipEdgeWidth = 0.30f-0.16f*clip;
            material->portalClipDissolve = 0.74f-0.18f*clip;
            material->portalClipColor = {1.0f,0.66f,0.26f,0.94f};
        }
    }
    s.player->UpdateLocalMatrix();
    s.player->UpdateWorldMatrix(true);
    const engine::Vector3 focus{gatePos.x,gatePos.y+0.92f,gatePos.z};
    const engine::Vector3 endEye{focus.x-s.direction.x*14.25f,focus.y+0.58f,focus.z-s.direction.z*14.25f};
    const float cameraT = EaseInOut(s.timer/0.34f);
    SetCameraPose(s.camera,Lerp(s.eye,endEye,cameraT),Lerp(s.target,focus,cameraT));
    s.player->RefreshRenderCameraData();
    if (s.manageBars) {
        const float height = (std::max)(s.baseBarHeight,0.105f);
        PostEffect::GetInstance()->GetParams()->cinemaBarHeight = s.baseBarHeight + (height-s.baseBarHeight)*Clamp(s.timer*5.8f);
    }
}

void GateEntryPresentation::Reset() {
    if (!state_) return;
    auto& s = *state_;
    s.player->EndCinematicLock(true);
    s.player->SetTranslate(s.start);
    s.player->SetScale(s.scale);
    s.player->SetRotation(s.rotation);
    s.player->SetIsVisible(s.visible);
    for (const auto& saved : s.materials) {
        auto* object = saved.object;
        object->SetMaterialType(saved.materialType);
        object->SetBlendMode(saved.blend);
        object->SetEmissive(saved.emissive);
        object->SetColor(saved.color);
        object->SetIsVisible(saved.visible);
        if (saved.hasMaterial) if (auto* material = object->GetMaterialData()) *material = saved.material;
    }
    s.player->UpdateLocalMatrix();
    s.player->UpdateWorldMatrix(true);
    s.camera->ConfigFixedPoint(s.eye,s.cameraRotation);
    s.camera->SetEye(s.eye);
    s.camera->SetTarget(s.target);
    s.camera->SetRotation(s.cameraRotation);
    s.camera->SetFollowTarget(s.cameraFollow);
    s.camera->SetFollowMode(s.cameraMode);
    s.camera->SetInputEnabled(s.cameraInput);
    s.player->RefreshRenderCameraData();
    if (s.manageBars) PostEffect::GetInstance()->GetParams()->cinemaBarHeight = s.baseBarHeight;
    state_.reset();
}
