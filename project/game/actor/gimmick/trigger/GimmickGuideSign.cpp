#include "GimmickGuideSign.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "ObjectPropertyDefinition.h"
#include "Player.h"
#include "SceneManager.h"
#include "WinApp.h"
#include "engine/graphics/2d/SpriteLayoutAnimation.h"
#include "engine/graphics/2d/SpriteLayoutVideo.h"
#include "engine/graphics/2d/SpriteCommon.h"
#include "game/system/scene/SceneGameplayAccess.h"
#include "game/ui/SpriteLayoutFactory.h"

#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

struct GimmickGuideSign::Layout {
    struct Item {
        std::unique_ptr<engine::Sprite> sprite;
        SpriteLayoutAnimation::Settings animation;
        engine::Vector2 position; // JSONの設計ピクセルでの基準位置。
        engine::Vector2 size; // JSONの設計ピクセルでの基準サイズ。
    };
    std::vector<Item> items;
    engine::Vector2 canvas;
    std::filesystem::path videoPath;
    std::unique_ptr<VideoSpritePlayer> video;
    engine::Sprite* videoSprite = nullptr;
    engine::SpriteCommon* common = nullptr;
    uint32_t poster = 0;
    bool videoOpen = false;

    void CloseVideo() {
        if (!videoOpen)
            return;
        video->Close();
        SpriteLayoutVideo::SetTexture(*videoSprite, poster);
        videoOpen = false;
    }
};

GimmickGuideSign::GimmickGuideSign() = default;
GimmickGuideSign::~GimmickGuideSign() = default;

void GimmickGuideSign::Initialize(engine::Object3DCommon* common, const std::string& modelName) {
    // 看板は道を塞がず、プレイヤーとの距離はUpdateで調べます。
    BaseGimmick::Initialize(common, modelName);
    SetGimmickType(kTypeId);
    SetName("Gimmick_GuideSign");
    SetColliderType(ColliderType::None);
    SetCollisionAttribute(0);
    SetCollisionMask(0);

    engine::ObjectPropertyDefinition<GimmickGuideSign> properties(*this, kTypeId, "案内看板",
        "近づくとSprite Editorで作ったレイアウトを看板の上に表示します。");
    properties.Expose("レイアウトJSON", "layoutPath", &GimmickGuideSign::layoutPath_, 0, 0, nullptr,
        {.assetDirectory = "Resources/json/sprite"});
    properties.Expose("反応距離", "activationRadius", &GimmickGuideSign::activationRadius_, 0.25f, 30.0f);
    properties.Expose("表示位置の高さ", "displayHeight", &GimmickGuideSign::displayHeight_, 0.0f, 20.0f);
    properties.Expose("表示の横ずれ（画面幅比）", "displayOffsetX", &GimmickGuideSign::displayOffsetX_, -0.35f, 0.35f);
    properties.Expose("画面に対する表示幅", "displayWidth", &GimmickGuideSign::displayWidth_, 0.06f, 0.6f);
}

void GimmickGuideSign::Update(float deltaTime) {
    BaseGimmick::Update(deltaTime);
    auto* manager = SceneManager::GetInstance();
    auto* scene = manager && manager->IsPlaying() ? manager->GetCurrentScene() : nullptr;
    const auto* player = GetScenePlayer(scene);

    // 範囲内の最初のフレームを0秒にし、次回接近で途中のコマを持ち越さないようにします。
    if (!player || !GetIsVisible()) {
        if (layout_)
            layout_->CloseVideo();
        inRange_ = false;
        elapsed_ = 0.0;
        return;
    }
    const auto offset = player->GetWorldPosition() - GetWorldPosition();
    const float radius = std::clamp(activationRadius_, 0.25f, 30.0f);
    const bool nearby = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z <= radius * radius;
    elapsed_ = nearby && inRange_ ? elapsed_ + (std::max)(0.0f, deltaTime) : 0.0;
    inRange_ = nearby;
    if (!nearby && layout_)
        layout_->CloseVideo();

    // 接近した看板の素材だけを作り、選択したJSONが変わったフレームで入れ替えます。
    if (inRange_ && layoutPath_ != attemptedPath_) {
        LoadLayout(scene->GetSpriteCommon());
        elapsed_ = 0.0;
    }
    if (nearby && layout_ && layout_->videoOpen)
        layout_->video->Update(deltaTime);
}

