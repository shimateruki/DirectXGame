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
    if (!sceneManager_) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (!currentScene) return;

    // -------------------------------------------------------------
    // 1. スプライトリスト
    //   
    // -------------------------------------------------------------
    ImGui::Begin("スプライト一覧 (Sprite List)");

    auto& sprites = currentScene->GetSprites();
    int id = 0;

    if (sprites.empty()) {
        ImGui::TextDisabled("スプライトがありません");
    }

    for (const auto& spritePtr : sprites) {
        Sprite* sprite = spritePtr.get();
        if (!sprite) continue;

        ImGui::PushID(id++);

        // 名前があればそれを使う、なければ番号
        std::string label = "Sprite " + std::to_string(id);
        if (!sprite->GetName().empty()) {
            label = sprite->GetName();
        }

        // 選択状態の管理
        bool isSelected = (selectedSprite_ == sprite);
        if (ImGui::Selectable(label.c_str(), isSelected)) {
            selectedSprite_ = sprite;
        }
        ImGui::PopID();
    }
    ImGui::End();


    // -------------------------------------------------------------
    // 2. 詳細設定＆ファイル管理
    // -------------------------------------------------------------
    ImGui::Begin("スプライト詳細 (Sprite Inspector)");

    // --- ファイル管理セクション ---
    if (ImGui::CollapsingHeader("ファイル管理 (File I/O)", ImGuiTreeNodeFlags_DefaultOpen)) {
        // ディレクトリ作成 (typo修正: resouces -> resources の方が一般的ですが、既存に合わせます)
        std::string directoryPath = "resouces/json/";
        if (!fs::exists(directoryPath)) { fs::create_directories(directoryPath); }

        // コンボボックス: 既存ファイルを選択して名前欄に入力する
        if (ImGui::BeginCombo("既存ファイル", currentSpriteFilename_)) {
            if (fs::exists(directoryPath)) {
                for (const auto& entry : fs::directory_iterator(directoryPath)) {
                    if (entry.path().extension() == ".json") {
                        std::string fname = entry.path().filename().string();

                        bool isSelected = (std::string(currentSpriteFilename_) == fname);
                        if (ImGui::Selectable(fname.c_str(), isSelected)) {
                            // 選択したらバッファにコピー
                            strcpy_s(currentSpriteFilename_, fname.c_str());
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }

        ImGui::InputText("保存名 (.json)", currentSpriteFilename_, sizeof(currentSpriteFilename_));

        // 保存ボタン
        if (ImGui::Button("レイアウト保存 (Save)")) {
            std::string fullPath = directoryPath + std::string(currentSpriteFilename_);
            // 拡張子がなければ足すなどの処理を入れても良い
            SaveSpriteLayout(fullPath);
        }

        // ※読み込みボタンもあると便利なのでレイアウトだけ作っておきます
        ImGui::SameLine();
        if (ImGui::Button("読み込み (Load)")) {
            std::string fullPath = directoryPath + std::string(currentSpriteFilename_);
            // LoadSpriteLayout(fullPath); // 実装済みならコメントアウト解除
        }
    }

    ImGui::Separator();

    // --- パラメータ編集セクション ---
    if (selectedSprite_) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "選択中: %s", selectedSprite_->GetName().c_str());

        // 各種パラメータ取得
        Vector2 pos = selectedSprite_->GetPosition();
        Vector2 size = selectedSprite_->GetSize();
        Vector2 anchor = selectedSprite_->GetAnchorPoint();
        Vector4 color = selectedSprite_->GetColor(); // 元コードで取得していたので活用します

        bool changed = false;

        // 座標
        if (ImGui::DragFloat2("座標 (Pos)", &pos.x, 1.0f)) {
            selectedSprite_->SetPosition(pos);
        }

        // サイズ
        if (ImGui::DragFloat2("サイズ (Size)", &size.x, 1.0f)) {
            selectedSprite_->SetSize(size);
        }

        // アンカーポイント (0.0~1.0の範囲が多いので、感度(speed)を0.01fに落として微調整しやすく)
        if (ImGui::DragFloat2("アンカー (Anchor)", &anchor.x, 0.01f)) {
            selectedSprite_->SetAnchorPoint(anchor);
        }

        // 色 (ColorEdit4を追加)
        if (ImGui::ColorEdit4("色 (Color)", &color.x)) {
            selectedSprite_->SetColor(color);
        }

        ImGui::Separator();

        // 選択解除ボタン（少し離して配置）
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        if (ImGui::Button("選択解除 (Deselect)", ImVec2(-1, 0))) { // 幅いっぱいにボタン
            selectedSprite_ = nullptr;
        }
    } else {
        ImGui::TextDisabled("リストからスプライトを選択してください");
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
