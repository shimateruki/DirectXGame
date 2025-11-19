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

/// <summary>
/// ピッキング、ギズモ操作、インスペクタ描画のメイン処理
/// </summary>
void SpriteDebugEditor::Update() {
#ifdef USE_IMGUI
    if (sceneManager_ == nullptr || inputManager_ == nullptr) {
        return;
    }

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) {
        selectedSprite_ = nullptr;
        lastUpdatedScene_ = nullptr;
        return;
    }

    if (lastUpdatedScene_ != currentScene) {
        selectedSprite_ = nullptr;
        isMovingX_ = false;
        isMovingY_ = false;
        lastUpdatedScene_ = currentScene;
    }

    bool isImGuiBusy = ImGui::GetIO().WantCaptureMouse;

    // --- 1. ギズモ操作 (ロジック) ---
    if (selectedSprite_) {
        if (!isImGuiBusy && inputManager_->IsMouseButtonTriggered(0)) {
            if (gizmoArrowX_ && IsMouseOver(gizmoArrowX_.get())) {
                isMovingX_ = true;
                isMovingY_ = false;
                dragStartMousePos_ = inputManager_->GetMousePosition();
                dragStartSpritePos_ = selectedSprite_->GetPosition();
            } else if (gizmoArrowY_ && IsMouseOver(gizmoArrowY_.get())) {
                isMovingX_ = false;
                isMovingY_ = true;
                dragStartMousePos_ = inputManager_->GetMousePosition();
                dragStartSpritePos_ = selectedSprite_->GetPosition();
            }
        }

        Vector2 mousePos = inputManager_->GetMousePosition();
        if (isMovingX_) {
            float deltaX = mousePos.x - dragStartMousePos_.x;
            Vector2 newPos = dragStartSpritePos_;
            newPos.x = dragStartSpritePos_.x + deltaX;
            selectedSprite_->SetPosition(newPos);
        }
        if (isMovingY_) {
            float deltaY = mousePos.y - dragStartMousePos_.y;
            Vector2 newPos = dragStartSpritePos_;
            newPos.y = dragStartSpritePos_.y + deltaY;
            selectedSprite_->SetPosition(newPos);
        }

        if (inputManager_->IsMouseButtonReleased(0)) {
            isMovingX_ = false;
            isMovingY_ = false;
        }
    }

    // --- 2. スプライトピッキング (ロジック) ---
    if (!isImGuiBusy && !isMovingX_ && !isMovingY_ && inputManager_->IsMouseButtonTriggered(0)) {
        Vector2 mousePos = inputManager_->GetMousePosition();
        bool hit = false;
        std::vector<std::unique_ptr<Sprite>>& sprites = currentScene->GetSprites();

        if (!sprites.empty()) {
            for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
                Sprite* sprite = it->get();
                if (sprite == nullptr) continue;

                if (IsMouseOver(sprite)) {
                    selectedSprite_ = sprite;
                    hit = true;
                    break;
                }
            }
        }

        if (!hit) {
            selectedSprite_ = nullptr;
        }
    }
#endif
}

void SpriteDebugEditor::DrawImGui() {
#ifdef USE_IMGUI
    if (sceneManager_ == nullptr) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) return;

    // (ImGui::Begin/End は Game.cpp の "Master Editor" が行う)

    if (selectedSprite_ == nullptr) {
        ImGui::Text("No sprite selected.");
        ImGui::Text("Click on a sprite in the game view.");
    } else {
        ImGui::Text("Selected: %s", selectedSprite_->GetName().c_str());
        ImGui::Separator();

        Vector2 pos = selectedSprite_->GetPosition();
        if (ImGui::DragFloat2("Position", &pos.x, 1.0f)) {
            selectedSprite_->SetPosition(pos);
        }
        Vector2 size = selectedSprite_->GetSize();
        if (ImGui::DragFloat2("Size", &size.x, 1.0f)) {
            selectedSprite_->SetSize(size);
        }
        Vector4 color = selectedSprite_->GetColor();
        if (ImGui::ColorEdit4("Color", &color.x)) {
            selectedSprite_->SetColor(color);
        }
        Vector2 anchor = selectedSprite_->GetAnchorPoint();
        if (ImGui::DragFloat2("Anchor", &anchor.x, 0.01f, 0.0f, 1.0f)) {
            selectedSprite_->SetAnchorPoint(anchor);
        }

        ImGui::Separator();
        if (ImGui::Button("Save Sprite Layout")) {
            SaveSpriteLayout("sprite_layout.json");
        }
    }
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

void SpriteDebugEditor::SaveSpriteLayout(const std::string& filename) {


    using json = nlohmann::json;
    json root;
    json spriteArray = json::array();

    if (sceneManager_ == nullptr) return;

    //  カレントシーンからスプライトリストを取得 
    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) return;
    std::vector<std::unique_ptr<Sprite>>& sprites = currentScene->GetSprites();

    if (sprites.empty()) return; // 保存対象なし

    for (const auto& sprite : sprites) {
        if (!sprite) continue;
        json spriteData;
        spriteData["name"] = sprite->GetName();
        Vector2 pos = sprite->GetPosition();
        Vector2 size = sprite->GetSize();
        Vector2 anchor = sprite->GetAnchorPoint();
        Vector4 color = sprite->GetColor();
        spriteData["position"] = { pos.x, pos.y };
        spriteData["size"] = { size.x, size.y };
        spriteData["anchor"] = { anchor.x, anchor.y };
        spriteData["color"] = { color.x, color.y, color.z, color.w };
        spriteArray.push_back(spriteData);
    }
    root["sprites"] = spriteArray;

    std::ofstream file(filename);
    if (file.is_open()) {
        file << root.dump(4);
        file.close();
        OutputDebugStringA(("Saved sprite layout to " + filename + "\n").c_str());
    } else {
        OutputDebugStringA(("Failed to open " + filename + " for saving.\n").c_str());
    }
}


bool SpriteDebugEditor::IsMouseOver(Sprite* sprite) const {
    if (sprite == nullptr || inputManager_ == nullptr) return false;

    Vector2 mousePos = inputManager_->GetMousePosition();
    Vector2 pos = sprite->GetPosition();
    Vector2 size = sprite->GetSize();
    Vector2 anchor = sprite->GetAnchorPoint();

    float minX = pos.x - (anchor.x * size.x);
    float maxX = minX + size.x;
    float minY = pos.y - (anchor.y * size.y);
    float maxY = minY + size.y;

    return (mousePos.x >= minX && mousePos.x <= maxX &&
        mousePos.y >= minY && mousePos.y <= maxY);
}


bool SpriteDebugEditor::IsMouseBusy() const {
    return isMovingX_ || isMovingY_;
}