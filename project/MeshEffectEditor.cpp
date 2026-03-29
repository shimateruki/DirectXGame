#include "MeshEffectEditor.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "SceneManager.h" 
#include "BaseScene.h"    
#include <imgui.h>
#include <fstream>
#include "json.hpp" // プロジェクト内のJSONライブラリ
#include <DebugConsole.h>

using json = nlohmann::json;

void MeshEffectEditor::Initialize(SceneManager* sceneManager) {
    // ★ 変更: SceneManager のポインタだけ保持する
    sceneManager_ = sceneManager;
    RefreshTextureList();
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
void MeshEffectEditor::Update(float deltaTime) {
    if (!sceneManager_) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();

    // ========================================================
    //  シーンが切り替わった（再生ボタン等）らプレビューを破棄！
    // ========================================================
    if (lastScene_ != currentScene) {
        previewEffect_.reset(); // 古いポインタを手放す
        lastScene_ = currentScene; // 現在のシーンを記憶
    }
    // ★ 変更: まだプレビューが作られていなければ、シーンからObject3dCommonを取得して生成する
    if (!previewEffect_ && sceneManager_) {
        BaseScene* currentScene = sceneManager_->GetCurrentScene();
        if (currentScene && currentScene->GetObject3dCommon()) {

            previewEffect_ = std::make_unique<EffectObject3d>();
            previewEffect_->Initialize(currentScene->GetObject3dCommon());

            // パラメータの初期設定
            previewEffect_->SetModel(editModelName_);
            if (auto renderer = previewEffect_->GetMeshRenderer()) {
                if (strlen(editTexturePath_) > 0) {
                    renderer->SetTexture(editTexturePath_);
                }
            }
            previewEffect_->SetColor(editColor_);
            previewEffect_->SetScrollSpeed(editScrollSpeed_);
            previewEffect_->SetIntensity(editIntensity_);
        }
    }
    if (previewEffect_) {
        // ★ 毎フレーム新しいパラメータを送る
        previewEffect_->SetStartScale(editStartScale_);
        previewEffect_->SetEndScale(editEndScale_);
        previewEffect_->SetStartColor(editStartColor_);
        previewEffect_->SetEndColor(editEndColor_);

        previewEffect_->SetScrollSpeed(editScrollSpeed_);
        previewEffect_->SetIntensity(editIntensity_);

        // 固定の Transform は Position と Rotation だけ適用
        previewEffect_->SetTranslate(editPosition_);
        previewEffect_->SetRotation(editRotation_);
        previewEffect_->SetDistortionStrength(editDistortionStrength_);
        previewEffect_->SetDistortionSpeed(editDistortionSpeed_);
        previewEffect_->SetEdgeFadeStrength(editEdgeFadeStrength_);
        previewEffect_->SetEnableDistortion(editEnableDistortion_);

        // ========================================================
        // ★ ここを追加！：カラーランプを使うかどうかのフラグを毎フレーム送る！
        // ========================================================
        // editRampTexturePath_ に文字列が入っていれば(テクスチャが設定されていれば) true になる
        bool useRamp = (strlen(editRampTexturePath_) > 0);
        previewEffect_->SetEnableColorRamp(useRamp);
        bool useNoise = (strlen(editNoiseTexturePath_) > 0);
        previewEffect_->SetEnableNoiseTexture(useNoise);
        // ========================================================

        // ★ ここを追加！：テクスチャの紐付けを毎フレーム確実に行う！
        if (strlen(editNoiseTexturePath_) > 0) {
            uint32_t handle = TextureManager::GetInstance()->Load(editNoiseTexturePath_);
            previewEffect_->SetNoiseTexture(handle);
        }
        if (strlen(editRampTexturePath_) > 0) {
            uint32_t handle = TextureManager::GetInstance()->Load(editRampTexturePath_);
            previewEffect_->SetRampTexture(handle);
        }

        // ★ オートループ機能
        if (isAutoLoop_ && !previewEffect_->IsPlaying()) {
            previewEffect_->Play(editLifetime_);
        }

        float timeStep = deltaTime;
        if (timeStep <= 0.0001f) timeStep = 1.0f / 60.0f;

        previewEffect_->Update(timeStep);
        previewEffect_->UpdateLocalMatrix();
        previewEffect_->UpdateWorldMatrix();
    }
}

void MeshEffectEditor::Draw() {
    // ★ 1. Game側から呼ばれているか確認
    DebugConsole::GetInstance()->AddLog("1: MeshEffectEditor::Draw() is Called!");
    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (lastScene_ != currentScene || !currentScene) {
        // シーンが破棄・切り替えられた直後のフレームなので、
        // 古いメモリ（ダングリングポインタ）へのアクセスを防ぐため描画をスキップ！
        return;
    }
    if (previewEffect_) {
        previewEffect_->Draw();
    }
    else {
        // ★ もし生成されていなければエラーを出す
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "Error: previewEffect_ is NULL!");
    }
}

