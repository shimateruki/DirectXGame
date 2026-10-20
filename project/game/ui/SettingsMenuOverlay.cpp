#define NOMINMAX
#include "SettingsMenuOverlay.h"
#include "game/system/settings/GameSettingsManager.h"
#include "InputManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "DirectXCommon.h"
#include "Log.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <utility>

namespace {
constexpr int kVisibleRows = 3;
constexpr engine::Vector4 kInk{.07f, .17f, .21f, 1};

// ポインターが半開区間の矩形内にあるか判定し、隣接項目の境界で二重選択を防ぎます。
bool Inside(engine::Vector2 p, float x, float y, float w, float h) {
    // 右端と下端を含めず、隣接する操作領域への二重ヒットを避ける。
    return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
}

WinApp* Window() {
    // 表示モードとカーソル制御は描画に使用中のウィンドウへ適用する。
    return DirectXCommon::GetInstance()->GetWinApp();
}

RECT previousCursorClip{};
bool previousCursorVisible = true;
} // namespace

SettingsMenuOverlay::SettingsMenuOverlay() = default;
SettingsMenuOverlay::~SettingsMenuOverlay() {
    // メニューを開いたまま破棄してもカーソル状態を戻す。
    Finalize();
}

void SettingsMenuOverlay::Initialize(engine::SpriteCommon* common, const std::string& layoutPath) {
    common_ = common;
    input_ = engine::InputManager::GetInstance();
    layoutSprites_.clear();
    labelTextures_.clear();

    // 画像・配置とクリック領域の共通データを一度だけ読み取る。
    try {
        std::ifstream file(layoutPath);
        nlohmann::json j;

        if (!(file >> j) || !j.is_object() || !j.contains("sprites") || !j["sprites"].is_array()) {
            LOG_ERROR("Settings layout '{}' could not be loaded", layoutPath);
            return;
        }

        if (j.contains("labelTextures") && j["labelTextures"].is_object()) {
            for (auto it = j["labelTextures"].begin(); it != j["labelTextures"].end(); ++it) {
                if (it.value().is_string()) {
                    labelTextures_.emplace(it.key(), it.value().get<std::string>());
                }
            }
        }

        for (const auto& data : j["sprites"]) {
            if (!data.is_object() || !data.contains("name") || !data["name"].is_string() ||
                data["name"].get<std::string>().empty() ||
                !data.contains("position") || !data.contains("size") || !data.contains("color")) {
                continue;
            }

            const auto& position = data["position"];
            const auto& size = data["size"];
            const auto& color = data["color"];
            if (!position.is_array() || position.size() != 2 || !size.is_array() || size.size() != 2 ||
                !color.is_array() || color.size() != 4) {
                continue;
            }

            LayoutSprite layout;
            layout.texture = data.value("texture", std::string{});
            layout.selectedTexture = data.value("selectedTexture", std::string{});
            layout.position = {position[0].get<float>(), position[1].get<float>()};
            layout.size = {size[0].get<float>(), size[1].get<float>()};
            layout.color = {color[0].get<float>(), color[1].get<float>(),
                            color[2].get<float>(), color[3].get<float>()};

            if (data.contains("textureCrop") && data["textureCrop"].is_array() && data["textureCrop"].size() == 4) {
                const auto& crop = data["textureCrop"];
                layout.cropOrigin = {crop[0].get<float>(), crop[1].get<float>()};
                layout.cropSize = {crop[2].get<float>(), crop[3].get<float>()};
                layout.hasCrop = true;
            }

            layoutSprites_.emplace(data["name"].get<std::string>(), std::move(layout));
        }
    } catch (...) {
        LOG_ERROR("Settings layout '{}' contains invalid data", layoutPath);
        layoutSprites_.clear();
        labelTextures_.clear();
    }
}

void SettingsMenuOverlay::Finalize() {
    // アクティブ状態の後始末を先に行い、カーソルのクリップを残さない。
    if (active_)
        SetActive(false);
    sprites_.clear();
    layoutSprites_.clear();
    labelTextures_.clear();
    common_ = nullptr;
    input_ = nullptr;
}

void SettingsMenuOverlay::ReadValues() {
    // 編集開始時点の音量と感度を退避し、キャンセル時の基準にする。
    auto* s = GameSettingsManager::GetInstance();
    original_.bgm = s->GetBGMVolume();
    original_.se = s->GetSEVolume();
    original_.sensitivity = s->GetCameraSensitivity();
    draft_ = original_;
}

