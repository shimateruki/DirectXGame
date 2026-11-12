#include "ControlsGuideOverlay.h"

#include "engine/graphics/2d/Sprite.h"
#include "engine/graphics/2d/SpriteCommon.h"
#include "engine/graphics/2d/VideoSpritePlayer.h"
#include "engine/graphics/core/DirectXCommon.h"
#include "engine/graphics/core/TextureManager.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
using Json = nlohmann::json;

const Json* FindMove(const Json& book, const std::string& key, int index) {
    if (!book.is_object() || !book.contains(key) || !book[key].is_object())
        return nullptr;
    const auto& entry = book[key];
    if (!entry.contains("moves") || !entry["moves"].is_array() || index < 0 ||
        static_cast<size_t>(index) >= entry["moves"].size())
        return nullptr;
    const auto& move = entry["moves"][index];
    return move.is_object() ? &move : nullptr;
}

std::string ReadAsset(const Json& move, const char* key) {
    const auto it = move.find(key);
    return it != move.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

// 映像と静止画で同じ正規化UVを使用する。不正・未設定の値は全体表示へ戻す。
std::array<float, 4> ReadPreviewCrop(const Json* move) {
    constexpr std::array<float, 4> full{0.0f, 0.0f, 1.0f, 1.0f};
    if (!move || !move->contains("previewCrop"))
        return full;
    const auto& crop = (*move)["previewCrop"];
    if (!crop.is_array() || crop.size() != 4)
        return full;
    auto values = full;
    for (size_t index = 0; index < values.size(); ++index) {
        if (!crop[index].is_number())
            return full;
        values[index] = crop[index].get<float>();
        if (!std::isfinite(values[index]))
            return full;
    }
    if (values[0] < 0.0f || values[1] < 0.0f || values[2] <= 0.0f || values[3] <= 0.0f ||
        values[0] + values[2] > 1.00001f || values[1] + values[3] > 1.00001f)
        return full;
    return values;
}

// ビルドに同梱するローカル素材だけを開き、外部URLやResources外の参照を除外する。
std::filesystem::path ResolveAsset(const std::string& value, bool video) {
    if (value.empty())
        return {};
    const std::filesystem::path input(std::u8string(value.begin(), value.end()));
    if (input.has_root_path())
        return {};
    for (const auto& component : input)
        if (component == "..")
            return {};
    const auto extension = input.extension();
    if (video ? extension != ".mp4" : (extension != ".png" && extension != ".dds"))
        return {};
    std::error_code error;
    const auto root = std::filesystem::canonical(video ? "Resources/video" : "Resources/sprite", error);
    if (error)
        return {};
    const auto file = std::filesystem::canonical(root / input, error);
    if (error || !std::filesystem::is_regular_file(file, error))
        return {};
    const auto relative = file.lexically_relative(root);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
        return {};
    return file;
}

#ifdef USE_IMGUI
std::string ReadJsonBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("read failed");
    return {std::istreambuf_iterator<char>(input), {}};
}

