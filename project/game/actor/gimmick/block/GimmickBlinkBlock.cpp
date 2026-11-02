#include "GimmickBlinkBlock.h"
#include "Easing.h"
#include <numbers>
#include "ObjectPropertyDefinition.h"
#include "CollisionConfig.h"
#include "Model.h"
#include "ModelManager.h"
#include "Player.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = std::numbers::pi_v<float>;
constexpr int kFlipFrameCount = 24;
constexpr const char* kFlipModelPrefix = "Stages/jump_flip_pair/jump_flip_pair_";

std::string FlipFrameModelName(int frame) {
    if (frame <= 0)
        return std::string(kFlipModelPrefix) + "blue_preview.obj";
    if (frame >= kFlipFrameCount)
        return std::string(kFlipModelPrefix) + "red_preview.obj";
    return std::string(kFlipModelPrefix) + "flip_" + (frame < 10 ? "0" : "") +
           std::to_string(frame) + ".obj";
}

ColliderConfig BuildFlipPairCollider(const Model* model) {
    // OBJのZ座標は読込時に反転するため、描画に使う変換済み頂点から板の位置を得る。
    // 固定枠・軸・模様より大きい、上向きの水平三角形が回転板の天面に相当する。
    ColliderConfig config;
    config.type = ColliderType::kNone;
    if (!model)
        return config;

    float largestArea = 0.0f;
    engine::Vector3 minimum{}, maximum{};
    for (const auto& mesh : model->GetModelData().meshes) {
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const auto& a = mesh.vertices[mesh.indices[i]];
            const auto& b = mesh.vertices[mesh.indices[i + 1]];
            const auto& c = mesh.vertices[mesh.indices[i + 2]];
            const float lowY = (std::min)({a.position.y, b.position.y, c.position.y});
            const float highY = (std::max)({a.position.y, b.position.y, c.position.y});
            if (highY - lowY > 0.001f || a.normal.y + b.normal.y + c.normal.y < 2.7f)
                continue;
            const float area = std::abs((b.position.x - a.position.x) * (c.position.z - a.position.z) -
                                        (b.position.z - a.position.z) * (c.position.x - a.position.x));
            if (area <= largestArea)
                continue;
            largestArea = area;
            minimum = {(std::min)({a.position.x, b.position.x, c.position.x}), lowY,
                       (std::min)({a.position.z, b.position.z, c.position.z})};
            maximum = {(std::max)({a.position.x, b.position.x, c.position.x}), highY,
                       (std::max)({a.position.z, b.position.z, c.position.z})};
        }
    }
    if (largestArea <= 0.0f)
        return config;

    // 天面上の薄い色面や模様の高さも取り込み、見た目に足が埋まらないようにする。
    const float deckPlaneY = maximum.y;
    for (const auto& mesh : model->GetModelData().meshes) {
        for (const auto& vertex : mesh.vertices) {
            const auto& p = vertex.position;
            if (p.x >= minimum.x && p.x <= maximum.x && p.z >= minimum.z && p.z <= maximum.z &&
                p.y >= deckPlaneY && p.y <= deckPlaneY + 0.1f)
                maximum.y = (std::max)(maximum.y, p.y);
        }
    }
    config.type = ColliderType::kOBB;
    config.center = {(minimum.x + maximum.x) * 0.5f, maximum.y - 0.125f,
                     (minimum.z + maximum.z) * 0.5f};
    config.size = {(maximum.x - minimum.x) * 0.5f, 0.125f, (maximum.z - minimum.z) * 0.5f};
    return config;
}
} // namespace

GimmickBlinkBlock::~GimmickBlinkBlock() {
    EventManager::GetInstance()->Unsubscribe(jumpSubscription_);
}

