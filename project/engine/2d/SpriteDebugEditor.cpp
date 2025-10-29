#include "SpriteDebugEditor.h"
#include "engine/scene/GamePlayScene.h" // GamePlayScene にアクセスするため
#include "engine/2d/Sprite.h"          // Sprite にアクセスするため
#include "externals/imgui/imgui.h"     // ImGui を使うため
#include "externals/nlohmann/json.hpp" // JSON保存用
#include <fstream>                     // ファイル出力用
#include <string>                      // std::string 用

void SpriteDebugEditor::Initialize(GamePlayScene* scene) {
    scene_ = scene;
    selectedSprite_ = nullptr;
}

void SpriteDebugEditor::Finalize() {
    // 今は特に何もしない
}

void SpriteDebugEditor::Update() {
    if (!scene_) return;

    // --- スプライトリストウィンドウ ---
    ImGui::Begin("Sprite List");
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        ImGui::SetNextFrameWantCaptureKeyboard(true); // ウィンドウフォーカス中はキー入力を ImGui が優先
    }

    // ★ GamePlayScene からスプライトリストを取得 (GetSprites() 関数が必要)
    std::vector<std::unique_ptr<Sprite>>& sprites = scene_->GetSprites();

    int spriteIndex = 0;
    for (auto& sprite : sprites) {
        if (!sprite) continue;

        // ★ スプライトに名前がない場合はインデックスを表示 (GetName() 関数が必要)
        std::string displayName = sprite->GetName();
        if (displayName.empty()) {
            displayName = "Sprite_" + std::to_string(spriteIndex);
        }

        if (ImGui::Selectable(displayName.c_str(), sprite.get() == selectedSprite_)) {
            selectedSprite_ = sprite.get();
        }
        spriteIndex++;
    }
    ImGui::End(); // Sprite List

    // --- インスペクターウィンドウ ---
    ImGui::Begin("Sprite Inspector");
    if (selectedSprite_) {
        // ★ 選択中のスプライトの名前を表示 (GetName() が必要)
        std::string selectedName = selectedSprite_->GetName();
        if (selectedName.empty()) selectedName = "Unnamed Sprite";
        ImGui::Text("Selected: %s", selectedName.c_str());
        ImGui::Separator();

        // --- プロパティ編集 ---
        // (Sprite クラスに Get/Set 関数が必要)
        Vector2 position = selectedSprite_->GetPosition();
        if (ImGui::DragFloat2("Position", &position.x, 1.0f)) {
            selectedSprite_->SetPosition(position);
        }

        Vector2 size = selectedSprite_->GetSize();
        if (ImGui::DragFloat2("Size", &size.x, 1.0f)) {
            selectedSprite_->SetSize(size);
        }

        Vector2 anchor = selectedSprite_->GetAnchorPoint();
        if (ImGui::DragFloat2("Anchor Point", &anchor.x, 0.01f, 0.0f, 1.0f)) {
            selectedSprite_->SetAnchorPoint(anchor);
        }

        Vector4 color = selectedSprite_->GetColor();
        // ★ ImGui::ColorEdit4 は float 配列を要求するので注意
        float colorArray[4] = { color.x, color.y, color.z, color.w };
        if (ImGui::ColorEdit4("Color", colorArray)) {
            selectedSprite_->SetColor({ colorArray[0], colorArray[1], colorArray[2], colorArray[3] });
        }

        // テクスチャハンドル (表示のみ)
        uint32_t texHandle = selectedSprite_->GetTextureHandle();
        ImGui::Text("Texture Handle: %u", texHandle);
        // (もしテクスチャ名を Sprite が持っていれば表示)
        // ImGui::Text("Texture Name: %s", selectedSprite_->GetTextureName().c_str());

        // 回転 (もし Sprite クラスに回転があれば)
        // float rotation = selectedSprite_->GetRotation();
        // if (ImGui::DragFloat("Rotation (Deg)", &rotation, 1.0f)) {
        //     selectedSprite_->SetRotation(rotation);
        // }

        // 表示フラグ (もしあれば)
        // bool isVisible = selectedSprite_->IsVisible();
        // if (ImGui::Checkbox("Is Visible", &isVisible)) {
        //     selectedSprite_->SetVisible(isVisible);
        // }


        // --- ★ 保存ボタン ---
        ImGui::Separator();
        OutputDebugStringA("Before Save Button\n"); // ★追加
        if (ImGui::Button("Save Sprite Layout")) {
            SaveSpriteLayout("sprite_layout.json"); // JSON ファイルに保存

        }

    } else {
        ImGui::Text("No sprite selected.");
    }
    ImGui::End(); // Sprite Inspector
}

void SpriteDebugEditor::DrawDebug() {
    // 今は実装しない (必要なら選択中のスプライトの枠を描画するなど)
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