void SettingsMenuOverlay::SetActive(bool active) {
    // 複数の設定画面が重なっても、最初の開閉だけでカーソル状態を切り替える。
    if (active_ == active)
        return;
    if (active) {
        if (activeCount_ == 0) {
            GetClipCursor(&previousCursorClip);
            CURSORINFO cursor{sizeof(CURSORINFO)};
            previousCursorVisible = !GetCursorInfo(&cursor) || (cursor.flags & CURSOR_SHOWING) != 0;
            ClipCursor(nullptr);
            WinApp::SetCursorVisibility(true);
        }

        ++activeCount_;
        Window()->SetCursorRecenteringSuppressed(true);
        ReadValues();
        animationTime_ = 0.0f;
        horizontalHeldDirection_ = 0;
        horizontalNextRepeatMs_ = 0;
        horizontalHoldStartMs_ = 0;
        category_ = row_ = scroll_ = 0;
        footer_ = -1;
        modal_ = Modal::None;
        status_.clear();
        suppressInput_ = true;
        replayOnly_ = false;
    } else {
        --activeCount_;
        Window()->SetCursorRecenteringSuppressed(activeCount_ > 0);

        if (activeCount_ == 0 && !replayOnly_) {
            if (GetForegroundWindow() == Window()->GetHwnd())
                ClipCursor(&previousCursorClip);
            WinApp::SetCursorVisibility(previousCursorVisible);
        }

        if (!replayOnly_) {
            draft_ = original_;
            PreviewAudio();
        }

        modal_ = Modal::None;
    }

    active_ = active;

    for (auto& s : sprites_)
        s->SetVisible(false);
    if (active_)
        BuildFrame();
}

bool SettingsMenuOverlay::IsDirty() const {
    // 公開している三項目だけを比較し、未適用の変更を判定する。
    return original_.bgm != draft_.bgm || original_.se != draft_.se ||
           original_.sensitivity != draft_.sensitivity;
}

void SettingsMenuOverlay::PreviewAudio() {
    // 音量だけは保存前に試聴できるよう、編集中の値を即時反映する。
    auto* s = GameSettingsManager::GetInstance();
    s->SetBGMVolume(draft_.bgm);
    s->SetSEVolume(draft_.se);
}

void SettingsMenuOverlay::Apply() {
    // 音量と感度だけを保存し、表示モードや反転設定には触れない。
    if (IsDirty()) Commit();
}

// プレビュー中の値を確定し、保存が成功した設定だけ次回起動へ引き継ぎます。
void SettingsMenuOverlay::Commit() {
    const auto fail = [this]() {
        status_ = "save_error";
    };

    // 保存完了までカメラ感度を確定しない。既存ファイルは一時ファイルから置換する。
    try {
        namespace fs = std::filesystem;
        const fs::path userPath = "Resources/json/user_config.json";
        auto read = [](const fs::path& path) {
            std::ifstream f(path);
            return std::string(std::istreambuf_iterator<char>(f), {});
        };

        const std::string oldUser = read(userPath);
        auto j = oldUser.empty() ? nlohmann::json::object() : nlohmann::json::parse(oldUser);
        if (!j.is_object())
            j = nlohmann::json::object();
        j["bgmVolume"] = draft_.bgm;
        j["seVolume"] = draft_.se;
        j["cameraSensitivity"] = draft_.sensitivity;

        auto replace = [](const fs::path& path, const std::string& text) {
            fs::create_directories(path.parent_path());

            // 前回失敗で残ったファイルや他の保存処理を上書きしないよう固有名を使います。
            const fs::path temporary = path.wstring() + L".options-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
            if (fs::exists(temporary))
                return false;
            {
                std::ofstream f(temporary, std::ios::binary);
                f << text;
                f.flush();

                if (!f) {
                    f.close();
                    fs::remove(temporary);
                    return false;
                }
            }

            if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return true;
            fs::remove(temporary);
            return false;
        };

        if (!replace(userPath, j.dump(4))) {
            fail();
            return;
        }
    } catch (...) {
        fail();
        return;
    }

    auto* s = GameSettingsManager::GetInstance();
    PreviewAudio();
    s->SetCameraSensitivity(draft_.sensitivity);

    original_ = draft_;
    modal_ = Modal::None;
    status_ = "saved";
}

