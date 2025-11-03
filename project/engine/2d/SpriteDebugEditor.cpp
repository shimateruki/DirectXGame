#include "SpriteDebugEditor.h"
#include "engine/scene/GamePlayScene.h" // GamePlayScene にアクセスするため
#include "engine/2d/Sprite.h"          // Sprite にアクセスするため
#include "externals/imgui/imgui.h"     // ImGui を使うため
#include "externals/nlohmann/json.hpp" // JSON保存用
#include <fstream>                     // ファイル出力用
#include <string>                      // std::string 用
#include "SpriteCommon.h"

void SpriteDebugEditor::Initialize(GamePlayScene* scene, InputManager* inputManager) {
    scene_ = scene;
    inputManager_ = inputManager;
    selectedSprite_ = nullptr;
    isMovingX_ = false;
    isMovingY_ = false;

    // ▼▼▼ ★ 1. ギズモの初期化 ★ ▼▼▼
    // (GamePlayScene から SpriteCommon を取得)
    SpriteCommon* spriteCommon = scene_->GetSpriteCommon();
    assert(spriteCommon);

    gizmoTextureHandle_ = Sprite::LoadTexture("white"); 

    // X軸ギズモ (赤色で、横長の矢印っぽくする)
    gizmoArrowX_ = std::make_unique<Sprite>();
    gizmoArrowX_->Initialize(spriteCommon, gizmoTextureHandle_);
    gizmoArrowX_->SetSize({ 50.0f, 10.0f }); // 横長
    gizmoArrowX_->SetColor({ 1.0f, 0.0f, 0.0f, 0.8f }); // 赤・半透明
    gizmoArrowX_->SetAnchorPoint({ 0.0f, 0.5f }); // 左端が基点

    // Y軸ギズモ (緑色で、縦長の矢印っぽくする)
    gizmoArrowY_ = std::make_unique<Sprite>();
    gizmoArrowY_->Initialize(spriteCommon, gizmoTextureHandle_);
    gizmoArrowY_->SetSize({ 10.0f, 50.0f }); // 縦長
    gizmoArrowY_->SetColor({ 0.0f, 1.0f, 0.0f, 0.8f }); // 緑・半透明
    gizmoArrowY_->SetAnchorPoint({ 0.5f, 0.0f }); // 上端が基点
 
}
void SpriteDebugEditor::Finalize() {
    // 今は特に何もしない
}