void GimmickBlinkBlock::Initialize(engine::Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);
    SetClassName("Gimmick");
    SetGimmickType("BlinkBlock");

    // イベント購読
    EventManager::GetInstance()->Unsubscribe(jumpSubscription_);
    jumpSubscription_ = EventManager::GetInstance()->Subscribe([this](const PlayerJumpEvent& e) { OnPlayerJump(e); });

    // 初期パラメータの読み込み
    if (GetParameters().has_value()) {
        colorType_ = GetParameters()->colorType;
    }

    // 初期状態の反映（ジャンプ0回目を基準にする）
    isActive_ = (0 % 2 == colorType_);
    UpdateAppearance();

    // 編集項目の宣言です。動作処理と旧paramの保存キーは変更しません。
    using Self = GimmickBlinkBlock;
    engine::ObjectPropertyDefinition<Self> properties(*this, kTypeId, "点滅・ジャンプ反転ブロック",
        "反転板モデルは枠の中央を軸に回転し、着地側の板だけが床になります。通常モデルはジャンプ回数で出現面を切り替えます。");
    using Parameter = engine::Object3d::EntityParameter;
    properties.Parameter("初期の色・反転面", "colorType", &Parameter::colorType, 0.0f, 1.0f,
        nullptr, {"青（ジャンプ偶数回）", "赤（ジャンプ奇数回）"});
    properties.Parameter("反転速度倍率", "speed", &Parameter::speed, 0.25f, 3.0f,
        +[](const Self& object) { return object.GetModelName().rfind("Stages/jump_flip_pair/jump_flip_pair_", 0) == 0; });
}

void GimmickBlinkBlock::Update(float deltaTime) {
    if (IsFlipPairModel()) {
        UpdateFlipPair(deltaTime);
        BaseGimmick::Update(deltaTime);
        return;
    }

    // Scene JSONはInitialize後に適用されるため、初回Updateで色種別と初期ON/OFFを同期する。
    // 青(0)は既定値と一致して変更検知だけでは再描画されないので、見た目は毎フレーム確定する。
    if (GetParameters().has_value() && GetParameters()->colorType != colorType_) {
        colorType_ = GetParameters()->colorType;
        isActive_ = (colorType_ == 0);
    }

    UpdateAppearance();
    BaseGimmick::Update(deltaTime);
}

void GimmickBlinkBlock::OnPlayerJump(const PlayerJumpEvent& event) {
    if (event.player && IsFlipPairModel()) {
        if (!flipPairInitialized_)
            ResetFlipPair();
        if (const SceneManager* sceneManager = SceneManager::GetInstance())
            flipPairWasPlaying_ = sceneManager->IsPlaying();

        // 連続ジャンプ時は現在の板角度から目標へ向かい直し、角度の飛びを防ぐ。
        const int targetSide = (static_cast<int>(event.player->GetJumpCount() & 1u) ^ flipPairInitialSide_);
        const float targetAngle = targetSide == 0 ? 0.0f : kPi;
        if (std::abs(targetAngle - flipTargetAngle_) > 0.001f) {
            flipFromAngle_ = flipAngle_;
            flipTargetAngle_ = targetAngle;
            flipElapsed_ = 0.0f;
            const float speed = std::clamp(GetParameters().has_value() ? GetParameters()->speed : 1.0f, 0.25f, 3.0f);
            flipDuration_ = (std::max)(0.16f, 0.48f * std::abs(flipTargetAngle_ - flipFromAngle_) / kPi / speed);
            // 移動先の板はジャンプ開始時から着地可能にする。アニメーション中に床判定を消すと、
            // 着地が先行したフレームでプレイヤーが板を通り抜けてしまう。
            ApplyFlipPairAppearance();
        }
        return;
    }

    // プレイヤーのジャンプ回数(0, 1, 2...)に合わせて状態を切り替える
    // 0回目: 青ON/赤OFF, 1回目: 青OFF/赤ON ... という同期をとる
    if (event.player) {
        uint32_t count = event.player->GetJumpCount();
        bool shouldBeActive = (count % 2 == colorType_);

        if (isActive_ != shouldBeActive) {
            isActive_ = shouldBeActive;
            UpdateAppearance();
        }
    }
}

bool GimmickBlinkBlock::IsFlipPairModel() const {
    return GetModelName().rfind(kFlipModelPrefix, 0) == 0;
}

void GimmickBlinkBlock::ResetFlipPair() {
    if (!flipPairInitialized_) {
        // 編集画面で初めて表示する前に全コマを読み込み、再生中の読み込み停止を避ける。
        for (int frame = 0; frame <= kFlipFrameCount; ++frame)
            ModelManager::GetInstance()->LoadModel(FlipFrameModelName(frame));
        flipPairColliders_[0] = BuildFlipPairCollider(ModelManager::GetInstance()->LoadModel(FlipFrameModelName(0)));
        flipPairColliders_[1] = BuildFlipPairCollider(ModelManager::GetInstance()->LoadModel(FlipFrameModelName(kFlipFrameCount)));
    }
    flipPairInitialSide_ = GetParameters().has_value() && GetParameters()->colorType == 1 ? 1 : 0;
    flipAngle_ = flipPairInitialSide_ == 0 ? 0.0f : kPi;
    flipFromAngle_ = flipAngle_;
    flipTargetAngle_ = flipAngle_;
    flipElapsed_ = 0.0f;
    flipFrame_ = -1;
    flipPairInitialized_ = true;
    ApplyFlipPairAppearance();
}

