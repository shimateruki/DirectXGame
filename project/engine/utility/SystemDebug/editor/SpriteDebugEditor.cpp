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
#include <algorithm>
#include"winapp.h"
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
        strcpy_s(currentSpriteFilename_, sizeof(currentSpriteFilename_), "Please_Select_Or_Set.json");
        lastUpdatedScene_ = currentScene;
    }
    static std::string s_lastSyncedSpriteFilename = "";
    std::string currentLoadedName = currentScene->GetLoadedSpriteFilename();

    // 読み込み名が変わった瞬間だけ、エディタにセットする！
    if (!currentLoadedName.empty() && s_lastSyncedSpriteFilename != currentLoadedName) {
        SetSpriteFilename(currentLoadedName);
        s_lastSyncedSpriteFilename = currentLoadedName;
    }
    if (selectedSprite_ != nullptr) {
        // 文字入力中（名前の変更中など）でなければショートカットを許可
        if (!ImGui::GetIO().WantCaptureKeyboard) {

            // --- 削除 (Delete) ---
            if (inputManager_->IsKeyTriggered(DIK_DELETE)) {
                auto& sprites = currentScene->GetSprites();
                sprites.erase(std::remove_if(sprites.begin(), sprites.end(),
                    [this](const std::unique_ptr<Sprite>& spritePtr) {
                        return spritePtr.get() == selectedSprite_;
                    }), sprites.end());
                selectedSprite_ = nullptr;
            }
            // 削除された場合はここで抜ける
            if (selectedSprite_ == nullptr) goto SKIP_SHORTCUTS;

            // --- 複製 (Ctrl + D または Ctrl + C) ---
            // ※ InputManager の「キーを押しっぱなし」を判定する関数名（IsKeyPressed など）に合わせてください
            bool isCtrlDown = inputManager_->IsKeyPressed(DIK_LCONTROL) || inputManager_->IsKeyPressed(DIK_RCONTROL);

            // Ctrl + C (コピー) または Ctrl + D (複製)
            if (isCtrlDown && (inputManager_->IsKeyTriggered(DIK_C) || inputManager_->IsKeyTriggered(DIK_D))) {

                auto newSprite = std::make_unique<Sprite>();

                // 元のスプライトからテクスチャを取得して初期化
                uint32_t handle = selectedSprite_->GetTextureHandle();
                newSprite->Initialize(currentScene->GetSpriteCommon(), handle);

                // 元のスプライトのパラメータを丸コピー
                newSprite->SetName(selectedSprite_->GetName() + "_Copy");
                // ※完全に重なると見えないので、少しズラして配置する
                newSprite->SetPosition({ selectedSprite_->GetPosition().x + 20.0f, selectedSprite_->GetPosition().y + 20.0f });
                newSprite->SetSize(selectedSprite_->GetSize());
                newSprite->SetAnchorPoint(selectedSprite_->GetAnchorPoint());
                newSprite->SetColor(selectedSprite_->GetColor());
                // (もしフリップや回転などがあれば、ここに追加でコピー処理を書きます)

                // シーンのリストに追加
                auto& sprites = currentScene->GetSprites();
                sprites.push_back(std::move(newSprite));

                // 複製したスプライトを即座に「選択状態」にする（続けて移動などができるように）
                selectedSprite_ = sprites.back().get();
            }
            if (!selectedSprite_->IsLocked()) {

                // IsKeyPressed を使うと「押しっぱなし」で滑らかに動きます。
                // Shiftキーを押している間は 5.0 ピクセル/フレームの高速移動、
                // 通常時は 0.5 ピクセル/フレームの微細な移動にします。
                float nudgeStep = (inputManager_->IsKeyPressed(DIK_LSHIFT) || inputManager_->IsKeyPressed(DIK_RSHIFT)) ? 5.0f : 0.5f;

                Vector2 pos = selectedSprite_->GetPosition();
                bool isNudged = false;

                if (inputManager_->IsKeyPressed(DIK_UP)) { pos.y -= nudgeStep; isNudged = true; }
                if (inputManager_->IsKeyPressed(DIK_DOWN)) { pos.y += nudgeStep; isNudged = true; }
                if (inputManager_->IsKeyPressed(DIK_LEFT)) { pos.x -= nudgeStep; isNudged = true; }
                if (inputManager_->IsKeyPressed(DIK_RIGHT)) { pos.x += nudgeStep; isNudged = true; }

                if (isNudged) {
                    selectedSprite_->SetPosition(pos);
                }
            }
        }
    }