void SettingsMenuOverlay::ResetCategory() {
    // この画面にある三項目だけを初期値へ戻す。保存は適用時に行う。
    draft_.bgm = draft_.se = .8f;
    draft_.sensitivity = 1.0f;
    PreviewAudio();
}

int SettingsMenuOverlay::RowCount() const {
    // カテゴリを分けず、音量二項目とカメラ感度を並べる。
    return 3;
}

void SettingsMenuOverlay::Adjust(int direction) {
    // 表示している百分率を整数で動かし、浮動小数点の誤差で1刻みが飛ばないようにする。
    status_.clear();
    float& value = row_ == 0 ? draft_.bgm : row_ == 1 ? draft_.se : draft_.sensitivity;
    const int minimum = row_ == 2 ? 20 : 0;
    const int percent = std::clamp(static_cast<int>(std::lround(value * 100.f)) + direction,
                                   minimum, 100);
    value = percent / 100.f;
    if (row_ < 2)
        PreviewAudio();
}

void SettingsMenuOverlay::ActivateRow() {
    Adjust(1);
}

void SettingsMenuOverlay::ActivateFooter(int index) {
    // 戻る際に編集値が残っていれば、破棄確認を先に表示する。
    dragging_ = false;
    modalSelection_ = 1;

    if (index == 0) {
        if (IsDirty())
            modal_ = Modal::Discard;
        else
            SetActive(false);
    }

    if (index == 1)
        modal_ = Modal::Reset;
    if (index == 2)
        Apply();
}

void SettingsMenuOverlay::HandleModal(bool accept, bool back) {
    // 確定したときだけ初期化または破棄を実行し、キャンセル時は編集値を保つ。
    if (back || (accept && modalSelection_ == 1)) {
        modal_ = Modal::None;
        return;
    }

    if (!accept) return;
    const Modal mode = modal_;
    modal_ = Modal::None;
    if (mode == Modal::Discard) SetActive(false);
    if (mode == Modal::Reset) ResetCategory();
}

void SettingsMenuOverlay::SetEditorViewport(int left, int top, int right, int bottom) {
    // 編集画面内のマウス座標をゲーム領域基準へ変換するため矩形を保持する。
    editorViewport_ = {left, top, right, bottom};
    hasEditorViewport_ = right > left && bottom > top;
}

engine::Vector2 SettingsMenuOverlay::Pointer() const {
    engine::Vector2 p = input_->GetMousePosition();
    float w = static_cast<float>(WinApp::kClientWidth), h = static_cast<float>(WinApp::kClientHeight);
#ifdef USE_IMGUI
    if (hasEditorViewport_) {
        POINT screen{};
        GetCursorPos(&screen);
        p = {static_cast<float>(screen.x - editorViewport_.left), static_cast<float>(screen.y - editorViewport_.top)};
        w = static_cast<float>(editorViewport_.right - editorViewport_.left);
        h = static_cast<float>(editorViewport_.bottom - editorViewport_.top);
    }
#endif
    // 描画先の矩形をそのまま設計座標へ戻す。Game Viewへの再拡縮も同じ比率で扱う。
    return {p.x * 1280.f / std::max(1.f, w), p.y * 720.f / std::max(1.f, h)};
}