void GimmickBlinkBlock::UpdateFlipPair(float deltaTime) {
    const SceneManager* sceneManager = SceneManager::GetInstance();
    const bool playing = sceneManager && sceneManager->IsPlaying();
    const int configuredSide = GetParameters().has_value() && GetParameters()->colorType == 1 ? 1 : 0;
    if (!flipPairInitialized_ || playing != flipPairWasPlaying_ ||
        (!playing && configuredSide != flipPairInitialSide_)) {
        ResetFlipPair();
    }
    flipPairWasPlaying_ = playing;

    if (playing && std::abs(flipTargetAngle_ - flipFromAngle_) > 0.001f) {
        flipElapsed_ = (std::min)(flipElapsed_ + (std::max)(deltaTime, 0.0f), flipDuration_);
        const float t = std::clamp(flipElapsed_ / flipDuration_, 0.0f, 1.0f);
        const float eased = Easing::SmoothStep(t);
        flipAngle_ = flipFromAngle_ + (flipTargetAngle_ - flipFromAngle_) * eased;
        if (t >= 1.0f)
            flipFromAngle_ = flipTargetAngle_;
    }

    ApplyFlipPairAppearance();
}

void GimmickBlinkBlock::ApplyFlipPairAppearance() {
    // 枠は全コマに共通で静止し、中央のX軸を中心とした板だけが半回転する。
    const int frame = std::clamp(static_cast<int>(std::lround(flipAngle_ / kPi * kFlipFrameCount)),
                                 0, kFlipFrameCount);
    if (frame != flipFrame_) {
        SetModel(FlipFrameModelName(frame));
        flipFrame_ = frame;
    }

    SetIsVisible(true);
    SetColor({1.0f, 1.0f, 1.0f, 1.0f});

    // 描画の読込座標と一致する端点判定へ切り替える。途中コマの全体AABBは使わない。
    const ColliderConfig& config = flipPairColliders_[flipTargetAngle_ < kPi * 0.5f ? 0 : 1];
    const ColliderConfig& current = GetColliderConfig();
    const bool colliderChanged = current.type != config.type ||
        current.center.x != config.center.x || current.center.y != config.center.y || current.center.z != config.center.z ||
        current.size.x != config.size.x || current.size.y != config.size.y || current.size.z != config.size.z ||
        current.rotation.x != 0.0f || current.rotation.y != 0.0f || current.rotation.z != 0.0f ||
        current.useModelSurface || current.useModelVolume || !current.footprint.empty();
    if (colliderChanged)
        SetColliderConfig(config);

    const uint32_t attribute = config.type == ColliderType::kNone ? 0 : CollisionCategories::Get("ground");
    if (GetCollisionAttribute() != attribute)
        SetCollisionAttribute(attribute);
    const uint32_t mask = CollisionCategories::GetDefaultGameplayMask();
    if (GetCollisionMask() != mask)
        SetCollisionMask(mask);
}

void GimmickBlinkBlock::UpdateAppearance() {
    // isActive_ に基づいて見た目と衝突判定を切り替える
    if (isActive_) {
        SetIsVisible(true);
        SetCollisionAttribute(CollisionCategories::Get("ground"));

        // 不透明
        if (colorType_ == 0) {
            // 青
            SetColor({0.2f, 0.6f, 1.0f, 1.0f});
        } else {
            // 赤
            SetColor({1.0f, 0.3f, 0.3f, 1.0f});
        }
    } else {
        // 非アクティブ時は衝突判定を消し、半透明にする
        SetCollisionAttribute(0);

        if (colorType_ == 0) {
            // 青（半透明）
            SetColor({0.2f, 0.6f, 1.0f, 0.2f});
        } else {
            // 赤（半透明）
            SetColor({1.0f, 0.3f, 0.3f, 0.2f});
        }
    }
}

std::unique_ptr<engine::Object3d> GimmickBlinkBlock::Clone() const {
    // 同じモデルで新しい実体を初期化してから、配置と設定値を複製する。
    auto newObj = std::make_unique<GimmickBlinkBlock>();
    assert(GetObjectCommon() != nullptr);
    newObj->Initialize(GetObjectCommon(), GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
