#include "SpriteDebugEditor.h"
#include "Sprite.h"
#include "imgui.h"
#include "json.hpp"
#include <fstream>
#include <string>
#include "SpriteCommon.h" 
#include "SceneManager.h"  
#include "BaseScene.h"    
#include "InputManager.h"
#include <filesystem>
#include "IconsFontAwesome5.h"

namespace fs = std::filesystem;


void SpriteDebugEditor::Initialize(SceneManager* sceneManager, InputManager* inputManager) {
    sceneManager_ = sceneManager; 
    inputManager_ = inputManager;
    selectedSprite_ = nullptr;
    isMovingX_ = false;
    isMovingY_ = false;


    gizmoArrowX_ = nullptr;
    gizmoArrowY_ = nullptr;
    gizmoTextureHandle_ = 0;
    initializedSpriteCommon_ = nullptr;
}


void SpriteDebugEditor::Finalize() {
    // (unique_ptr が自動で解放するので、特に何もしない)
}
void SpriteDebugEditor::Update(const Vector2& localMousePos, bool isHovered) {
#ifdef USE_IMGUI
    if (sceneManager_ == nullptr) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) {
        selectedSprite_ = nullptr;
        return;
    }

    // シーン切り替え時のリセット
    if (lastUpdatedScene_ != currentScene) {
        selectedSprite_ = nullptr;
        isMovingX_ = false;
        isMovingY_ = false;
        lastUpdatedScene_ = currentScene;
    }

    // ★重要：GameViewの上にマウスがない、または他のウィンドウが重なっている場合は無視
    // ただし、ドラッグ中は操作を継続させたいので isMoving もチェック
    if (!isHovered && !isMovingX_ && !isMovingY_) return;

    // --- 1. ギズモ操作 (簡易移動) ---
    if (selectedSprite_) {
        // マウスが押された瞬間
        if (inputManager_->IsMouseButtonTriggered(0) && IsMouseOver(selectedSprite_, localMousePos)) {
            isMovingX_ = true;
            isMovingY_ = true;
            dragStartMousePos_ = localMousePos; // 引数の座標を保存
            dragStartSpritePos_ = selectedSprite_->GetPosition();
        }

        // ドラッグ中
        if (inputManager_->IsMouseButtonPressed(0) && (isMovingX_ || isMovingY_)) {
            Vector2 delta = { localMousePos.x - dragStartMousePos_.x, localMousePos.y - dragStartMousePos_.y };
            Vector2 newPos = { dragStartSpritePos_.x + delta.x, dragStartSpritePos_.y + delta.y };
            selectedSprite_->SetPosition(newPos);
        }

        if (inputManager_->IsMouseButtonReleased(0)) {
            isMovingX_ = false;
            isMovingY_ = false;
        }
    }

    // --- 2. スプライト選択 ---
    // ドラッグ中でなく、クリックされた瞬間
    if (!isMovingX_ && !isMovingY_ && inputManager_->IsMouseButtonTriggered(0)) {
        auto& sprites = currentScene->GetSprites();
        bool hit = false;
        // 重なり順を考慮して逆順（手前）からチェック
        for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
            Sprite* sprite = it->get();
            if (sprite && IsMouseOver(sprite, localMousePos)) {
                selectedSprite_ = sprite;
                hit = true;
                break;
            }
        }
        if (!hit) selectedSprite_ = nullptr;
    }
#endif
}