SettingsMenuOverlay::Result SettingsMenuOverlay::Update(float deltaTime) {
    if (!active_)
        return Result::None;

    // ポーズ中でもUIへ渡される時間で、既存ポーズと同じ周期のカーソルを動かす。
    animationTime_ = std::fmod(animationTime_ + std::max(0.f, deltaTime), 120.f);

    if (replayOnly_) {
        // 通常更新が再開したら履歴の編集値は破棄し、現在の設定から開き直します。
        replayOnly_ = false;
        active_ = false;
        --activeCount_;
        SetActive(true);
        return Result::None;
    }

    if (suppressInput_) {
        if (input_->GetPressedKeys().empty() && input_->GetPressedMouseButton() < 0 &&
            input_->GetPressedGamepadButton() == 0)
            suppressInput_ = false;
        BuildFrame();
        return Result::None;
    }

    const bool accept = input_->IsKeyTriggered(DIK_RETURN) || input_->IsKeyTriggered(DIK_SPACE) ||
                        input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A);
    const bool back = input_->IsKeyTriggered(DIK_ESCAPE) || input_->IsKeyTriggered(DIK_TAB) || input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B);
    int horizontal =
        (input_->IsKeyTriggered(DIK_RIGHT) ||
            input_->IsKeyTriggered(DIK_D) ||
            input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_RIGHT) ||
            input_->IsMenuStickTriggered(XINPUT_GAMEPAD_DPAD_RIGHT) ? 1 : 0) -
        (input_->IsKeyTriggered(DIK_LEFT) ||
            input_->IsKeyTriggered(DIK_A) ||
            input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_LEFT) ||
            input_->IsMenuStickTriggered(XINPUT_GAMEPAD_DPAD_LEFT) ? 1 : 0);
    const int vertical =
        (input_->IsKeyTriggered(DIK_DOWN) ||
            input_->IsKeyTriggered(DIK_S) ||
            input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN) ||
            input_->IsMenuStickTriggered(XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0) -
        (input_->IsKeyTriggered(DIK_UP) ||
            input_->IsKeyTriggered(DIK_W) ||
            input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP) ||
            input_->IsMenuStickTriggered(XINPUT_GAMEPAD_DPAD_UP) ? 1 : 0);
    engine::Vector2 p = Pointer();
    const bool click = input_->IsMouseButtonTriggered(0);

    if (modal_ != Modal::None) {
        horizontalHeldDirection_ = 0;
        if (horizontal)
            modalSelection_ = 1 - modalSelection_;
        bool choose = accept;

        if (click) {
            if (InsideLayout(p, "settings_modal_button_0")) {
                modalSelection_ = 0;
                choose = true;
            }

            if (InsideLayout(p, "settings_modal_button_1")) {
                modalSelection_ = 1;
                choose = true;
            }
        }

        HandleModal(choose, back);
    } else {
        if (vertical) {
            if (footer_ >= 0) {
                footer_ = -1;
                row_ = vertical < 0 ? RowCount() - 1 : 0;
            } else if (row_ + vertical >= RowCount() || row_ + vertical < 0)
                footer_ = 0;
            else
                row_ += vertical;
        }

        // 項目の値だけは押し続けても動かせるようにする。短押しは1、長押し開始後は一定間隔で1ずつ進める。
        if (footer_ < 0) {
            const engine::Vector2 stick = input_->GetLeftStick();
            const bool stickRight = stick.x >= .45f && std::abs(stick.x) >= std::abs(stick.y);
            const bool stickLeft = stick.x <= -.45f && std::abs(stick.x) >= std::abs(stick.y);
            const int held =
                (input_->IsKeyPressed(DIK_RIGHT) || input_->IsKeyPressed(DIK_D) ||
                 input_->IsGamepadButtonPressed(XINPUT_GAMEPAD_DPAD_RIGHT) || stickRight ? 1 : 0) -
                (input_->IsKeyPressed(DIK_LEFT) || input_->IsKeyPressed(DIK_A) ||
                 input_->IsGamepadButtonPressed(XINPUT_GAMEPAD_DPAD_LEFT) || stickLeft ? 1 : 0);
            const uint64_t now = GetTickCount64();

            if (held == 0) {
                horizontalHeldDirection_ = 0;
            } else if (held != horizontalHeldDirection_) {
                horizontalHeldDirection_ = held;
                horizontalHoldStartMs_ = now;
                horizontalNextRepeatMs_ = now + 320;
                horizontal = held;
            } else if (now >= horizontalNextRepeatMs_) {
                horizontal = held;
                horizontalNextRepeatMs_ = now + (now - horizontalHoldStartMs_ >= 1200 ? 35 : 65);
            } else {
                // スティック側の既存リピート通知と二重に増加しないよう、この画面で一元管理する。
                horizontal = 0;
            }
        } else {
            horizontalHeldDirection_ = 0;
        }

        if (horizontal) {
            if (footer_ >= 0)
                footer_ = (footer_ + horizontal + 3) % 3;
            else
                Adjust(horizontal);
        }

        if (back)
            ActivateFooter(0);
        else if (accept) {
            if (footer_ >= 0)
                ActivateFooter(footer_);
            else
                ActivateRow();
        }

        if (click && modal_ == Modal::None) {
            for (int f = 0; f < 3; ++f)
                if (InsideLayout(p, "settings_footer_button_" + std::to_string(f))) {
                    footer_ = f;
                    ActivateFooter(f);
                }

            for (int n = 0; n < kVisibleRows && n + scroll_ < RowCount(); ++n)
                if (InsideLayout(p, "settings_row_" + std::to_string(n))) {
                    row_ = scroll_ + n;
                    footer_ = -1;

                    // つまみはバー端から半幅だけはみ出すため、その部分もつかめるようにする。
                    const LayoutSprite* track = FindLayoutSprite("settings_slider_track_" + std::to_string(n));
                    dragging_ = track && p.x >= track->position.x - 6.0f &&
                                p.x <= track->position.x + track->size.x + 6.0f;
                }
        }

        if (!input_->IsMouseButtonPressed(0))
            dragging_ = false;
        if (dragging_ && modal_ == Modal::None && (row_ >= 0 && row_ < 3)) {
            const LayoutSprite* track = FindLayoutSprite("settings_slider_track_" + std::to_string(row_));
            const float t = track && track->size.x > 0.0f
                                ? std::clamp((p.x - track->position.x) / track->size.x, 0.0f, 1.0f)
                                : 0.0f;

            if (row_ < 2) {
                if (row_ == 0)
                    draft_.bgm = t;
                else
                    draft_.se = t;
                PreviewAudio();
            } else
                draft_.sensitivity = .2f + t * .8f;
        }

        const LayoutSprite* firstRow = FindLayoutSprite("settings_row_0");
        const LayoutSprite* lastRow = FindLayoutSprite("settings_row_2");
        const bool pointerOverRows = firstRow && lastRow &&
            Inside(p, firstRow->position.x, firstRow->position.y, firstRow->size.x,
                   lastRow->position.y + lastRow->size.y - firstRow->position.y);
        if (pointerOverRows && input_->GetMouseWheelDelta() != 0) {
            row_ = std::clamp(row_ + (input_->GetMouseWheelDelta() > 0 ? -1 : 1), 0, RowCount() - 1);
            footer_ = -1;
        }

        scroll_ = std::clamp(scroll_, std::max(0, row_ - kVisibleRows + 1), row_);
    }

    if (active_)
        BuildFrame();
    return active_ ? Result::None : Result::Closed;
}