SKIP_SHORTCUTS:
 
    if (!isHovered && !isMovingX_ && !isMovingY_) return;

    // --- 1. ギズモ操作 (簡易移動) ---
    if (selectedSprite_) {
        // ★ 変更: ロックされていない時だけドラッグを開始できる
        if (!selectedSprite_->IsLocked()) {
            // マウスが押された瞬間
            if (inputManager_->IsMouseButtonTriggered(0) && IsMouseOver(selectedSprite_, localMousePos)) {
                isMovingX_ = true;
                isMovingY_ = true;
                dragStartMousePos_ = localMousePos;
                dragStartSpritePos_ = selectedSprite_->GetPosition();
            }

            // ドラッグ中
            if (inputManager_->IsMouseButtonPressed(0) && (isMovingX_ || isMovingY_)) {
                Vector2 delta = { localMousePos.x - dragStartMousePos_.x, localMousePos.y - dragStartMousePos_.y };
                Vector2 newPos = { dragStartSpritePos_.x + delta.x, dragStartSpritePos_.y + delta.y };

                // Shiftキーを押している間は10ピクセル単位でスナップ
                if (inputManager_->IsKeyPressed(DIK_LSHIFT) || inputManager_->IsKeyPressed(DIK_RSHIFT)) {
                    float snapStep = 10.0f;
                    newPos.x = std::round(newPos.x / snapStep) * snapStep;
                    newPos.y = std::round(newPos.y / snapStep) * snapStep;
                }

                selectedSprite_->SetPosition(newPos);
            }
        } // <- if(!selectedSprite_->IsLocked()) の閉じカッコ

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
            // ★ 変更: ロックされていないスプライトのみ選択可能にする
            if (sprite && !sprite->IsLocked() && IsMouseOver(sprite, localMousePos)) {
                selectedSprite_ = sprite;
                hit = true;
                break;
            }
        }
        if (!hit) selectedSprite_ = nullptr;
    }
    
#endif
}

// =========================================================================
// 大元の描画関数（これを呼べば3つのウィンドウがすべて描画される）
// =========================================================================
void SpriteDebugEditor::DrawImGui() {
#ifdef USE_IMGUI
    DrawHierarchyWindow();
    DrawInspectorWindow();
    DrawProjectWindow();
#endif
}