// 編集対象を読み直してから動画・静止画・表示範囲だけ更新し、書きかけのJSONを公開しない。
void SaveMoveAssets(const std::filesystem::path& path, const std::string& key, int index,
                    const Json& selected, const std::string& video, const std::string& poster) {
    const auto original = ReadJsonBytes(path);
    auto latest = Json::parse(original);
    const auto* move = FindMove(latest, key, index);
    if (!move || ReadAsset(*move, "name") != ReadAsset(selected, "name"))
        throw std::runtime_error("move changed");
    auto& target = latest[key]["moves"][index];
    target["previewVideo"] = video;
    target["previewPoster"] = poster;
    target["previewCrop"] = ReadPreviewCrop(&selected);
    auto temporary = path;
    temporary += L".video-preview-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    try {
        std::ofstream output(temporary, std::ios::binary);
        output << latest.dump(2) << '\n';
        output.close();
        if (!output || ReadJsonBytes(path) != original ||
            !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("replace failed");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
#endif
}

void ControlsGuideOverlay::CloseMovePreview() {
    if (videoPlayer_ && !previewMoveKey_.empty())
        videoPlayer_->Close();
    previewMoveKey_.clear();
    previewVideoPath_.clear();
    previewPosterPath_.clear();
    previewPosterHandle_ = 0;
    previewDescriptionHandle_ = 0;
    if (movePreviewSprite_)
        movePreviewSprite_->SetVisible(false);
}

void ControlsGuideOverlay::UpdateMovePreview(float deltaTime) {
    const auto* move = isActive_ && bookVisible_ && !isSliding_
        ? FindMove(bookData_, bookKey_, previewMoveIndex_) : nullptr;
    if (!move) {
        CloseMovePreview();
        return;
    }
    const auto video = ReadAsset(*move, "previewVideo");
    const auto poster = ReadAsset(*move, "previewPoster");
    if (video.empty() && poster.empty()) {
        CloseMovePreview();
        return;
    }
    const auto key = bookKey_ + "/" + std::to_string(previewMoveIndex_);
    if (key != previewMoveKey_ || video != previewVideoPath_ || poster != previewPosterPath_) {
        CloseMovePreview();
        previewMoveKey_ = key;
        previewVideoPath_ = video;
        previewPosterPath_ = poster;
        if (!ResolveAsset(poster, false).empty())
            previewPosterHandle_ = engine::Sprite::LoadTexture(poster);
        const auto path = ResolveAsset(video, true);
        if (!path.empty() || previewPosterHandle_) {
            const std::string description = "ui/control_guide/book/" +
                (bookKey_ == "crown" ? std::string("crown_gate") : bookKey_) +
                "/move_" + std::to_string(previewMoveIndex_) + "_description.png";
            if (!ResolveAsset(description, false).empty())
                previewDescriptionHandle_ = engine::Sprite::LoadTexture(description);
        }
        if (!path.empty() && previewPlaybackEnabled_) {
            if (!videoPlayer_)
                videoPlayer_ = std::make_unique<VideoSpritePlayer>();
            videoPlayer_->RequestOpen(path);
        }
    }
    if (videoPlayer_ && previewPlaybackEnabled_)
        videoPlayer_->Update(deltaTime);
}

void ControlsGuideOverlay::DrawMovePreview() {
    if (movePreviewSprite_)
        movePreviewSprite_->SetVisible(false);
    if (!bookVisible_ || isSliding_ || previewMoveKey_.empty() || !spriteCommon_)
        return;
    auto* directX = spriteCommon_->GetDxCommon();
    auto* commands = directX ? directX->GetCommandList() : nullptr;
    const auto handle = videoPlayer_ && previewPlaybackEnabled_ && commands
        ? videoPlayer_->PrepareTextureForDraw(*commands) : std::nullopt;
    const auto textureHandle = handle.value_or(previewPosterHandle_);
    if (!textureHandle)
        return;
    const auto& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle);
    const float width = static_cast<float>(metadata.width);
    const float height = static_cast<float>(metadata.height);
    const float frameWidth = movePreviewSize_.x * layoutRatio_.x;
    const float frameHeight = movePreviewSize_.y * layoutRatio_.y;
    if (!std::isfinite(frameWidth) || !std::isfinite(frameHeight) ||
        (std::min)({width, height, frameWidth, frameHeight}) <= 0.0f)
        return;
    const auto crop = ReadPreviewCrop(FindMove(bookData_, bookKey_, previewMoveIndex_));
    const engine::Vector2 sourceSize{width * crop[2], height * crop[3]};
    const float scale = (std::min)(frameWidth / sourceSize.x, frameHeight / sourceSize.y);
    const engine::Vector2 size{sourceSize.x * scale, sourceSize.y * scale};
    if (!movePreviewSprite_) {
        movePreviewSprite_ = std::make_unique<engine::Sprite>();
        movePreviewSprite_->Initialize(spriteCommon_, textureHandle);
        movePreviewSprite_->SetAnchorPoint({0.0f, 0.0f});
    }
    movePreviewSprite_->SetTextureHandle(textureHandle);
    movePreviewSprite_->SetTextureRect({width * crop[0], height * crop[1]}, sourceSize);
    movePreviewSprite_->SetPosition({movePreviewPosition_.x * layoutRatio_.x + bookOffsetX_ + (frameWidth - size.x) * 0.5f,
                                    movePreviewPosition_.y * layoutRatio_.y + (frameHeight - size.y) * 0.5f});
    movePreviewSprite_->SetSize(size);
    movePreviewSprite_->SetVisible(true);
    movePreviewSprite_->Update();
    movePreviewSprite_->Draw();
    const bool hasLayoutDescription = std::any_of(bookElements_.begin(), bookElements_.end(), [](const BookElement& item) {
        return item.slot.rfind("selected_move_", 0) == 0 && item.slot.find("_description") != std::string::npos;
    });
    // 詳細欄がJSONにある場合はそちらを使い、旧レイアウトだけ従来の自動配置を残す。
    if (previewDescriptionHandle_ && !hasLayoutDescription) {
        const auto& description = TextureManager::GetInstance()->GetMetadata(previewDescriptionHandle_);
        if (!description.width || !description.height)
            return;
        if (!movePreviewDescription_) {
            movePreviewDescription_ = std::make_unique<engine::Sprite>();
            movePreviewDescription_->Initialize(spriteCommon_, previewDescriptionHandle_);
            movePreviewDescription_->SetAnchorPoint({0.0f, 0.0f});
        }
        const engine::Vector2 sourceSize{static_cast<float>(description.width), static_cast<float>(description.height)};
        const float descriptionScale = (std::min)(frameWidth / sourceSize.x, 1.0f);
        movePreviewDescription_->SetTextureHandle(previewDescriptionHandle_);
        movePreviewDescription_->SetTextureRect({0.0f, 0.0f}, sourceSize);
        movePreviewDescription_->SetSize({sourceSize.x * descriptionScale, sourceSize.y * descriptionScale});
        movePreviewDescription_->SetPosition({movePreviewPosition_.x * layoutRatio_.x + bookOffsetX_,
                                              (movePreviewPosition_.y + movePreviewSize_.y + 22.0f) * layoutRatio_.y});
        movePreviewDescription_->SetVisible(true);
        movePreviewDescription_->Update();
        movePreviewDescription_->Draw();
    }
}