void SettingsMenuOverlay::Picture(const std::string& texture, float x, float y, float w, float h,
                                  const engine::Vector4& color) {
    // Spriteをフレーム間で再利用し、テクスチャ切替時は切り出し範囲を全体へ戻す。
    if (usedSprites_ == sprites_.size()) {
        auto s = std::make_unique<engine::Sprite>();
        s->Initialize(common_, engine::Sprite::LoadTexture(texture));
        sprites_.push_back(std::move(s));
    }

    auto& s = sprites_[usedSprites_++];
    const auto handle = engine::Sprite::LoadTexture(texture);
    const auto& meta = TextureManager::GetInstance()->GetMetadata(handle);
    s->SetTextureHandle(handle);
    s->SetTextureName(texture);
    s->SetTextureRect({0, 0}, {static_cast<float>(meta.width), static_cast<float>(meta.height)});

    // Spriteの投影サイズと一致させ、表示とPointerの逆変換で余白の差を作らない。
    const engine::Vector2 scale{WinApp::kClientWidth / 1280.f, WinApp::kClientHeight / 720.f};
    s->SetAnchorPoint({0, 0});
    s->SetPosition({x * scale.x, y * scale.y});
    s->SetSize({w * scale.x, h * scale.y});
    s->SetColor(color);
    s->SetEmissive(1);
    s->SetVisible(true);
    s->Update();
}

void SettingsMenuOverlay::Label(const std::string& id, float x, float y, float height) {
    // 事前生成した文字画像の縦横比を保ち、指定した高さに合わせる。
    const auto it = labelTextures_.find(id);
    if (it == labelTextures_.end()) {
        return;
    }

    const std::string& path = it->second;
    const auto& m = TextureManager::GetInstance()->GetMetadata(engine::Sprite::LoadTexture(path));
    Picture(path, x, y, height * static_cast<float>(m.width) / std::max(1.f, static_cast<float>(m.height)), height,
            kInk);
}

void SettingsMenuOverlay::Number(int value, float x, float y, float height) {
    // 数字も文字画像を桁ごとに配置し、文字列の生成コストを避ける。
    for (char c : std::to_string(value)) {
        Label(std::string("digit_") + c, x, y, height);
        x += height * .65f;
    }
}

const SettingsMenuOverlay::LayoutSprite* SettingsMenuOverlay::FindLayoutSprite(const std::string& name) const {
    const auto it = layoutSprites_.find(name);
    return it == layoutSprites_.end() ? nullptr : &it->second;
}