// =========================================================================
// 1. Hierarchy (左パネル) の描画
// =========================================================================
void SpriteDebugEditor::DrawHierarchyWindow() {
#ifdef USE_IMGUI
    if (!sceneManager_) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (!currentScene) return;

    SpriteCommon* spriteCommon = currentScene->GetSpriteCommon();
    if (!spriteCommon) return;
    auto& sprites = currentScene->GetSprites();

    ImGui::Begin(ICON_FA_LIST_UL " Sprite Hierarchy");

    // --- ファイル管理 ---
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
            // LoadSpriteLayout(fullPath); // 実装に合わせて呼び出し
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    // --- スプライトリスト ---
    ImGui::Text(ICON_FA_IMAGES " Sprite List");
    ImGui::BeginChild("SpriteListRegion", ImVec2(0, 0), true);

    int id = 0;
    if (sprites.empty()) {
        ImGui::TextDisabled(ICON_FA_INFO_CIRCLE " スプライトがありません");
    }

    for (const auto& spritePtr : sprites) {
        Sprite* sprite = spritePtr.get();
        if (!sprite) continue;

        ImGui::PushID(id++);

        // 目玉アイコン
        bool isVisible = sprite->IsVisible();
        const char* eyeIcon = isVisible ? ICON_FA_EYE : ICON_FA_EYE_SLASH;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, isVisible ? ImVec4(1, 1, 1, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1));
        if (ImGui::Button(eyeIcon)) sprite->SetVisible(!isVisible);
        ImGui::PopStyleColor(2);

        ImGui::SameLine();

        // 鍵アイコン
        bool isLocked = sprite->IsLocked();
        const char* lockIcon = isLocked ? ICON_FA_LOCK : ICON_FA_UNLOCK;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, isLocked ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        std::string lockButtonId = std::string(lockIcon) + "##Lock" + std::to_string(id);
        if (ImGui::Button(lockButtonId.c_str())) sprite->SetLocked(!isLocked);
        ImGui::PopStyleColor(2);

        ImGui::SameLine();

        // リスト選択
        std::string label = (sprite->GetName().empty()) ? "Sprite " + std::to_string(id) : sprite->GetName();
        std::string iconLabel = std::string(ICON_FA_IMAGE " ") + label;

        if (!isVisible) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

        bool isSelected = (selectedSprite_ == sprite);
        if (ImGui::Selectable(iconLabel.c_str(), isSelected)) selectedSprite_ = sprite;

        if (!isVisible) ImGui::PopStyleColor();

        ImGui::PopID();
    }

    // ドラッグ＆ドロップで新規作成
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE_FILE")) {
            const char* droppedFilename = (const char*)payload->Data;
            auto newSprite = std::make_unique<Sprite>();
            uint32_t handle = Sprite::LoadTexture(droppedFilename);
            newSprite->Initialize(spriteCommon, handle);
            newSprite->SetName(droppedFilename);
            newSprite->SetTextureName(droppedFilename);
            sprites.push_back(std::move(newSprite));
            selectedSprite_ = sprites.back().get();
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::EndChild();
    ImGui::End();
#endif
}

// =========================================================================
// 2. Inspector (右パネル) の描画
// =========================================================================
void SpriteDebugEditor::DrawInspectorWindow() {
#ifdef USE_IMGUI
    if (!sceneManager_) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (!currentScene) return;

    auto& sprites = currentScene->GetSprites();

    ImGui::Begin(ICON_FA_INFO_CIRCLE " Sprite Inspector");
    if (selectedSprite_) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), ICON_FA_EDIT " 編集: %s", selectedSprite_->GetName().c_str());

        // 描画順（Zオーダー）
        auto it = std::find_if(sprites.begin(), sprites.end(),
            [this](const std::unique_ptr<Sprite>& ptr) { return ptr.get() == selectedSprite_; });
        if (it != sprites.end()) {
            size_t index = std::distance(sprites.begin(), it);
            if (ImGui::Button(ICON_FA_ARROW_UP " 前面へ (手前)")) {
                if (index < sprites.size() - 1) std::swap(sprites[index], sprites[index + 1]);
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_ARROW_DOWN " 背面へ (奥)")) {
                if (index > 0) std::swap(sprites[index], sprites[index - 1]);
            }
        }
        ImGui::Separator();

        // 名前編集
        char nameBuffer[256] = { 0 };
        strcpy_s(nameBuffer, selectedSprite_->GetName().c_str());
        if (ImGui::InputText(ICON_FA_PEN " 名前 (Name)", nameBuffer, sizeof(nameBuffer))) {
            selectedSprite_->SetName(nameBuffer);
        }

        // 基本パラメータ
        Vector2 pos = selectedSprite_->GetPosition();
        Vector2 size = selectedSprite_->GetSize();
        Vector2 anchor = selectedSprite_->GetAnchorPoint();
        Vector4 color = selectedSprite_->GetColor();

        if (ImGui::DragFloat2(ICON_FA_ARROWS_ALT " 座標 (Pos)", &pos.x, 1.0f)) selectedSprite_->SetPosition(pos);
        if (ImGui::DragFloat2(ICON_FA_EXPAND_ARROWS_ALT " サイズ (Size)", &size.x, 1.0f)) selectedSprite_->SetSize(size);
        if (ImGui::DragFloat2(ICON_FA_ANCHOR " アンカー (Anchor)", &anchor.x, 0.01f)) selectedSprite_->SetAnchorPoint(anchor);
        if (ImGui::ColorEdit4(ICON_FA_PALETTE " 色 (Color)", &color.x)) selectedSprite_->SetColor(color);

        ImGui::Separator();

        // クイック配置
        ImGui::Text(ICON_FA_CROSSHAIRS " クイック配置 (Quick Snap)");
        float gw = (float)WinApp::kClientWidth;
        float gh = (float)WinApp::kClientHeight;
        ImVec2 btnSz(30.0f, 30.0f);
        auto QuickSnapButton = [&](const char* label, float xRatio, float yRatio) {
            if (ImGui::Button(label, btnSz)) {
                selectedSprite_->SetPosition({ gw * xRatio, gh * yRatio });
                selectedSprite_->SetAnchorPoint({ xRatio, yRatio });
            }
            };
        QuickSnapButton("↖", 0.0f, 0.0f); ImGui::SameLine();
        QuickSnapButton("⬆", 0.5f, 0.0f); ImGui::SameLine();
        QuickSnapButton("↗", 1.0f, 0.0f);

        QuickSnapButton("⬅", 0.0f, 0.5f); ImGui::SameLine();
        QuickSnapButton("・", 0.5f, 0.5f); ImGui::SameLine();
        QuickSnapButton("➡", 1.0f, 0.5f);

        QuickSnapButton("↙", 0.0f, 1.0f); ImGui::SameLine();
        QuickSnapButton("⬇", 0.5f, 1.0f); ImGui::SameLine();
        QuickSnapButton("↘", 1.0f, 1.0f);

        ImGui::Separator();

        // 画像差し替え
        ImGui::Text(ICON_FA_IMAGE " テクスチャ変更 (Texture Replace)");
        ImGui::Button("下から画像をここにドロップ！", ImVec2(-1, 30.0f));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE_FILE")) {
                const char* droppedFilename = (const char*)payload->Data;
                uint32_t newHandle = Sprite::LoadTexture(droppedFilename);
                selectedSprite_->SetTextureHandle(newHandle);
                selectedSprite_->SetTextureName(droppedFilename);
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 5.0f));
        if (ImGui::Button(ICON_FA_TIMES_CIRCLE " 選択解除 (Deselect)", ImVec2(-1, 0))) selectedSprite_ = nullptr;

    }
    else {
        ImGui::TextDisabled(ICON_FA_MOUSE_POINTER " リストからスプライトを選択してください");
    }
    ImGui::End();