void ControlsGuideOverlay::DrawMovePreviewEditor() {
#ifdef USE_IMGUI
    if (!ImGui::TreeNode("技の映像プレビュー"))
        return;
    const auto* move = FindMove(bookData_, bookKey_, previewMoveIndex_);
    if (ImGui::BeginCombo("編集する技", move ? ReadAsset(*move, "name").c_str() : "なし")) {
        for (int index = 0; index < bookMoveCount_; ++index) {
            const auto* candidate = FindMove(bookData_, bookKey_, index);
            if (candidate && ImGui::Selectable(ReadAsset(*candidate, "name").c_str(), previewMoveIndex_ == index))
                previewMoveIndex_ = index;
        }
        ImGui::EndCombo();
    }
    ImGui::DragFloat2("映像枠の左上", &movePreviewPosition_.x, 1.0f);
    ImGui::DragFloat2("映像枠のサイズ", &movePreviewSize_.x, 1.0f, 1.0f, 4000.0f);
    if (ImGui::Checkbox("映像を再生", &previewPlaybackEnabled_))
        CloseMovePreview();
    ImGui::TextWrapped("MP4はResources/videoから、静止画はResources/spriteからの相対パス。映像は640×360、無音ループです。枠は下のレイアウト保存で保存します。");
    move = FindMove(bookData_, bookKey_, previewMoveIndex_);
    if (move) {
        const auto key = bookKey_ + "/" + std::to_string(previewMoveIndex_);
        if (key != previewEditorMove_) {
            previewEditorMove_ = key;
            const auto fill = [](auto& buffer, const std::string& value) {
                buffer.fill(0);
                std::copy_n(value.data(), (std::min)(value.size(), buffer.size() - 1), buffer.data());
            };
            fill(previewVideoInput_, ReadAsset(*move, "previewVideo"));
            fill(previewPosterInput_, ReadAsset(*move, "previewPoster"));
        }
        auto crop = ReadPreviewCrop(move);
        if (ImGui::DragFloat4("映像の範囲 (X / Y / 幅 / 高さ)", crop.data(), 0.005f, 0.0f, 1.0f)) {
            crop[0] = std::clamp(crop[0], 0.0f, 0.95f);
            crop[1] = std::clamp(crop[1], 0.0f, 0.95f);
            crop[2] = std::clamp(crop[2], 0.05f, 1.0f - crop[0]);
            crop[3] = std::clamp(crop[3], 0.05f, 1.0f - crop[1]);
            bookData_[bookKey_]["moves"][previewMoveIndex_]["previewCrop"] = crop;
        }
        ImGui::TextWrapped("範囲は0〜1。0,0,1,1で全体表示。映像設定の保存でこの技の範囲も保存します。");
        ImGui::InputText("動画", previewVideoInput_.data(), previewVideoInput_.size());
        ImGui::InputText("静止画", previewPosterInput_.data(), previewPosterInput_.size());
        const bool apply = ImGui::Button("試しに表示");
        ImGui::SameLine();
        const bool save = ImGui::Button("この技の映像設定を保存");
        if (apply || save) {
            const std::string video = previewVideoInput_.data();
            const std::string poster = previewPosterInput_.data();
            if ((!video.empty() && ResolveAsset(video, true).empty()) ||
                (!poster.empty() && ResolveAsset(poster, false).empty())) {
                editorStatus_ = "素材が見つからないか、パス・拡張子が対応範囲外です。";
            } else {
                try {
                    if (save) {
                        const std::filesystem::path path(std::u8string(previewBookPath_.begin(), previewBookPath_.end()));
                        SaveMoveAssets(path, bookKey_, previewMoveIndex_, *move, video, poster);
                    }
                    auto& target = bookData_[bookKey_]["moves"][previewMoveIndex_];
                    target["previewVideo"] = video;
                    target["previewPoster"] = poster;

                    CloseMovePreview();
                    editorStatus_ = save ? "この技の映像設定を保存しました。" : "プレビューに反映しました。";
                } catch (...) {
                    editorStatus_ = "映像設定を保存できませんでした。元の技データを確認してください。";
                }
            }
        }
        if (videoPlayer_ && videoPlayer_->HasError())
            ImGui::TextWrapped("動画を再生できません。640×360のMP4か確認してください。");
    }
    ImGui::TreePop();
#endif
}