bool SettingsMenuOverlay::InsideLayout(const engine::Vector2& point, const std::string& name) const {
    const LayoutSprite* layout = FindLayoutSprite(name);
    return layout && Inside(point, layout->position.x, layout->position.y, layout->size.x, layout->size.y);
}

void SettingsMenuOverlay::PictureLayout(const std::string& name, bool selected, float offsetX,
                                        float offsetY, float widthScale) {
    const LayoutSprite* layout = FindLayoutSprite(name);
    if (!layout || layout->texture.empty()) {
        return;
    }

    const std::string& texture = selected && !layout->selectedTexture.empty()
                                     ? layout->selectedTexture : layout->texture;
    Picture(texture, layout->position.x + offsetX, layout->position.y + offsetY,
            layout->size.x * widthScale, layout->size.y, layout->color);

    if (layout->hasCrop) {
        sprites_[usedSprites_ - 1]->SetTextureRect(layout->cropOrigin, layout->cropSize);
        sprites_[usedSprites_ - 1]->Update();
    }
}

void SettingsMenuOverlay::CenteredLabel(const std::string& id, const std::string& layoutName) {
    const LayoutSprite* layout = FindLayoutSprite(layoutName);
    const auto texture = labelTextures_.find(id);
    if (!layout || texture == labelTextures_.end()) {
        return;
    }

    const auto& metadata = TextureManager::GetInstance()->GetMetadata(engine::Sprite::LoadTexture(texture->second));
    const float width = layout->size.y * static_cast<float>(metadata.width) /
                        std::max(1.0f, static_cast<float>(metadata.height));
    const float center = layout->position.x + layout->size.x * 0.5f;
    Label(id, center - width * 0.5f, layout->position.y, layout->size.y);
}

void SettingsMenuOverlay::DrawCursor(int row) {
    // 選択行のJSON位置を基準にカーソルを追従させ、弾みだけを実行時に加える。
    const LayoutSprite* first = FindLayoutSprite("settings_row_0");
    const LayoutSprite* current = FindLayoutSprite("settings_row_" + std::to_string(row));
    if (!first || !current) {
        return;
    }

    const float rowOffset = current->position.y - first->position.y;
    const float bob = std::sin(animationTime_ * 7.5f) * 2.0f;
    PictureLayout("settings_cursor_slime", false, 0.0f, rowOffset + bob);
    PictureLayout("settings_cursor_arrow", false, std::sin(animationTime_ * 8.0f) * 2.0f, rowOffset);
}

void SettingsMenuOverlay::BuildFrame() {
    // JSONの各Spriteを名前で描き、設定値に応じた幅・文字・選択状態だけ上書きする。
    if (!common_) return;
    usedSprites_ = 0;
    PictureLayout("settings_background");
    PictureLayout("settings_frame");
    PictureLayout("settings_title");

    for (int r = 0; r < 3; ++r) {
        const bool selected = row_ == r && footer_ < 0;
        const std::string index = std::to_string(r);
        PictureLayout("settings_row_" + index, selected);
        PictureLayout("settings_row_label_" + index);

        const float value = r == 0 ? draft_.bgm : r == 1 ? draft_.se : draft_.sensitivity;
        const float t = std::clamp(r == 2 ? (value - 0.2f) / 0.8f : value, 0.0f, 1.0f);
        PictureLayout("settings_slider_track_" + index);
        const LayoutSprite* fill = FindLayoutSprite("settings_slider_fill_" + index);
        PictureLayout("settings_slider_fill_" + index, false, 0.0f, 0.0f,
                      fill && fill->size.x > 0.0f ? std::max(1.0f / fill->size.x, t) : t);

        const LayoutSprite* track = FindLayoutSprite("settings_slider_track_" + index);
        PictureLayout("settings_slider_thumb_" + index, false,
                      track ? t * track->size.x : 0.0f);

        if (const LayoutSprite* number = FindLayoutSprite("settings_number_" + index)) {
            Number(static_cast<int>(std::round(value * 100)), number->position.x,
                   number->position.y, number->size.y);
        }
        if (selected && modal_ == Modal::None) DrawCursor(r);
    }

    CenteredLabel(status_.empty() ? (IsDirty() ? "dirty" : "clean") : status_, "settings_status");

    for (int f = 0; f < 3; ++f) {
        const std::string index = std::to_string(f);
        PictureLayout("settings_footer_button_" + index, footer_ == f);
        if (footer_ == f && modal_ == Modal::None)
            PictureLayout("settings_footer_arrow_" + index, false, std::sin(animationTime_ * 8.0f) * 2.0f);
        PictureLayout("settings_footer_label_" + index);
    }

    if (modal_ != Modal::None) {
        PictureLayout("settings_modal_mask");
        PictureLayout("settings_modal_panel");
        CenteredLabel(modal_ == Modal::Discard ? "confirm_discard" : "confirm_reset_all",
                      "settings_modal_title");

        for (int b = 0; b < 2; ++b) {
            const std::string index = std::to_string(b);
            PictureLayout("settings_modal_button_" + index, b == modalSelection_);
            if (b == modalSelection_)
                PictureLayout("settings_modal_arrow_" + index, false, std::sin(animationTime_ * 8.0f) * 2.0f);
            PictureLayout("settings_modal_label_" + index);
        }
    }

    // 確認ダイアログを開いた時も、実際に使える閉じる・決定を両端に表示する。
    const bool gamepad = input_ && input_->IsGamepadMode();
    PictureLayout("settings_footer_close_frame");
    PictureLayout("settings_footer_close_text", gamepad);
    PictureLayout("settings_footer_confirm_frame");
    PictureLayout("settings_footer_confirm_text", gamepad);

    for (size_t i = usedSprites_; i < sprites_.size(); ++i) sprites_[i]->SetVisible(false);
}