void SpriteDebugEditor::DrawImGui() {
#ifdef USE_IMGUI
    if (!sceneManager_) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (!currentScene) return;

    // -------------------------------------------------------------
    // 1. スプライトリスト
    // -------------------------------------------------------------
    ImGui::Text(ICON_FA_LIST_UL " Sprite List");

    // 高さを150ピクセルに固定したスクロール枠を作成
    ImGui::BeginChild("SpriteListRegion", ImVec2(0, 150), true);

    auto& sprites = currentScene->GetSprites();
    int id = 0;

    if (sprites.empty()) {
        ImGui::TextDisabled(ICON_FA_INFO_CIRCLE " スプライトがありません");
    }

    for (const auto& spritePtr : sprites) {
        Sprite* sprite = spritePtr.get();
        if (!sprite) continue;

        ImGui::PushID(id++);

        std::string label = (sprite->GetName().empty()) ? "Sprite " + std::to_string(id) : sprite->GetName();
        // リストの各項目に画像アイコンを添える
        std::string iconLabel = std::string(ICON_FA_IMAGE " ") + label;

        bool isSelected = (selectedSprite_ == sprite);
        if (ImGui::Selectable(iconLabel.c_str(), isSelected)) {
            selectedSprite_ = sprite;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Spacing();

    // -------------------------------------------------------------
    // 2. ファイル管理 (File I/O)
    // -------------------------------------------------------------
    if (ImGui::CollapsingHeader(ICON_FA_SAVE " ファイル管理 (File I/O)", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string directoryPath = "Resources/json/sprite/";
        if (!std::filesystem::exists(directoryPath)) {
            std::filesystem::create_directories(directoryPath);
        }

        if (ImGui::BeginCombo(ICON_FA_HISTORY " 既存ファイル", currentSpriteFilename_)) {
            if (std::filesystem::exists(directoryPath)) {
                for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
                    if (entry.path().extension() == ".json") {
                        std::string fname = entry.path().filename().string();
                        bool isSelected = (std::string(currentSpriteFilename_) == fname);
                        if (ImGui::Selectable(fname.c_str(), isSelected)) {
                            strcpy_s(currentSpriteFilename_, fname.c_str());
                        }
                    }
                }
            }
            ImGui::EndCombo();
        }

        ImGui::InputText(ICON_FA_FILE_SIGNATURE " 保存名 (.json)", currentSpriteFilename_, sizeof(currentSpriteFilename_));

        if (ImGui::Button(ICON_FA_DOWNLOAD " レイアウト保存 (Save)")) {
            std::string fullPath = directoryPath + std::string(currentSpriteFilename_);
            SaveSpriteLayout(fullPath);
        }

        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UPLOAD " 読み込み (Load)")) {
            std::string fullPath = directoryPath + std::string(currentSpriteFilename_);
            // LoadSpriteLayout(fullPath);
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    // -------------------------------------------------------------
    // 3. パラメータ編集セクション
    // -------------------------------------------------------------
    if (selectedSprite_) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), ICON_FA_EDIT " 編集: %s", selectedSprite_->GetName().c_str());

        Vector2 pos = selectedSprite_->GetPosition();
        Vector2 size = selectedSprite_->GetSize();
        Vector2 anchor = selectedSprite_->GetAnchorPoint();
        Vector4 color = selectedSprite_->GetColor();

        if (ImGui::DragFloat2(ICON_FA_ARROWS_ALT " 座標 (Pos)", &pos.x, 1.0f)) {
            selectedSprite_->SetPosition(pos);
        }
        if (ImGui::DragFloat2(ICON_FA_EXPAND_ARROWS_ALT " サイズ (Size)", &size.x, 1.0f)) {
            selectedSprite_->SetSize(size);
        }
        if (ImGui::DragFloat2(ICON_FA_ANCHOR " アンカー (Anchor)", &anchor.x, 0.01f)) {
            selectedSprite_->SetAnchorPoint(anchor);
        }
        if (ImGui::ColorEdit4(ICON_FA_PALETTE " 色 (Color)", &color.x)) {
            selectedSprite_->SetColor(color);
        }

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        if (ImGui::Button(ICON_FA_TIMES_CIRCLE " 選択解除 (Deselect)", ImVec2(-1, 0))) {
            selectedSprite_ = nullptr;
        }
    }
    else {
        ImGui::TextDisabled(ICON_FA_MOUSE_POINTER " リストからスプライトを選択してください");
    }
#endif
}