void GimmickGuideSign::LoadLayout(engine::SpriteCommon* common) {
    attemptedPath_ = layoutPath_;
    layout_.reset();
    if (!common || layoutPath_.empty())
        return;

    try {
        const std::filesystem::path path(std::u8string(layoutPath_.begin(), layoutPath_.end()));
        const auto root = std::filesystem::canonical("Resources/json/sprite");
        const auto resolved = std::filesystem::weakly_canonical(path);
        const auto local = resolved.lexically_relative(root);
        if (local.empty() || local.is_absolute() || *local.begin() == L".." || resolved.extension() != L".json")
            return;

        nlohmann::json data;
        std::ifstream file(resolved);
        if (!(file >> data) || !data.is_object() || !data.contains("sprites") || !data["sprites"].is_array())
            return;

        // World UIの幅に合わせるため、Spriteは元の設計ピクセルで生成して親子関係を復元します。
        auto layout = std::make_unique<Layout>();
        layout->canvas = SpriteLayoutScaler::Make(data).designResolution;
        layout->common = common;
        std::unordered_map<std::string, engine::Sprite*> byName;
        std::vector<std::pair<engine::Sprite*, std::string>> parents;
        for (const auto& row : data["sprites"]) {
            if (!row.is_object())
                return;
            auto sprite = SpriteLayoutFactory::Create(common, row, {}, "sprite");
            if (!byName.emplace(sprite->GetName(), sprite.get()).second)
                return;
            sprite->SetRotation(SpriteLayoutScaler::ReadFloat(row, "rotation", 0.0f));
            sprite->SetVisible(true);
            parents.emplace_back(sprite.get(), SpriteLayoutScaler::ReadString(row, "parentName", ""));
            // 転送先は図鑑とも共用するため、レイアウト内では最初の有効な動画一つを再生します。
            if (!layout->videoSprite) {
                auto path = SpriteLayoutVideo::Resolve(SpriteLayoutScaler::ReadString(row, "video", ""));
                if (!path.empty()) {
                    layout->videoPath = std::move(path);
                    layout->videoSprite = sprite.get();
                    layout->poster = sprite->GetTextureHandle();
                }
            }
            const auto pos = sprite->GetPosition();
            const auto size = sprite->GetSize();
            layout->items.push_back({std::move(sprite), SpriteLayoutAnimation::Read(row), pos, size});
        }
        for (const auto& [sprite, name] : parents) {
            if (name.empty())
                continue;
            const auto parent = byName.find(name);
            if (parent == byName.end())
                return;
            sprite->SetParent(parent->second, false);
            if (sprite->GetParent() != parent->second)
                return;
        }
        layout_ = std::move(layout);
    } catch (const std::exception&) {
        // 無効なJSONでも看板モデルは残し、表示だけを止めます。
    }
}

void GimmickGuideSign::DrawGuide(engine::Camera* camera) {
    if (!layout_)
        return;
    if (!inRange_ || !camera || !GetIsVisible()) {
        layout_->CloseVideo();
        return;
    }

    // カメラの背後に回った看板は、透視除算で画面の反対側に表示させません。
    auto anchor = GetWorldPosition();
    anchor.y += displayHeight_;
    if (engine::Math::Transform(anchor, camera->GetViewMatrix()).z <= 0.001f) {
        layout_->CloseVideo();
        return;
    }
    const auto ndc = engine::Math::Transform(anchor, camera->GetViewProjectionMatrix());
    if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f) {
        layout_->CloseVideo();
        return;
    }

    // 実際に画面内へ出た時だけ先頭から再生し、デコード中は静止画を使います。
    if (layout_->videoSprite) {
        if (!layout_->videoOpen) {
            if (!layout_->video)
                layout_->video = std::make_unique<VideoSpritePlayer>();
            layout_->video->RequestOpen(layout_->videoPath);
            layout_->videoOpen = true;
        }
        auto* commands = layout_->common->GetDxCommon()->GetCommandList();
        if (const auto frame = layout_->video->PrepareTextureForDraw(*commands))
            SpriteLayoutVideo::SetTexture(*layout_->videoSprite, *frame);
    }

    // キャンバスの下辺中央を看板の上に合わせ、縦横比を維持します。
    const float width = static_cast<float>(WinApp::GetClientWidth());
    const float height = static_cast<float>(WinApp::GetClientHeight());
    const float scale = width * std::clamp(displayWidth_, 0.06f, 0.6f) / layout_->canvas.x;
    const float canvasWidth = layout_->canvas.x * scale;
    const float anchoredX = ((ndc.x + 1.0f) * 0.5f + std::clamp(displayOffsetX_, -0.35f, 0.35f)) * width;
    // 横にずらしても、画面端で見出しが欠けない位置に収めます。
    const engine::Vector2 origin{std::clamp(anchoredX - canvasWidth * 0.5f, 0.0f, width - canvasWidth),
                                (1.0f - ndc.y) * height * 0.5f - layout_->canvas.y * scale};
    for (const auto& item : layout_->items) {
        auto& sprite = *item.sprite;
        engine::Vector2 position{item.position.x * scale, item.position.y * scale};
        if (!sprite.GetParent()) {
            position.x += origin.x;
            position.y += origin.y;
        }
        sprite.SetPosition(position);
        sprite.SetSize({item.size.x * scale, item.size.y * scale});
        if (&sprite != layout_->videoSprite)
            SpriteLayoutAnimation::Apply(sprite, item.animation, elapsed_);
    }
    for (const auto& item : layout_->items) {
        item.sprite->Update();
        item.sprite->Draw();
    }
}

std::unique_ptr<engine::Object3D> GimmickGuideSign::Clone() const {
    auto clone = std::make_unique<GimmickGuideSign>();
    clone->Initialize(GetObjectCommon(), GetModelName());
    clone->CopyFrom(this);
    clone->layoutPath_ = layoutPath_;
    clone->activationRadius_ = activationRadius_;
    clone->displayHeight_ = displayHeight_;
    clone->displayWidth_ = displayWidth_;
    clone->displayOffsetX_ = displayOffsetX_;
    return clone;
}
