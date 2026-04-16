#include "MeshEffectEditor.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "SceneManager.h" 
#include "BaseScene.h"    
#include <imgui.h>
#include <fstream>
#include "json.hpp" // プロジェクト内のJSONライブラリ
#include <DebugConsole.h>
#include "IconsFontAwesome5.h"
#include <filesystem>
#include <MeshEffectManager.h>

using json = nlohmann::json;
static const char* kEasingNames[] = {
    "Linear (等速)",
    "InSine", "OutSine", "InOutSine",
    "InQuad", "OutQuad", "InOutQuad",
    "InCubic", "OutCubic", "InOutCubic",
    "InQuart", "OutQuart", "InOutQuart",
    "InQuint", "OutQuint", "InOutQuint",
    "InExpo", "OutExpo", "InOutExpo",
    "InCirc", "OutCirc", "InOutCirc",
    "InBack", "OutBack", "InOutBack",
    "InElastic", "OutElastic", "InOutElastic",
    "InBounce", "OutBounce", "InOutBounce"
};
void MeshEffectEditor::Initialize(SceneManager* sceneManager) {
    // ★ 変更: SceneManager のポインタだけ保持する
    sceneManager_ = sceneManager;
    RefreshTextureList();
    RefreshJsonFileList();
}
void MeshEffectEditor::RefreshTextureList() {
    textureFileList_.clear();
    std::string path = "Resources/sprite/"; // テクスチャフォルダのパス

    // 指定フォルダ内のファイルを走査
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.is_regular_file()) {
            // ファイル名だけをリストに追加
            textureFileList_.push_back(entry.path().filename().string());
        }
    }

    // currentTextureIndex_ の安全対策
    if (textureFileList_.empty()) {
        currentTextureIndex_ = -1;
    }
    else {
        // 現在の editTexturePath_ に一致するものを探してインデックスを合わせる
        currentTextureIndex_ = 0; // デフォルト
        std::string currentFileName = std::filesystem::path(editTexturePath_).filename().string();
        for (int i = 0; i < textureFileList_.size(); ++i) {
            if (textureFileList_[i] == currentFileName) {
                currentTextureIndex_ = i;
                break;
            }
        }
    }
    SyncTextureIndices();
}
void MeshEffectEditor::RefreshJsonFileList() {
    jsonFileList_.clear();
    std::string path = "Resources/json/effect/";

    // フォルダが無ければ自動で作る（クラッシュ防止）
    std::filesystem::create_directories(path);

    // フォルダ内の .json ファイルだけをリストアップする
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            jsonFileList_.push_back(entry.path().filename().string());
        }
    }

    currentJsonIndex_ = -1; // 初期状態は未選択（新規作成）
}
void MeshEffectEditor::Update(float deltaTime) {
    if (!sceneManager_) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();

    if (lastScene_ != currentScene) {
        previewEffect_.reset();
        extraPreviewEffects_.clear();
        lastScene_ = currentScene;
        targetObject_ = nullptr;
    }

    if (!previewEffect_ && currentScene && currentScene->GetObject3dCommon()) {
        previewEffect_ = std::make_unique<EffectObject3d>();
        previewEffect_->Initialize(currentScene->GetObject3dCommon());
        previewEffect_->SetModel(editModelName_);
        if (auto renderer = previewEffect_->GetMeshRenderer()) {
            if (strlen(editTexturePath_) > 0) renderer->SetTexture(editTexturePath_);
        }
        previewEffect_->SetColor(editColor_);
        previewEffect_->SetScrollSpeed(editScrollSpeed_);
        previewEffect_->SetIntensity(editIntensity_);
    }

    if (!previewEffect_) return;

    int neededExtras = 0;
    if (editVolumeMode_ == 1) neededExtras = 1;
    else if (editVolumeMode_ == 2) neededExtras = 2;

    while (extraPreviewEffects_.size() < neededExtras) {
        auto extra = std::make_unique<EffectObject3d>();
        if (currentScene && currentScene->GetObject3dCommon()) {
            extra->Initialize(currentScene->GetObject3dCommon());
        }
        extraPreviewEffects_.push_back(std::move(extra));
    }
    while (extraPreviewEffects_.size() > neededExtras) {
        extraPreviewEffects_.pop_back();
    }

    std::vector<EffectObject3d*> activePreviews;
    activePreviews.push_back(previewEffect_.get());
    for (auto& ex : extraPreviewEffects_) activePreviews.push_back(ex.get());

    Vector3 basePos = editPosition_;
    Vector3 baseRot = editRotation_;

    if (targetObject_) {
        Vector3 targetPos = targetObject_->GetWorldPosition();

        // ★修正: ターゲットのY軸回転（向き）だけを取得
        float targetWorldY = 0.0f;
        Object3d* curr = targetObject_;
        while (curr) {
            targetWorldY += curr->GetRotation().y;
            curr = curr->GetParent();
        }

        // ★修正: エディタ設定位置(editPosition_)をターゲットの向きに合わせて回転
        float s = sinf(targetWorldY);
        float c = cosf(targetWorldY);
        Vector3 rotatedOffset;
        rotatedOffset.x = editPosition_.x * c + editPosition_.z * s;
        rotatedOffset.y = editPosition_.y;
        rotatedOffset.z = -editPosition_.x * s + editPosition_.z * c;

        // 基準座標と回転の計算
        basePos.x = targetPos.x + rotatedOffset.x;
        basePos.y = targetPos.y + rotatedOffset.y;
        basePos.z = targetPos.z + rotatedOffset.z;

        // 回転はY軸（向き）だけを足す
        baseRot.y = editRotation_.y + targetWorldY;
    }
    // ========================================================
    // ★ 修正1: 一斉再生の完全同期！
    // ========================================================
    if (forcePlayRequest_ || (isAutoLoop_ && !activePreviews.empty() && !activePreviews[0]->IsPlaying())) {
        for (auto* fx : activePreviews) {
            fx->Play(editLifetime_);
        }
        forcePlayRequest_ = false; // フラグを消化
    }

    Vector3 localZ;
    localZ.x = sinf(baseRot.y) * cosf(baseRot.x);
    localZ.y = -sinf(baseRot.x);
    localZ.z = cosf(baseRot.y) * cosf(baseRot.x);

    float timeStep = (deltaTime <= 0.0001f) ? (1.0f / 60.0f) : deltaTime;

    for (size_t i = 0; i < activePreviews.size(); ++i) {
        auto* fx = activePreviews[i];

        if (editProceduralType_ == 0) {
            fx->SetModel(editModelName_);
        }
        if (auto renderer = fx->GetMeshRenderer()) {
            if (strlen(editTexturePath_) > 0) renderer->SetTexture(editTexturePath_);
        }
        fx->SetBlendMode(static_cast<BlendMode>(currentBlendModeIndex_));
        fx->SetStartScale(editStartScale_);
        fx->SetEndScale(editEndScale_);
        fx->SetStartColor(editStartColor_);
        fx->SetEndColor(editEndColor_);
        fx->SetScrollSpeed(editScrollSpeed_);
        fx->SetIntensity(editIntensity_);

        fx->SetDistortionStrength(editDistortionStrength_);
        fx->SetDistortionSpeed(editDistortionSpeed_);
        fx->SetEdgeFadeStrength(editEdgeFadeStrength_);
        fx->SetEnableDistortion(editEnableDistortion_);
        fx->SetEnableReveal(editEnableReveal_);
        fx->SetEasingType(editEasingType_);
        fx->SetProceduralType(editProceduralType_);

        bool useRamp = (strlen(editRampTexturePath_) > 0);
        bool useNoise = (strlen(editNoiseTexturePath_) > 0);
        fx->SetEnableColorRamp(useRamp);
        fx->SetEnableNoiseTexture(useNoise);

        if (useNoise) fx->SetNoiseTexture(TextureManager::GetInstance()->Load(editNoiseTexturePath_));
        if (useRamp) fx->SetRampTexture(TextureManager::GetInstance()->Load(editRampTexturePath_));

        Vector3 finalPos = basePos;
        Vector3 finalRot = baseRot;

        if (editVolumeMode_ == 1 && i == 1) {
            finalRot.x += 1.570796f;
        }
        else if (editVolumeMode_ == 2) {
            float gap = 0.02f;
            if (i == 1) { finalPos.x += localZ.x * gap; finalPos.y += localZ.y * gap; finalPos.z += localZ.z * gap; }
            if (i == 2) { finalPos.x -= localZ.x * gap; finalPos.y -= localZ.y * gap; finalPos.z -= localZ.z * gap; }
        }

        fx->SetTranslate(finalPos);
        fx->SetRotation(finalRot);

        fx->Update(timeStep);
        fx->UpdateLocalMatrix();
        fx->UpdateWorldMatrix();
    }
}
void MeshEffectEditor::Draw() {
    // ★ 1. Game側から呼ばれているか確認
    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (lastScene_ != currentScene || !currentScene) {
        return;
    }
    if (previewEffect_) {
        previewEffect_->Draw();
        for (auto& ex : extraPreviewEffects_) {
            ex->Draw();
        }
    }
    else {
        // ★ もし生成されていなければエラーを出す
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "Error: previewEffect_ is NULL!");
    }
}