void SpriteDebugEditor::SaveSpriteLayout(const std::string& filename) {
    using json = nlohmann::json;

    if (sceneManager_ == nullptr) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) return;

    json root;
    json spriteArray = json::array();

    auto& sprites = currentScene->GetSprites();

    for (const auto& spritePtr : sprites) {
        Sprite* sprite = spritePtr.get();
        if (sprite == nullptr) continue;

        json spriteData;
        if (!sprite->GetName().empty()) {
            spriteData["name"] = sprite->GetName();
        } else {
     
            spriteData["name"] = "NoName_" + std::to_string((size_t)sprite);
        }

        Vector2 pos = sprite->GetPosition();
        Vector2 size = sprite->GetSize();
        Vector2 anchor = sprite->GetAnchorPoint();

        spriteData["position"] = { pos.x, pos.y };
        spriteData["size"] = { size.x, size.y };
        spriteData["anchor"] = { anchor.x, anchor.y };

        spriteArray.push_back(spriteData);
    }
    root["sprites"] = spriteArray;

    std::ofstream file(filename);
    if (file.is_open()) {
        file << root.dump(4);
        file.close();
    }
}



bool SpriteDebugEditor::IsMouseOver(Sprite* sprite, const Vector2& localMousePos) const {
    if (sprite == nullptr) return false;

    Vector2 pos = sprite->GetPosition();
    Vector2 size = sprite->GetSize();
    Vector2 anchor = sprite->GetAnchorPoint();

    // スプライトの描画範囲を計算
    float minX = pos.x - (anchor.x * size.x);
    float maxX = minX + size.x;
    float minY = pos.y - (anchor.y * size.y);
    float maxY = minY + size.y;

    return (localMousePos.x >= minX && localMousePos.x <= maxX &&
        localMousePos.y >= minY && localMousePos.y <= maxY);
}
bool SpriteDebugEditor::IsMouseBusy() const {
    // ImGui操作中かどうか
#ifdef USE_IMGUI
    return ImGui::GetIO().WantCaptureMouse;
#else
    return false;
#endif
}

// (Draw の実装)
void SpriteDebugEditor::Draw() {
#ifdef USE_IMGUI
    if (sceneManager_ == nullptr || selectedSprite_ == nullptr || ImGui::GetIO().WantCaptureMouse) {
        return;
    }

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) {
        return;
    }
    SpriteCommon* currentSpriteCommon = currentScene->GetSpriteCommon();
    if (currentSpriteCommon == nullptr) {
        return;
    }
    if (gizmoArrowX_ == nullptr || initializedSpriteCommon_ != currentSpriteCommon) {

        // ギズモを「現在のシーンの Common」で再作成
        gizmoTextureHandle_ = Sprite::LoadTexture("white");

        gizmoArrowX_ = std::make_unique<Sprite>();
        gizmoArrowX_->Initialize(currentSpriteCommon, gizmoTextureHandle_);
        gizmoArrowX_->SetSize({ 50.0f, 10.0f });
        gizmoArrowX_->SetColor({ 1.0f, 0.0f, 0.0f, 0.8f });
        gizmoArrowX_->SetAnchorPoint({ 0.0f, 0.5f });

        gizmoArrowY_ = std::make_unique<Sprite>();
        gizmoArrowY_->Initialize(currentSpriteCommon, gizmoTextureHandle_);
        gizmoArrowY_->SetSize({ 10.0f, 50.0f });
        gizmoArrowY_->SetColor({ 0.0f, 1.0f, 0.0f, 0.8f });
        gizmoArrowY_->SetAnchorPoint({ 0.5f, 0.0f });

        // ★ 「今使った Common」を記憶する
        initializedSpriteCommon_ = currentSpriteCommon;
    }

    // --- ギズモの描画 ---
    Vector2 pos = selectedSprite_->GetPosition();
    gizmoArrowX_->SetPosition(pos);
    gizmoArrowY_->SetPosition(pos);

    gizmoArrowX_->Update();
    gizmoArrowY_->Update();

    gizmoArrowX_->Draw();
    gizmoArrowY_->Draw();
#endif
}