/// <summary>
/// ピッキング、ギズモ操作、インスペクタ描画のメイン処理
/// </summary>
void SpriteDebugEditor::Update() {
    if (scene_ == nullptr || inputManager_ == nullptr) {
        return;
    }

    // ImGuiのウィンドウがマウスを使っているか（クリックなどを邪魔しないため）
    bool isImGuiBusy = ImGui::GetIO().WantCaptureMouse;

    // --- 1. ギズモ操作 (ドラッグ処理) ---
    // (※ 選択中スプライトがある場合のみ)
    if (selectedSprite_) {

        // (A) ドラッグ開始判定
        // (ImGuiがビジーでなく、左クリックが押された瞬間)
        if (!isImGuiBusy && inputManager_->IsMouseButtonTriggered(0)) {

            // X軸ギズモがクリックされたか？
            if (IsMouseOver(gizmoArrowX_.get())) {
                isMovingX_ = true;
                isMovingY_ = false; // Yは確実に false
                dragStartMousePos_ = inputManager_->GetMousePosition();
                dragStartSpritePos_ = selectedSprite_->GetPosition();
            }
            // Y軸ギズモがクリックされたか？
            else if (IsMouseOver(gizmoArrowY_.get())) {
                isMovingX_ = false; // Xは確実に false
                isMovingY_ = true;
                dragStartMousePos_ = inputManager_->GetMousePosition();
                dragStartSpritePos_ = selectedSprite_->GetPosition();
            }
        }

        // (B) ドラッグ中の処理
        Vector2 mousePos = inputManager_->GetMousePosition();

        if (isMovingX_) {
            float deltaX = mousePos.x - dragStartMousePos_.x; // マウスの移動差分(X)
            Vector2 newPos = dragStartSpritePos_;
            newPos.x = dragStartSpritePos_.x + deltaX; // スプライトのX座標に反映
            selectedSprite_->SetPosition(newPos);
        }
        if (isMovingY_) {
            float deltaY = mousePos.y - dragStartMousePos_.y; // マウスの移動差分(Y)
            Vector2 newPos = dragStartSpritePos_;
            newPos.y = dragStartSpritePos_.y + deltaY; // スプライトのY座標に反映
            selectedSprite_->SetPosition(newPos);
        }

        // (C) ドラッグ終了判定
        // (※ IsMouseButtonReleased がない場合は !inputManager_->IsMouseButtonPressed(0) で代用)
        if (inputManager_->IsMouseButtonReleased(0)) {
            isMovingX_ = false;
            isMovingY_ = false;
        }
    }

    // --- 2. スプライトピッキング (選択処理) ---

    // (ImGuiがビジーでなく、ギズモをドラッグ中でなく、左クリックが押された瞬間)
    if (!isImGuiBusy && !isMovingX_ && !isMovingY_ && inputManager_->IsMouseButtonTriggered(0)) {

        Vector2 mousePos = inputManager_->GetMousePosition();
        bool hit = false; // ヒットしたか

        // 全スプライトを逆順（手前が先）にチェック
        auto& sprites = scene_->GetSprites();
        for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
            Sprite* sprite = it->get();
            if (sprite == nullptr) continue;

            // マウス座標がスプライトの矩形内にあるか判定
            if (IsMouseOver(sprite)) {

                // ★ ヒット！
                selectedSprite_ = sprite; // 選択対象を更新
                hit = true;
                break; // 一番手前のものだけ選んで終了
            }
        }

        // 何もヒットしなかったら、選択を解除
        if (!hit) {
            selectedSprite_ = nullptr;
        }
    }

    // --- 3. インスペクタウィンドウの描画 ---

    if (!ImGui::Begin("Sprite Inspector")) {
        // ウィンドウがたたまれているなどで描画不要なら即終了
        ImGui::End();
        return;
    }

    if (selectedSprite_ == nullptr) {
        ImGui::Text("No sprite selected.");
        ImGui::Text("Click on a sprite in the game view.");
    } else {
        // ★ 選択中のスプライトの情報を表示・編集
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

      
    }

    ImGui::End();
}
void SpriteDebugEditor::Draw() {
    // 選択中のスプライトがあり、かつImGuiがビジーでない時
    if (selectedSprite_ != nullptr && !ImGui::GetIO().WantCaptureMouse) {

        // ギズモを選択中スプライトの位置に合わせる
        Vector2 pos = selectedSprite_->GetPosition();
        gizmoArrowX_->SetPosition(pos);
        gizmoArrowY_->SetPosition(pos);

        // ギズモのスプライト行列を更新
        gizmoArrowX_->Update();
        gizmoArrowY_->Update();

        // ギズモを描画
        // (※SpriteCommon::SetPipeline は GamePlayScene::Draw で呼ばれる前提)
        gizmoArrowX_->Draw();
        gizmoArrowY_->Draw();
    }
}

// --- (任意) スプライトレイアウト保存処理 ---
void SpriteDebugEditor::SaveSpriteLayout(const std::string& filename) {
    using json = nlohmann::json;
    json root;
    json spriteArray = json::array();

    if (!scene_) return;
    std::vector<std::unique_ptr<Sprite>>& sprites = scene_->GetSprites();

    for (const auto& sprite : sprites) {
        if (!sprite) continue;

        json spriteData;
        // Sprite に GetName(), GetTextureName() がある前提
        spriteData["name"] = sprite->GetName();
        // spriteData["texture"] = sprite->GetTextureName(); // テクスチャ名も保存したい場合
        Vector2 pos = sprite->GetPosition();
        Vector2 size = sprite->GetSize();
        Vector2 anchor = sprite->GetAnchorPoint();
        Vector4 color = sprite->GetColor();

        spriteData["position"] = { pos.x, pos.y };
        spriteData["size"] = { size.x, size.y };
        spriteData["anchor"] = { anchor.x, anchor.y };
        spriteData["color"] = { color.x, color.y, color.z, color.w };
        // 必要なら回転なども保存
        // spriteData["rotation"] = sprite->GetRotation();
        // spriteData["visible"] = sprite->IsVisible();

        spriteArray.push_back(spriteData);
    }

    root["sprites"] = spriteArray;

    std::ofstream file(filename);
    if (file.is_open()) {
        file << root.dump(4); // 整形して出力
        file.close();
        OutputDebugStringA(("Saved sprite layout to " + filename + "\n").c_str());
    } else {
        OutputDebugStringA(("Failed to open " + filename + " for saving.\n").c_str());
    }
}

//マウスオーバー判定ヘルパー
bool SpriteDebugEditor::IsMouseOver(Sprite * sprite) const {
    if (sprite == nullptr) return false;

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
/// <summary>
/// スプライトエディタがマウスを（ギズモ操作で）使用中か
/// </summary>
bool SpriteDebugEditor::IsMouseBusy() const {
    // X軸またはY軸をドラッグ中なら「ビジー(true)」を返す
    return isMovingX_ || isMovingY_;
}