void MeshEffectEditor::DrawImGui() {
#ifdef USE_IMGUI
    if (!previewEffect_) return;

    // ウィンドウの横幅を取得
    float availWidth = ImGui::GetContentRegionAvail().x;

    // ==========================================
    // 1. エフェクト再生コントロール
    // ==========================================
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button(ICON_FA_PLAY " エフェクト再生 (ATTACK!)", ImVec2(availWidth, 40))) {
        forcePlayRequest_ = true;
    }
    ImGui::PopStyleColor();

    ImGui::Checkbox("自動ループ (Auto Loop)", &isAutoLoop_);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_UNDO " 時間リセット")) {
        previewEffect_->ResetTime();
        for (auto& ex : extraPreviewEffects_) ex->ResetTime(); 
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Separator();
    ImGui::Text(ICON_FA_CROSSHAIRS " --- ターゲット追従 (Target Tracking) ---");

    // SceneManager経由で現在のシーンのオブジェクトリストを取得
    if (sceneManager_ && sceneManager_->GetCurrentScene()) {
        auto& objects = sceneManager_->GetCurrentScene()->GetObjects();

        // 現在選ばれているターゲットの名前
        std::string currentTargetName = targetObject_ ? targetObject_->GetName() : "なし (None)";

        if (ImGui::BeginCombo("追従対象", currentTargetName.c_str())) {
            // 「追従解除（None）」の選択肢
            if (ImGui::Selectable("なし (None)", targetObject_ == nullptr)) {
                targetObject_ = nullptr;
            }

            // シーン内の全オブジェクトをリストアップ
            for (auto& obj : objects) {
                if (!obj) continue;
                bool isSelected = (targetObject_ == obj.get());

                if (ImGui::Selectable(obj->GetName().c_str(), isSelected)) {
                    targetObject_ = obj.get();
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Separator();

    // ==========================================
    // 2. ブレンドモード設定
    // ==========================================
    const char* blendModeNames[] = { "なし (None)", "通常 (半透明)", "加算発光 (Add)", "減算 (Subtract)", "乗算 (Multiply)", "スクリーン (Screen)" };
    if (ImGui::Combo(ICON_FA_ADJUST " ブレンドモード", &currentBlendModeIndex_, blendModeNames, IM_ARRAYSIZE(blendModeNames))) {
        previewEffect_->SetBlendMode(static_cast<BlendMode>(currentBlendModeIndex_));
    }
    ImGui::Separator();
    ImGui::Text(ICON_FA_CUBES " [ 立体化モード (Volume Mode) ]");
    const char* volumeModes[] = { "0: なし (None)", "1: 十字クロス (Cross)", "2: 3枚重ね (Layer)" };
    ImGui::Combo("立体化", &editVolumeMode_, volumeModes, IM_ARRAYSIZE(volumeModes));

    ImGui::Checkbox(ICON_FA_RULER_HORIZONTAL " 伸びるアニメーション (Reveal Mode)", &editEnableReveal_);
    if (ImGui::Combo(ICON_FA_CHART_LINE " イージング (Easing)", &editEasingType_, kEasingNames, IM_ARRAYSIZE(kEasingNames))) {
        previewEffect_->SetEasingType(editEasingType_);
    }
    ImGui::Separator();
    const char* procTypes[] = { "0: 外部モデル (Tex)", "1: 斜め切り (Slash)", "2: 回転切り (Spin)", "3: 突き (Thrust)" };
    if (ImGui::Combo("エフェクト形状", &editProceduralType_, procTypes, IM_ARRAYSIZE(procTypes))) {
        previewEffect_->SetProceduralType(editProceduralType_);

        // ★ 回転切りを選んだ時、自動で360度以上にしておく親切設計
        if (editProceduralType_ == 2 && previewEffect_->editSlashAngle_ < 360.0f) {
            previewEffect_->editSlashAngle_ = 400.0f; // 少し余分に回して透明部分を隠す
        }

        previewEffect_->UpdateProceduralMesh();
        for (auto& ex : extraPreviewEffects_) {
            ex->SetProceduralType(editProceduralType_);
            ex->UpdateProceduralMesh();
        }
    }

    if (editProceduralType_ >= 1) {
        ImGui::Indent();
        bool changed = false;

        // ★ 1(Slash)と2(Spin)の両方で角度や厚みをいじれるようにする
        if (editProceduralType_ == 1 || editProceduralType_ == 2) {
            // スライダーの上限を 1080度（3周）に解放！
            changed |= ImGui::SliderFloat("斬撃の角度", &previewEffect_->editSlashAngle_, 30.0f, 1080.0f);
            changed |= ImGui::SliderFloat("内側の半径", &previewEffect_->editInnerRadius_, 0.0f, 5.0f);
            changed |= ImGui::SliderFloat("外側の半径", &previewEffect_->editOuterRadius_, 1.0f, 15.0f);
            changed |= ImGui::SliderFloat("軌跡の厚み", &previewEffect_->editThickness_, 0.01f, 3.0f);
            changed |= ImGui::SliderFloat("螺旋の高さ(Zズレ)", &previewEffect_->editSpiralPitch_, -5.0f, 5.0f); // ★追加
        }
        else if (editProceduralType_ == 3) {
            changed |= ImGui::SliderFloat("突きの長さ", &previewEffect_->editThrustLength_, 1.0f, 20.0f);
            changed |= ImGui::SliderFloat("根元の太さ", &previewEffect_->editThrustRadius_, 0.1f, 5.0f);
        }

        changed |= ImGui::SliderInt("ポリゴン分割数", &previewEffect_->editMeshSegments_, 4, 128);

        if (changed) {
            previewEffect_->UpdateProceduralMesh();
            for (auto& ex : extraPreviewEffects_) {
                ex->editSlashAngle_ = previewEffect_->editSlashAngle_;
                ex->editInnerRadius_ = previewEffect_->editInnerRadius_;
                ex->editOuterRadius_ = previewEffect_->editOuterRadius_;
                ex->editThickness_ = previewEffect_->editThickness_;
                ex->editSpiralPitch_ = previewEffect_->editSpiralPitch_; 
                ex->editThrustLength_ = previewEffect_->editThrustLength_;
                ex->editThrustRadius_ = previewEffect_->editThrustRadius_;
                ex->editMeshSegments_ = previewEffect_->editMeshSegments_;
                ex->UpdateProceduralMesh();
            }
        }
        ImGui::Unindent();
    }
    // ==========================================
    // 3. リソース設定 (メッシュ・テクスチャ)
    // ==========================================
    if (ImGui::CollapsingHeader(ICON_FA_CUBES " リソース設定 (Resources)", ImGuiTreeNodeFlags_DefaultOpen)) {

        // --- 共通のテクスチャ選択ヘルパー (ラムダ式) ---
        auto TextureCombo = [&](const char* label, int& currentIndex, char* targetPath, auto callback) {
            const char* previewName = (currentIndex >= 0 && currentIndex < (int)textureFileList_.size())
                ? textureFileList_[currentIndex].c_str() : "なし (None)";

            if (ImGui::BeginCombo(label, previewName)) {
                // 「なし」の選択
                if (ImGui::Selectable("なし (None)", currentIndex == -1)) {
                    currentIndex = -1;
                    targetPath[0] = '\0';
                    callback("");
                }
                // リストから選択
                for (int i = 0; i < (int)textureFileList_.size(); ++i) {
                    if (ImGui::Selectable(textureFileList_[i].c_str(), currentIndex == i)) {
                        currentIndex = i;
                        std::string fullPath = "Resources/sprite/" + textureFileList_[i];
                        strncpy_s(targetPath, 256, fullPath.c_str(), _TRUNCATE);
                        callback(fullPath);
                    }
                }
                ImGui::EndCombo();
            }
            };

        // --- メッシュ選択 (UI修正) ---
        if (editProceduralType_ == 0) {
            std::vector<std::string> modelNames = ModelManager::GetInstance()->GetLoadedModelNames();
            if (ImGui::BeginCombo(ICON_FA_CUBE " メッシュ選択", editModelName_)) {
                for (const auto& name : modelNames) {
                    bool isSelected = (name == editModelName_);
                    if (ImGui::Selectable(name.c_str(), isSelected)) {
                        strncpy_s(editModelName_, name.c_str(), sizeof(editModelName_) - 1);
                        previewEffect_->SetModel(editModelName_);
                    }
                }
                ImGui::EndCombo();
            }
        }
        else {
            // プロシージャル使用時は無効化されていることを明記する
            ImGui::TextDisabled(ICON_FA_CUBE " メッシュ選択 (無効)");
            ImGui::TextDisabled("※プロシージャル生成を使用しているためモデル選択は不要です");
        }

        // --- ① メインテクスチャ (t0) ---
        if (editProceduralType_ == 0) {
            TextureCombo(ICON_FA_IMAGE " メインテクスチャ", currentTextureIndex_, editTexturePath_, [&](const std::string& path) {
                if (auto renderer = previewEffect_->GetMeshRenderer()) {
                    renderer->SetTexture(path);
                }
                });
        }
        else {
            // プロシージャル使用時は無効化されていることを明記する
            ImGui::TextDisabled(ICON_FA_IMAGE " メインテクスチャ (無効)");
            ImGui::TextDisabled("※シェーダーで形を生成しているため不要です");
        }

        // --- ② ノイズテクスチャ (t2) ---
    
        if (editProceduralType_ != 1) {
            TextureCombo(ICON_FA_WIND " ノイズテクスチャ", currentNoiseTextureIndex_, editNoiseTexturePath_, [&](const std::string& path) {
                uint32_t handle = path.empty() ? 0 : TextureManager::GetInstance()->Load(path);
                previewEffect_->SetNoiseTexture(handle);
                });
        }
        else {
            ImGui::TextDisabled(ICON_FA_WIND " ノイズテクスチャ (無効)");
            ImGui::TextDisabled("※プロシージャルで斬撃の筋(ノイズ)を自動生成しています");
        }

        // --- ③ カラーランプ (t3) ---
        TextureCombo(ICON_FA_PALETTE " カラーランプ", currentRampTextureIndex_, editRampTexturePath_, [&](const std::string& path) {
            uint32_t handle = path.empty() ? 0 : TextureManager::GetInstance()->Load(path);
            previewEffect_->SetRampTexture(handle);
            });
    }

    // ==========================================
    // 4. 配置設定 (Transform)
    // ==========================================
    if (ImGui::CollapsingHeader(ICON_FA_ARROWS_ALT " 配置設定 (Transform)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("座標 (Position)", &editPosition_.x, 0.1f);
        ImGui::DragFloat3("回転 (Rotation)", &editRotation_.x, 0.01f);
    }

    // ==========================================
    // 5. アニメーション設定
    // ==========================================
    if (ImGui::CollapsingHeader(ICON_FA_FILM " アニメーション設定", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat(ICON_FA_CLOCK " 寿命 (秒)", &editLifetime_, 0.05f, 3.0f);

        ImGui::Spacing();
        ImGui::Text(ICON_FA_EXPAND_ARROWS_ALT " [ スケール (Scale) ]");
        ImGui::DragFloat3("開始スケール", &editStartScale_.x, 0.1f);
        ImGui::DragFloat3("終了スケール", &editEndScale_.x, 0.1f);

        ImGui::Spacing();

        // ★ カラーランプが設定されているかチェック
        if (strlen(editRampTexturePath_) == 0) {
            ImGui::Text(ICON_FA_TINT " [ カラー (Color) ]");
            ImGui::ColorEdit4("開始カラー", &editStartColor_.x);
            ImGui::ColorEdit4("終了カラー", &editEndColor_.x);
        }
        else {
            // カラーランプ使用時は非表示にし、理由を明記する
            ImGui::TextDisabled(ICON_FA_TINT " [ カラー設定無効 ]");
            ImGui::TextDisabled("※カラーランプが適用されているため無効化されています");
        }
    }

    // ==========================================
    // 6. シェーダーパラメータ
    // ==========================================
    if (ImGui::CollapsingHeader(ICON_FA_SLIDERS_H " シェーダーパラメータ", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat2("スクロール速度", &editScrollSpeed_.x, 0.01f);
        ImGui::DragFloat("発光強度 (HDR)", &editIntensity_, 0.01f, 0.0f, 100.0f);

        ImGui::Separator();

        ImGui::Text(ICON_FA_WATER " [ 背景歪み (Distortion) ]");
        ImGui::Checkbox("背景歪みを有効にする", &editEnableDistortion_);
        ImGui::SliderFloat("歪みの強さ", &editDistortionStrength_, 0.0f, 0.2f);
        ImGui::SliderFloat("歪みの速度", &editDistortionSpeed_, 0.0f, 50.0f);

        ImGui::Spacing();

        ImGui::Text(ICON_FA_CUT " [ エッジフェード (形状削り出し) ]");
        ImGui::SliderFloat("削り出しの強さ", &editEdgeFadeStrength_, 1.0f, 10.0f);
    }
    if (ImGui::CollapsingHeader("Collision Settings")) {
        ImGui::Checkbox("Has Collision", &previewEffect_->editHasCollision_);
        if (previewEffect_->editHasCollision_) {
            ImGui::Combo("Shape", &previewEffect_->editCollisionShape_, "Sphere\0AABB\0OBB\0Cylinder\0\0");
            ImGui::DragFloat3("Size/Radius", &previewEffect_->editCollisionSize_.x, 0.1f);
            ImGui::DragFloat3("Offset", &previewEffect_->editCollisionOffset_.x, 0.1f);

            // スライダーを動かした瞬間、実際のコライダー設定に反映させる！
            ColliderType cType = ColliderType::kNone;
            if (previewEffect_->editCollisionShape_ == 0) cType = ColliderType::kSphere;
            else if (previewEffect_->editCollisionShape_ == 1) cType = ColliderType::kAABB;
            else if (previewEffect_->editCollisionShape_ == 2) cType = ColliderType::kOBB;
            else if (previewEffect_->editCollisionShape_ == 3) cType = ColliderType::kCylinder;
            previewEffect_->SetColliderType(cType);

            Object3d::ColliderConfig cConfig;
            cConfig.size = previewEffect_->editCollisionSize_;
            cConfig.center = previewEffect_->editCollisionOffset_;
            cConfig.rotation = { 0.0f, 0.0f, 0.0f };
            previewEffect_->SetColliderConfig(cConfig);
        }
        else {
            // チェックを外したら判定を消す
            previewEffect_->SetColliderType(ColliderType::kNone);
        }

        // ★マネージャーに「今プレビューしてるエフェクト」を教える
        MeshEffectManager::GetInstance()->SetPreviewEffectForDebug(previewEffect_.get());
    }
    // ==========================================
       // 7. 保存と読み込み (Save & Load)
       // ==========================================
    if (ImGui::CollapsingHeader(ICON_FA_SAVE " 保存と読み込み (Save & Load)", ImGuiTreeNodeFlags_DefaultOpen)) {

        // ：既存ファイルのプルダウン選択
        const char* previewValue = (currentJsonIndex_ >= 0 && currentJsonIndex_ < (int)jsonFileList_.size())
            ? jsonFileList_[currentJsonIndex_].c_str() : "新規作成 (New File)";

        if (ImGui::BeginCombo(ICON_FA_FILE_ALT " 既存ファイル", previewValue)) {
            // 「新規作成」を選ぶと入力欄をクリアする
            if (ImGui::Selectable("新規作成 (New File)", currentJsonIndex_ == -1)) {
                currentJsonIndex_ = -1;
                saveFileName_[0] = '\0'; // ファイル名を空にする
            }

            // 既存ファイル一覧
            for (int i = 0; i < (int)jsonFileList_.size(); ++i) {
                if (ImGui::Selectable(jsonFileList_[i].c_str(), currentJsonIndex_ == i)) {
                    currentJsonIndex_ = i;
                    // ★ 選択したファイル名をテキスト入力欄に自動でコピーする！
                    strncpy_s(saveFileName_, jsonFileList_[i].c_str(), sizeof(saveFileName_) - 1);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();

        // ここは今まで通り（プルダウンと連動して文字が入ります）
        ImGui::InputText("ファイル名", saveFileName_, sizeof(saveFileName_));

        float halfWidth = (availWidth - ImGui::GetStyle().ItemSpacing.x) / 2.0f;

        // ★ 保存ボタン
        if (ImGui::Button(ICON_FA_SAVE " JSON 保存", ImVec2(halfWidth, 0))) {
            SaveToJson();
            RefreshJsonFileList(); // 保存したらリストを最新に更新する！
        }
        ImGui::SameLine();

        // ★ 読込ボタン
        if (ImGui::Button(ICON_FA_FOLDER_OPEN " JSON 読込", ImVec2(halfWidth, 0))) {
            LoadFromJson();
        }
    }
#endif
}
// ==========================================
// JSON 保存処理
// ==========================================
void MeshEffectEditor::SaveToJson() {
    std::string directoryPath = "Resources/json/effect/";

    // フォルダが無ければ自動で作ってくれる魔法の1行！
    std::filesystem::create_directories(directoryPath);

    // ファイル名と合体させてフルパスを作る
    std::string fullPath = directoryPath + saveFileName_;

    json j;
    if (targetObject_) {
        j["TargetName"] = targetObject_->GetName();
    }
    else {
        j["TargetName"] = "";
    }
    // --- リソース ---
    j["ModelName"] = editModelName_;
    j["TexturePath"] = editTexturePath_;
    j["NoiseTexturePath"] = editNoiseTexturePath_;
    j["RampTexturePath"] = editRampTexturePath_;

    // --- ベース Transform ---
    j["Position"] = { editPosition_.x, editPosition_.y, editPosition_.z };
    j["Rotation"] = { editRotation_.x, editRotation_.y, editRotation_.z };

    // --- アニメーション ---
    j["Lifetime"] = editLifetime_;
    j["AutoLoop"] = isAutoLoop_;

    j["StartScale"] = { editStartScale_.x, editStartScale_.y, editStartScale_.z };
    j["EndScale"] = { editEndScale_.x, editEndScale_.y, editEndScale_.z };

    j["StartColor"] = { editStartColor_.x, editStartColor_.y, editStartColor_.z, editStartColor_.w };
    j["EndColor"] = { editEndColor_.x, editEndColor_.y, editEndColor_.z, editEndColor_.w };

    // --- シェーダーパラメータ ---
    j["ScrollSpeed"] = { editScrollSpeed_.x, editScrollSpeed_.y };
    j["Intensity"] = editIntensity_;
    j["DistortionStrength"] = editDistortionStrength_;
    j["DistortionSpeed"] = editDistortionSpeed_;
    j["EdgeFadeStrength"] = editEdgeFadeStrength_;
    j["EnableDistortion"] = editEnableDistortion_;
    j["BlendMode"] = currentBlendModeIndex_;
    j["EnableReveal"] = editEnableReveal_;
    j["EasingType"] = editEasingType_;
    j["VolumeMode"] = editVolumeMode_;

    // ==========================================
    // プロシージャルパラメータの保存
    // ==========================================
    j["ProceduralType"] = editProceduralType_;
    if (previewEffect_) {
        j["SlashAngle"] = previewEffect_->editSlashAngle_;
        j["InnerRadius"] = previewEffect_->editInnerRadius_;
        j["OuterRadius"] = previewEffect_->editOuterRadius_;
        j["Thickness"] = previewEffect_->editThickness_;
        j["SpiralPitch"] = previewEffect_->editSpiralPitch_;
        j["ThrustLength"] = previewEffect_->editThrustLength_;
        j["ThrustRadius"] = previewEffect_->editThrustRadius_;
        j["MeshSegments"] = previewEffect_->editMeshSegments_;
    }
    j["Collision"]["HasCollision"] = previewEffect_->editHasCollision_;
    j["Collision"]["Shape"] = previewEffect_->editCollisionShape_;
    j["Collision"]["Size"] = { previewEffect_->editCollisionSize_.x, previewEffect_->editCollisionSize_.y, previewEffect_->editCollisionSize_.z };
    j["Collision"]["Offset"] = { previewEffect_->editCollisionOffset_.x, previewEffect_->editCollisionOffset_.y, previewEffect_->editCollisionOffset_.z };
    std::ofstream file(fullPath);
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
        DebugConsole::GetInstance()->AddLog("Saved Effect JSON: " + fullPath);
    }
    else {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "Failed to save Effect JSON: " + fullPath);
    }
}

// ==========================================
// JSON 読み込み処理
// ==========================================
void MeshEffectEditor::LoadFromJson() {
    std::string fullPath = "Resources/json/effect/" + std::string(saveFileName_);

    std::ifstream file(fullPath);
    if (!file.is_open()) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "Failed to load Effect JSON: " + fullPath);
        return;
    }

    json j;
    file >> j;
    file.close();

    // ==========================================
    // 読み込んだデータをUIバッファと実際のプレビューに反映
    // ==========================================

    if (j.contains("ModelName")) {
        std::string modelName = j["ModelName"];
        strncpy_s(editModelName_, modelName.c_str(), sizeof(editModelName_) - 1);
        previewEffect_->SetModel(editModelName_);
    }

    if (j.contains("TexturePath")) {
        std::string texPath = j["TexturePath"];
        strncpy_s(editTexturePath_, texPath.c_str(), sizeof(editTexturePath_) - 1);
        if (auto renderer = previewEffect_->GetMeshRenderer()) {
            if (strlen(editTexturePath_) > 0) {
                renderer->SetTexture(editTexturePath_);
            }
        }
    }

    if (j.contains("NoiseTexturePath")) {
        std::string noisePath = j["NoiseTexturePath"];
        strncpy_s(editNoiseTexturePath_, noisePath.c_str(), sizeof(editNoiseTexturePath_) - 1);
        if (strlen(editNoiseTexturePath_) > 0) {
            uint32_t texHandle = TextureManager::GetInstance()->Load(editNoiseTexturePath_);
            previewEffect_->SetNoiseTexture(texHandle);
        }
    }

    if (j.contains("RampTexturePath")) {
        std::string rampPath = j["RampTexturePath"];
        strncpy_s(editRampTexturePath_, rampPath.c_str(), sizeof(editRampTexturePath_) - 1);
        if (strlen(editRampTexturePath_) > 0) {
            uint32_t texHandle = TextureManager::GetInstance()->Load(editRampTexturePath_);
            previewEffect_->SetRampTexture(texHandle);
        }
    }

    if (j.contains("Position")) {
        editPosition_.x = j["Position"][0];
        editPosition_.y = j["Position"][1];
        editPosition_.z = j["Position"][2];
    }
    if (j.contains("Rotation")) {
        editRotation_.x = j["Rotation"][0];
        editRotation_.y = j["Rotation"][1];
        editRotation_.z = j["Rotation"][2];
    }

    // アニメーション関連の読み込み
    if (j.contains("Lifetime")) editLifetime_ = j["Lifetime"];
    if (j.contains("AutoLoop")) isAutoLoop_ = j["AutoLoop"];

    if (j.contains("StartScale")) {
        editStartScale_.x = j["StartScale"][0];
        editStartScale_.y = j["StartScale"][1];
        editStartScale_.z = j["StartScale"][2];
    }
    if (j.contains("EndScale")) {
        editEndScale_.x = j["EndScale"][0];
        editEndScale_.y = j["EndScale"][1];
        editEndScale_.z = j["EndScale"][2];
    }

    if (j.contains("StartColor")) {
        editStartColor_.x = j["StartColor"][0];
        editStartColor_.y = j["StartColor"][1];
        editStartColor_.z = j["StartColor"][2];
        editStartColor_.w = j["StartColor"][3];
    }
    if (j.contains("EndColor")) {
        editEndColor_.x = j["EndColor"][0];
        editEndColor_.y = j["EndColor"][1];
        editEndColor_.z = j["EndColor"][2];
        editEndColor_.w = j["EndColor"][3];
    }

    if (j.contains("ScrollSpeed")) {
        editScrollSpeed_.x = j["ScrollSpeed"][0];
        editScrollSpeed_.y = j["ScrollSpeed"][1];
        previewEffect_->SetScrollSpeed(editScrollSpeed_);
    }

    if (j.contains("Intensity")) {
        editIntensity_ = j["Intensity"];
        previewEffect_->SetIntensity(editIntensity_);
    }
    if (j.contains("DistortionStrength")) editDistortionStrength_ = j["DistortionStrength"];
    if (j.contains("DistortionSpeed")) editDistortionSpeed_ = j["DistortionSpeed"];
    if (j.contains("EdgeFadeStrength")) editEdgeFadeStrength_ = j["EdgeFadeStrength"];
    if (j.contains("EnableDistortion")) editEnableDistortion_ = j["EnableDistortion"];
    if (j.contains("BlendMode")) currentBlendModeIndex_ = j["BlendMode"];
    if (j.contains("EnableReveal")) editEnableReveal_ = j["EnableReveal"];
    else editEnableReveal_ = true;

    if (j.contains("EasingType")) editEasingType_ = j["EasingType"];
    else editEasingType_ = 0;

    if (j.contains("VolumeMode")) editVolumeMode_ = j["VolumeMode"];
    else editVolumeMode_ = 0;

    // ==========================================
    // ★ 追加: プロシージャルパラメータの読み込み
    // ==========================================
    if (j.contains("ProceduralType")) editProceduralType_ = j["ProceduralType"];
    if (previewEffect_) {
        if (j.contains("SlashAngle")) previewEffect_->editSlashAngle_ = j["SlashAngle"];
        if (j.contains("InnerRadius")) previewEffect_->editInnerRadius_ = j["InnerRadius"];
        if (j.contains("OuterRadius")) previewEffect_->editOuterRadius_ = j["OuterRadius"];
        if (j.contains("Thickness")) previewEffect_->editThickness_ = j["Thickness"];
        if (j.contains("SpiralPitch")) previewEffect_->editSpiralPitch_ = j["SpiralPitch"];
        if (j.contains("ThrustLength")) previewEffect_->editThrustLength_ = j["ThrustLength"];
        if (j.contains("ThrustRadius")) previewEffect_->editThrustRadius_ = j["ThrustRadius"];
        if (j.contains("MeshSegments")) previewEffect_->editMeshSegments_ = j["MeshSegments"];
    }
    if (j.contains("Collision")) {
        previewEffect_->editHasCollision_ = j["Collision"]["HasCollision"];
        previewEffect_->editCollisionShape_ = j["Collision"]["Shape"];
        previewEffect_->editCollisionSize_ = { j["Collision"]["Size"][0], j["Collision"]["Size"][1], j["Collision"]["Size"][2] };
        previewEffect_->editCollisionOffset_ = { j["Collision"]["Offset"][0], j["Collision"]["Offset"][1], j["Collision"]["Offset"][2] };
    }
    targetObject_ = nullptr;
    if (j.contains("TargetName")) {
        std::string targetName = j["TargetName"];
        if (!targetName.empty() && sceneManager_ && sceneManager_->GetCurrentScene()) {
            auto& objects = sceneManager_->GetCurrentScene()->GetObjects();

            // シーン内を再帰的に探すヘルパー関数
            std::function<Object3d* (Object3d*, const std::string&)> findObj = [&](Object3d* obj, const std::string& name) -> Object3d* {
                if (!obj) return nullptr;
                if (obj->GetName() == name) return obj;
                for (auto* child : obj->GetChildren()) {
                    Object3d* found = findObj(child, name);
                    if (found) return found;
                }
                return nullptr;
                };

            for (auto& obj : objects) {
                targetObject_ = findObj(obj.get(), targetName);
                if (targetObject_) break; // 見つけたら終了
            }
        }
    }

    // ★読み込み完了後、新しい設定値でエフェクトを最初から再生し直す
    forcePlayRequest_ = true;
    SyncTextureIndices();


    if (editProceduralType_ >= 1 && previewEffect_) {
        previewEffect_->SetProceduralType(editProceduralType_);
        previewEffect_->UpdateProceduralMesh();

        for (auto& ex : extraPreviewEffects_) {
            ex->SetProceduralType(editProceduralType_);
            // パラメータを同期
            ex->editSlashAngle_ = previewEffect_->editSlashAngle_;
            ex->editInnerRadius_ = previewEffect_->editInnerRadius_;
            ex->editOuterRadius_ = previewEffect_->editOuterRadius_;
            ex->editThickness_ = previewEffect_->editThickness_;
            ex->editSpiralPitch_ = previewEffect_->editSpiralPitch_;
            ex->editThrustLength_ = previewEffect_->editThrustLength_;
            ex->editThrustRadius_ = previewEffect_->editThrustRadius_;
            ex->editMeshSegments_ = previewEffect_->editMeshSegments_;

            ex->UpdateProceduralMesh();
        }
    }
}


void MeshEffectEditor::SyncTextureIndices() {
    auto findIndex = [&](const std::string& path) -> int {
        if (path.empty()) return -1;
        std::string fileName = std::filesystem::path(path).filename().string();
        for (int i = 0; i < (int)textureFileList_.size(); ++i) {
            if (textureFileList_[i] == fileName) return i;
        }
        return -1;
        };
    currentTextureIndex_ = findIndex(editTexturePath_);
    currentNoiseTextureIndex_ = findIndex(editNoiseTexturePath_);
    currentRampTextureIndex_ = findIndex(editRampTexturePath_);
}