void MeshEffectEditor::DrawImGui() {
#ifdef USE_IMGUI
    if (!previewEffect_) return;

    // ウィンドウの横幅を取得（ボタンなどを画面幅ぴったりに合わせるため）
    float availWidth = ImGui::GetContentRegionAvail().x;

    // ==========================================
    // 1. プレビュー操作（最頻出なので一番上に固定）
    // ==========================================
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("PLAY EFFECT (ATTACK!)", ImVec2(availWidth, 40))) {
        previewEffect_->Play(editLifetime_);
    }
    ImGui::PopStyleColor();

    ImGui::Checkbox("Auto Loop", &isAutoLoop_);
    ImGui::SameLine();
    if (ImGui::Button("Reset Time")) {
        previewEffect_->ResetTime();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ==========================================
    // 2. リソース設定
    // ==========================================
    if (ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen)) {
        // --- メッシュ選択 ---
        std::vector<std::string> modelNames = ModelManager::GetInstance()->GetLoadedModelNames();
        if (ImGui::BeginCombo("Select Mesh", editModelName_)) {
            for (const auto& name : modelNames) {
                bool isSelected = (name == editModelName_);
                if (ImGui::Selectable(name.c_str(), isSelected)) {
                    strncpy_s(editModelName_, name.c_str(), sizeof(editModelName_) - 1);
                    previewEffect_->SetModel(editModelName_);
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // --- ① テクスチャ選択 ---
        if (!textureFileList_.empty()) {
            const char* currentTexName = (currentTextureIndex_ >= 0) ? textureFileList_[currentTextureIndex_].c_str() : "None";
            if (ImGui::BeginCombo("Texture", currentTexName)) {
                for (int i = 0; i < textureFileList_.size(); ++i) {
                    bool isSelected = (currentTextureIndex_ == i);
                    if (ImGui::Selectable(textureFileList_[i].c_str(), isSelected)) {
                        currentTextureIndex_ = i;
                        std::string fullPath = "Resources/sprite/" + textureFileList_[i];
                        strncpy_s(editTexturePath_, fullPath.c_str(), sizeof(editTexturePath_) - 1);
                        if (auto renderer = previewEffect_->GetMeshRenderer()) {
                            renderer->SetTexture(editTexturePath_);
                        }
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No texture found in Resources/sprite/");
        }

        // --- ② ノイズテクスチャ選択 ---
        const char* currentNoiseTexName = (currentNoiseTextureIndex_ >= 0) ? textureFileList_[currentNoiseTextureIndex_].c_str() : "None";
        if (ImGui::BeginCombo("Noise Texture", currentNoiseTexName)) {
            for (int i = 0; i < textureFileList_.size(); ++i) {
                bool isSelected = (currentNoiseTextureIndex_ == i);
                if (ImGui::Selectable(textureFileList_[i].c_str(), isSelected)) {
                    currentNoiseTextureIndex_ = i;
                    std::string fullPath = "Resources/sprite/" + textureFileList_[i];
                    strncpy_s(editNoiseTexturePath_, fullPath.c_str(), sizeof(editNoiseTexturePath_) - 1);

                    // 選択されたら即座にプレビューに反映
                    uint32_t texHandle = TextureManager::GetInstance()->Load(editNoiseTexturePath_);
                    previewEffect_->SetNoiseTexture(texHandle);
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

       auto TextureCombo = [&](const char* label, int& currentIndex, char* targetPath, auto callback) {
        const char* previewName = (currentIndex >= 0 && currentIndex < (int)textureFileList_.size())
            ? textureFileList_[currentIndex].c_str() : "None";

        if (ImGui::BeginCombo(label, previewName)) {
            // =======================================================
            // ★追加：テクスチャを「なし (None)」に戻す機能
            if (ImGui::Selectable("None", currentIndex == -1)) {
                currentIndex = -1;
                targetPath[0] = '\0'; // パスを空にする
                callback("");         // 空文字を渡す
            }
            // =======================================================

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
    }

    // ==========================================
    // 3. 配置（トランスフォーム）
    // ==========================================
    if (ImGui::CollapsingHeader("Base Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &editPosition_.x, 0.1f);
        ImGui::DragFloat3("Rotation", &editRotation_.x, 0.01f);
        // ※固定Scaleはアニメーションで上書きされるため廃止しました
    }

    // ==========================================
    // 4. アニメーション（Scale & Color）
    // ==========================================
    if (ImGui::CollapsingHeader("Animation (Start -> End)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Lifetime (sec)", &editLifetime_, 0.05f, 3.0f);

        ImGui::Spacing();
        ImGui::Text("[ Scale ]");
        ImGui::DragFloat3("Start Scale", &editStartScale_.x, 0.1f);
        ImGui::DragFloat3("End Scale", &editEndScale_.x, 0.1f);

        ImGui::Spacing();
        ImGui::Text("[ Color ]");
        ImGui::ColorEdit4("Start Color", &editStartColor_.x);
        ImGui::ColorEdit4("End Color", &editEndColor_.x);
    }

    // ==========================================
    // 5. シェーダーパラメータ
    // ==========================================
    if (ImGui::CollapsingHeader("Shader Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat2("Scroll Speed", &editScrollSpeed_.x, -10.0f, 10.0f);
        ImGui::SliderFloat("Intensity (HDR)", &editIntensity_, 0.0f, 10.0f);
    }
    ImGui::Text("[ Distortion (波打ち) ]");
    ImGui::Checkbox("Enable Grab Distortion (背景歪み)", &editEnableDistortion_);
    ImGui::SliderFloat("Dist Strength", &editDistortionStrength_, 0.0f, 0.2f); // 0.05くらいが自然です
    ImGui::SliderFloat("Dist Speed", &editDistortionSpeed_, 0.0f, 50.0f);     // 15.0くらいが標準

    ImGui::Spacing();
    ImGui::Text("[ Edge Fade (外側の透明化) ]");

    // 1.0f で削りなし、10.0f でほぼ消滅
    ImGui::SliderFloat("Fade Strength", &editEdgeFadeStrength_, 1.0f, 10.0f);
    // ==========================================
    // 6. 保存と読み込み
    // ==========================================
    if (ImGui::CollapsingHeader("Save & Load")) {
        ImGui::InputText("File Name", saveFileName_, sizeof(saveFileName_));

        // 2つのボタンを画面幅の半分ずつで横並びに配置
        float halfWidth = (availWidth - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
        if (ImGui::Button("Save JSON", ImVec2(halfWidth, 0))) {
            SaveToJson();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load JSON", ImVec2(halfWidth, 0))) {
            LoadFromJson();
        }
    }
#endif
}

// ==========================================
// JSON 保存処理
// ==========================================
void MeshEffectEditor::SaveToJson() {
    json j;

    // --- リソース ---
    j["ModelName"] = editModelName_;
    j["TexturePath"] = editTexturePath_;
    j["NoiseTexturePath"] = editNoiseTexturePath_;
    j["RampTexturePath"] = editRampTexturePath_; // ★ 追加

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
    std::ofstream file(saveFileName_);
    if (file.is_open()) {
        file << j.dump(4); // 4インデントで綺麗に出力
        file.close();
    }
}

// ==========================================
// JSON 読み込み処理
// ==========================================
void MeshEffectEditor::LoadFromJson() {
    std::ifstream file(saveFileName_);
    if (!file.is_open()) {
        // ファイルが無ければ何もしない
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

    // ★ 追加: カラーランプの読み込み
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
    if (j.contains("Lifetime")) {
        editLifetime_ = j["Lifetime"];
    }
    if (j.contains("AutoLoop")) {
        isAutoLoop_ = j["AutoLoop"];
    }

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

    // ★読み込み完了後、新しい設定値でエフェクトを最初から再生し直す
    previewEffect_->Play(editLifetime_);
    SyncTextureIndices();
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