void SettingsMenuOverlay::Draw() {
    // 非表示中はSprite群を描かず、編集中の画面だけを重ねる。
    if (active_)
        for (auto& s : sprites_)
            s->Draw();
}

void SettingsMenuOverlay::CollectReplaySprites(std::vector<engine::Sprite*>& out) const {
    // リプレイ描画が所有Spriteを参照できるよう、非所有ポインタだけ渡す。
    for (const auto& s : sprites_)
        out.push_back(s.get());
}

void SettingsMenuOverlay::CaptureReplayState(nlohmann::json& state) const {
    // 表示中の選択位置と編集中の値を記録し、再生画面で同じUIを組み立てる。
    state = {{"active", active_}, {"category", category_}, {"row", row_},
             {"scroll", scroll_}, {"footer", footer_},     {"layoutVersion", 3}, {"animationTime", animationTime_}};
    state["values"] = {{"bgm", draft_.bgm}, {"se", draft_.se}, {"sensitivity", draft_.sensitivity}};
}

// 表示位置と編集中の値を再現し、旧レイアウトの行番号も現行項目へ読み替えます。
// リプレイ専用状態にするため実設定の保存とウィンドウ操作は行いません。
void SettingsMenuOverlay::RestoreReplayState(const nlohmann::json& state) {
    if (!state.is_object())
        return;
    const bool active = state.value("active", false);

    if (active_ != active) {
        activeCount_ += active ? 1 : -1;
        active_ = active;
        Window()->SetCursorRecenteringSuppressed(activeCount_ > 0);
    }

    replayOnly_ = true;
    modal_ = Modal::None;

    if (state.contains("values") && state["values"].is_object()) {
        const auto& v = state["values"];
        draft_.bgm = std::clamp(v.value("bgm", .8f), 0.f, 1.f);
        draft_.se = std::clamp(v.value("se", .8f), 0.f, 1.f);
        draft_.sensitivity = std::clamp(v.value("sensitivity", 1.f), .2f, 1.f);

    }

    // 旧カテゴリ形式の履歴は三項目の対応する行に移し、隠れた項目は先頭へ戻す。
    int savedRow = state.value("row", 0);

    if (state.value("layoutVersion", 1) < 3) {
        int category = state.value("category", 0);
        if (state.value("layoutVersion", 1) < 2) category = category >= 2 ? category - 1 : 0;
        savedRow = category == 0 ? std::min(savedRow, 1) : category == 1 ? 2 : 0;
    }

    animationTime_ = state.value("animationTime", 0.f);
    category_ = scroll_ = 0;
    row_ = std::clamp(savedRow, 0, 2);
    footer_ = std::clamp(state.value("footer", -1), -1, 2);

    // リプレイ復元では実際の設定保存・ウィンドウ変更を行いません。
    if (active_)
        BuildFrame();
    else
        for (auto& s : sprites_)
            s->SetVisible(false);
}
