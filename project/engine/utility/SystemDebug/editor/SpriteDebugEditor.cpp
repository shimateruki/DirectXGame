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
void SpriteDebugEditor::Update() {
#ifdef USE_IMGUI
    if (sceneManager_ == nullptr || inputManager_ == nullptr) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) {
        selectedSprite_ = nullptr;
        return;
    }

    if (lastUpdatedScene_ != currentScene) {
        selectedSprite_ = nullptr;
        isMovingX_ = false;
        isMovingY_ = false;
        lastUpdatedScene_ = currentScene;
    }

    bool isImGuiBusy = ImGui::GetIO().WantCaptureMouse;

    // --- 1. ギズモ操作 (簡易移動) ---
    if (selectedSprite_ && !isImGuiBusy) {
        if (inputManager_->IsMouseButtonTriggered(0) && IsMouseOver(selectedSprite_)) {
            isMovingX_ = true;
            isMovingY_ = true;
            dragStartMousePos_ = inputManager_->GetMousePosition();
            dragStartSpritePos_ = selectedSprite_->GetPosition();
        }

        if (inputManager_->IsMouseButtonPressed(0) && (isMovingX_ || isMovingY_)) {
            Vector2 mousePos = inputManager_->GetMousePosition();
            Vector2 delta = { mousePos.x - dragStartMousePos_.x, mousePos.y - dragStartMousePos_.y };
            Vector2 newPos = dragStartSpritePos_;
            newPos.x += delta.x;
            newPos.y += delta.y;
            selectedSprite_->SetPosition(newPos);
        }

        if (inputManager_->IsMouseButtonReleased(0)) {
            isMovingX_ = false;
            isMovingY_ = false;
        }
    }

    // --- 2. スプライト選択 ---
    if (!isMovingX_ && !isMovingY_ && !isImGuiBusy && inputManager_->IsMouseButtonTriggered(0)) {
        auto& sprites = currentScene->GetSprites();
        bool hit = false;
        for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
            Sprite* sprite = it->get();
            if (sprite && IsMouseOver(sprite)) {
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
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();

    // -------------------------------------------------------------
    // 1. スプライトリスト (名前を表示するように修正！)
    // -------------------------------------------------------------
    ImGui::Begin("Sprite List");

    auto& sprites = currentScene->GetSprites();
    int id = 0;
    for (const auto& spritePtr : sprites) {
        Sprite* sprite = spritePtr.get();
        if (!sprite) continue;

        ImGui::PushID(id++);

        // ★修正点: 名前があればそれを使う
        std::string label = "Sprite " + std::to_string(id);
        if (!sprite->GetName().empty()) {
            label = sprite->GetName();
        }

        bool isSelected = (selectedSprite_ == sprite);
        if (ImGui::Selectable(label.c_str(), isSelected)) {
            selectedSprite_ = sprite;
        }
        ImGui::PopID();
    }
    ImGui::End();


    // -------------------------------------------------------------
    // 2. 設定＆ファイル管理
    // -------------------------------------------------------------
    ImGui::Begin("Sprite Inspector");

    if (ImGui::CollapsingHeader("Layout File Manager", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string directoryPath = "resouces/json/";
        if (!fs::exists(directoryPath)) { fs::create_directories(directoryPath); }

        if (ImGui::BeginCombo("Existing Files", currentSpriteFilename_)) {
            if (fs::exists(directoryPath)) {
                for (const auto& entry : fs::directory_iterator(directoryPath)) {
                    if (entry.path().extension() == ".json") {
                        std::string fname = entry.path().filename().string();
                        bool isSelected = (std::string(currentSpriteFilename_) == fname);
                        if (ImGui::Selectable(fname.c_str(), isSelected)) {
                            strcpy_s(currentSpriteFilename_, fname.c_str());
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }

        ImGui::InputText("Filename", currentSpriteFilename_, sizeof(currentSpriteFilename_));

        if (ImGui::Button("Save Sprite Layout")) {
            std::string fullPath = directoryPath + std::string(currentSpriteFilename_);
            SaveSpriteLayout(fullPath);
        }
    }

    ImGui::Separator();

    if (selectedSprite_) {
        ImGui::Text("Selected: %s", selectedSprite_->GetName().c_str()); // 名前確認用

        Vector2 pos = selectedSprite_->GetPosition();
        Vector2 size = selectedSprite_->GetSize();
        Vector2 anchor = selectedSprite_->GetAnchorPoint();
        Vector4 color = selectedSprite_->GetColor();

        bool changed = false;
        if (ImGui::DragFloat2("Position", &pos.x, 1.0f)) { selectedSprite_->SetPosition(pos); changed = true; }
        if (ImGui::DragFloat2("Size", &size.x, 1.0f)) { selectedSprite_->SetSize(size); changed = true; }
        if (ImGui::DragFloat2("Anchor", &anchor.x, 0.05f)) { selectedSprite_->SetAnchorPoint(anchor); changed = true; }

        ImGui::Separator();
        if (ImGui::Button("Deselect")) selectedSprite_ = nullptr;
    }
    ImGui::End();
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