#endif
}

// =========================================================================
// 3. Project (下パネル / アセットブラウザ) の描画
// =========================================================================
void SpriteDebugEditor::DrawProjectWindow() {
#ifdef USE_IMGUI
    ImGui::Begin(ICON_FA_FOLDER_OPEN " Sprite Assets");
    std::string spriteDirPath = "Resources/sprite/";
    if (std::filesystem::exists(spriteDirPath)) {
        float itemWidth = 120.0f;
        float windowVisibleX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

        for (const auto& entry : std::filesystem::directory_iterator(spriteDirPath)) {
            if (entry.path().extension() == ".png") {
                std::string filename = entry.path().filename().string();
                ImGui::PushID(filename.c_str());
                ImGui::Button(filename.c_str(), ImVec2(itemWidth, 40.0f));

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("SPRITE_FILE", filename.c_str(), filename.size() + 1);
                    ImGui::Text(ICON_FA_PLUS_CIRCLE " 生成/変更: %s", filename.c_str());
                    ImGui::EndDragDropSource();
                }
                ImGui::PopID();

                float lastButtonMaxX = ImGui::GetItemRectMax().x;
                float nextButtonMaxX = lastButtonMaxX + ImGui::GetStyle().ItemSpacing.x + itemWidth;
                if (nextButtonMaxX < windowVisibleX) ImGui::SameLine();
            }
        }
    }
    else {
        ImGui::TextDisabled("フォルダが見つかりません: %s", spriteDirPath.c_str());
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
        Vector4 color = sprite->GetColor();
        spriteData["position"] = { pos.x, pos.y };
        spriteData["size"] = { size.x, size.y };
        spriteData["anchor"] = { anchor.x, anchor.y };
        spriteData["color"] = { color.x, color.y, color.z };
        spriteData["texture"] = sprite->GetTextureName();
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
