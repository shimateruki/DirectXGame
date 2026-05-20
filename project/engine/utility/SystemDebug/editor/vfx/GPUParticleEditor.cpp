#include "GPUParticleEditor.h"
#include <imgui.h>
#include <fstream>
#include <json.hpp> // nlohmann/json
#include "DebugConsole.h"
#include <TextureManager.h>
#include "IconsFontAwesome5.h"
#include "GPUParticleSystem.h"
#include "CameraManager.h"

using json = nlohmann::json;

void GPUParticleEditor::Initialize() {
    emitTimer_ = 0.0f;
}

void GPUParticleEditor::Update(float deltaTime) {
    // エディタのプレビュー間隔もスローモーションに対応させる
    float scaledDelta = deltaTime * GPUParticleManager::GetInstance()->GetTimeScale();

    if (config_.isLooping) {
        emitTimer_ += scaledDelta;
        if (emitTimer_ >= config_.emitInterval) {
            EmitWithPreview();
            emitTimer_ = 0.0f;
        }
    }
}
void GPUParticleEditor::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text("--- GPUパーティクルエディタ ---");

    // ⚙️ システム設定
    if (ImGui::CollapsingHeader(ICON_FA_COGS " システム設定 (System)", ImGuiTreeNodeFlags_DefaultOpen)) {
        float timeScale = GPUParticleManager::GetInstance()->GetTimeScale();
        if (ImGui::DragFloat(ICON_FA_CLOCK " 全体再生速度 (Time Scale)", &timeScale, 0.05f, 0.0f, 5.0f)) {
            GPUParticleManager::GetInstance()->SetTimeScale(timeScale);
        }
        ImGui::Text("※0.5でスローモーション、2.0で倍速になります");
    }

    if (ImGui::CollapsingHeader(ICON_FA_EYE " プレビュー環境 (Preview Environment)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("カメラの前に発生させる (Spawn in front of Camera)", &isPreviewMode_);
        if (isPreviewMode_) {
            ImGui::Indent();
            ImGui::DragFloat("カメラからの距離 (Distance)", &previewDistance_, 0.1f, 1.0f, 50.0f);
            ImGui::Unindent();
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "※プレビュー用です。実際の座標は「発生位置(Position)」に保存されます");
    }

    // ✨ 発生パラメータ
    if (ImGui::CollapsingHeader(ICON_FA_MAGIC " 発生パラメータ (Emit Parameters)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* blendModes[] = { "加算 (光・魔法)", "半透明 (霧・煙)", "歪み (衝撃波・陽炎)" };
        ImGui::Combo(ICON_FA_ADJUST " 合成モード (Blend Mode)", &config_.blendModeIndex, blendModes, IM_ARRAYSIZE(blendModes));
        ImGui::Separator();

        const char* shapeTypes[] = { "ボックス (Box)", "スフィア (Sphere)", "コーン (Cone)", "メッシュ (Mesh)", "ハート (Heart)" };
        ImGui::Combo(ICON_FA_SHAPES " 発生形状 (Shape Type)", &config_.shapeType, shapeTypes, IM_ARRAYSIZE(shapeTypes));

        if (ImGui::CollapsingHeader(ICON_FA_COMPRESS_ALT " コリジョン (Collision)", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool isCollide = config_.enableCollision != 0;
            if (ImGui::Checkbox("地形と衝突させる", &isCollide)) {
                config_.enableCollision = isCollide ? 1 : 0;
            }
            if (isCollide) {
                ImGui::DragFloat("跳ね返り係数 (Restitution)", &config_.restitution, 0.05f, 0.0f, 1.0f);
            }
        }

        ImGui::DragFloat3(ICON_FA_CROSSHAIRS " 発生位置 (Position)", &config_.emitPos.x, 0.1f);

        const char* easeTypes[] = {
                 "0: Linear (一定)",
                 "1: InSine", "2: OutSine", "3: InOutSine",
                 "4: InQuad", "5: OutQuad", "6: InOutQuad",
                 "7: InCubic", "8: OutCubic", "9: InOutCubic",
                 "10: InQuart", "11: OutQuart", "12: InOutQuart",
                 "13: InQuint", "14: OutQuint", "15: InOutQuint",
                 "16: InExpo", "17: OutExpo", "18: InOutExpo",
                 "19: InCirc", "20: OutCirc", "21: InOutCirc",
                 "22: InBack", "23: OutBack", "24: InOutBack",
                 "25: InElastic", "26: OutElastic", "27: InOutElastic",
                 "28: InBounce", "29: OutBounce", "30: InOutBounce"
        };

        ImGui::Combo(ICON_FA_PALETTE " 色の変化カーブ", &config_.colorEaseType, easeTypes, IM_ARRAYSIZE(easeTypes));
        ImGui::Separator();
        ImGui::Combo(ICON_FA_EXPAND_ARROWS_ALT " サイズの変形カーブ", &config_.sizeEaseType, easeTypes, IM_ARRAYSIZE(easeTypes));

        // 形に合わせて出すUIを変える！
        if (config_.shapeType == 0) {
            ImGui::DragFloat3("発生範囲 (Area)", &config_.emitArea.x, 0.1f);
        }
        else if (config_.shapeType == 1) {
            ImGui::DragFloat("半径 (Radius)", &config_.shapeRadius, 0.1f, 0.0f, 100.0f);
        }
        else if (config_.shapeType == 2) {
            ImGui::DragFloat("半径 (Radius)", &config_.shapeRadius, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("広がり角度 (Angle)", &config_.shapeAngle, 1.0f, 0.0f, 90.0f);
        }
        else if (config_.shapeType == 4) {

            ImGui::DragFloat("ハートの大きさ (Radius)", &config_.shapeRadius, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("厚み (Thickness)", &config_.emitArea.x, 0.1f, 0.0f, 50.0f);
            ImGui::DragFloat("輪郭の太さ (Line Thickness)", &config_.emitArea.y, 0.01f, 0.0f, 1.0f);
        }
        ImGui::Separator();

        ImGui::DragFloat3(ICON_FA_TACHOMETER_ALT " 初期速度 (Velocity)", &config_.emitVelocity.x, 0.1f);
        ImGui::DragInt(ICON_FA_SORT_NUMERIC_UP " 発生数 (Count)", &config_.emitCount, 10, 1, GPUParticleSystem::kMaxParticles);
        ImGui::DragFloat(ICON_FA_HOURGLASS_HALF " 寿命 (Life Time)", &config_.emitLife, 0.05f, 0.1f, 10.0f);
        ImGui::DragFloat(ICON_FA_RANDOM " 速度のばらつき (Variance)", &config_.velocityVariance, 0.1f, 0.0f, 50.0f);
        ImGui::DragFloat(ICON_FA_SYNC " 回転スピード (Rot Speed)", &config_.rotSpeed, 0.05f, 0.0f, 20.0f);

        ImGui::ColorEdit4("発生時の色 (Base Color)", &config_.baseColor.x);
        ImGui::ColorEdit4("中間の色 (Mid Color)", &config_.midColor.x);
        ImGui::DragFloat("色がMidになる時間(割合)", &config_.colorMidTime, 0.01f, 0.01f, 0.99f);
        ImGui::ColorEdit4("消滅時の色 (End Color)", &config_.endColor.x);
        ImGui::DragFloat(ICON_FA_SUN " 発光強度 (HDR Intensity)", &config_.colorIntensity, 0.1f, 0.0f, 20.0f);

        std::vector<std::string> allPaths = TextureManager::GetInstance()->GetLoadedTexturePaths();
        static std::vector<std::string> filteredPaths;
        static std::vector<std::string> fileNames;
        filteredPaths.clear();
        fileNames.clear();

        for (const auto& path : allPaths) {
            // spriteフォルダの中の particle フォルダのみを参照するように絞り込み
            if (path.find("Resources/sprite/particle/") != std::string::npos) {
                filteredPaths.push_back(path);
                fileNames.push_back(std::filesystem::path(path).filename().string());
            }
        }

        std::vector<const char*> texNames;
        int currentIndex = 0;
        for (int i = 0; i < filteredPaths.size(); ++i) {
            texNames.push_back(fileNames[i].c_str());
            if (config_.texturePath == filteredPaths[i]) {
                currentIndex = i;
            }
        }

        if (!texNames.empty()) {
            if (ImGui::Combo(ICON_FA_IMAGE " テクスチャ画像", &currentIndex, texNames.data(), static_cast<int>(texNames.size()))) {
                config_.texturePath = filteredPaths[currentIndex];
                // ここで無理に SetTexture を呼ばなくても、EmitFromConfig 時に
                // 新しいテクスチャ用のシステムが自動で選択・更新されるようになります。
            }
        }

        ImGui::DragFloat("中間の大きさ (Mid Size)", &config_.midSize, 0.1f, 0.0f, 50.0f);
        ImGui::DragFloat("サイズがMidになる時間(割合)", &config_.sizeMidTime, 0.01f, 0.01f, 0.99f);
        ImGui::DragFloat("発生時の大きさ (Base Size)", &config_.baseSize, 0.1f, 0.1f, 50.0f);
        ImGui::DragFloat("消滅時の大きさ (End Size)", &config_.endSize, 0.1f, 0.1f, 50.0f);
    }

    // 🌍 環境変化
    if (ImGui::CollapsingHeader(ICON_FA_GLOBE " 環境変化 (Environment)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("重力 (Gravity)", &config_.envGravity.x, 0.01f);
        ImGui::DragFloat("空気抵抗 (Air Drag)", &config_.envDrag, 0.001f, 0.8f, 1.0f);
        ImGui::DragFloat3(ICON_FA_WIND " 風 (Wind)", &config_.envWind.x, 0.1f);
        ImGui::DragFloat("乱流・ノイズ (Turbulence)", &config_.envTurbulence, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat(ICON_FA_FEATHER " 地形との馴染み (Soft Fade)", &config_.softParticleFade, 0.1f, 0.1f, 20.0f);
    }
    ImGui::Separator();

    // 🎮 エディタ操作
    if (ImGui::CollapsingHeader(ICON_FA_GAMEPAD " エディタ操作 (Editor Controls)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button(ICON_FA_PLAY " 1回発生 (Emit Once)", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
            EmitWithPreview();
        }

        ImGui::Checkbox(ICON_FA_REDO " 連続発生テスト (Loop Emit)", &config_.isLooping);
        if (config_.isLooping) {
            ImGui::Indent();
            ImGui::DragFloat("発生間隔 (Interval)", &config_.emitInterval, 0.01f, 0.01f, 2.0f);
            ImGui::Unindent();
        }
    }

    // 🚀 クイック・プリセット (Quick Presets)
    if (ImGui::CollapsingHeader(ICON_FA_BOLT " クイック・プリセット (Quick Presets)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("ボタン一つで標準的な設定を流し込みます。");
        
        if (ImGui::Button("⭐ スター (Star)", ImVec2(120, 30))) {
            config_.blendModeIndex = 0; // Additive
            config_.shapeType = 1;      // Sphere
            config_.shapeRadius = 2.0f;
            config_.emitCount = 300;
            config_.emitLife = 1.2f;
            config_.baseColor = { 1.0f, 0.9f, 0.2f, 1.0f }; // Yellowish
            config_.midColor = { 1.0f, 0.5f, 0.1f, 1.0f };  // Orange
            config_.endColor = { 1.0f, 0.0f, 0.0f, 0.0f };  // Red fade
            config_.colorMidTime = 0.5f;
            config_.colorIntensity = 5.0f;
            config_.baseSize = 0.5f;
            config_.midSize = 0.8f;
            config_.endSize = 0.0f;
            config_.sizeMidTime = 0.5f;
            config_.emitVelocity = { 0.0f, 2.0f, 0.0f };
            config_.velocityVariance = 3.0f;
            config_.envGravity = { 0.0f, -0.5f, 0.0f };
            config_.texturePath = "Resources/sprite/particle/particle.png";
            config_.rotSpeed = 5.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("✨ コイン待機 (Idle)", ImVec2(150, 30))) {
            config_.blendModeIndex = 0; // Additive
            config_.shapeType = 1;      // Sphere
            config_.shapeRadius = 1.5f;
            config_.emitCount = 15;     // 少なめで上品に
            config_.emitLife = 2.5f;
            config_.baseColor = { 1.0f, 1.0f, 0.5f, 1.0f }; // Pale Yellow
            config_.midColor = { 1.0f, 0.8f, 0.2f, 1.0f };  // Gold
            config_.endColor = { 1.0f, 0.5f, 0.0f, 0.0f };
            config_.colorMidTime = 0.5f;
            config_.colorIntensity = 3.0f;
            config_.baseSize = 0.1f;    // 小さめから
            config_.midSize = 0.4f;     // ふわっと大きくなる
            config_.endSize = 0.0f;
            config_.sizeMidTime = 0.5f;
            config_.emitVelocity = { 0.0f, 0.2f, 0.0f }; // ゆっくり上に
            config_.velocityVariance = 0.5f;
            config_.envGravity = { 0.0f, 0.3f, 0.0f }; // フワッと浮上
            config_.texturePath = "Resources/sprite/particle/particle.png";
            config_.rotSpeed = 2.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("🌟 コイン取得 (Burst)", ImVec2(150, 30))) {
            config_.blendModeIndex = 0; // Additive
            config_.shapeType = 1;      // Sphere
            config_.shapeRadius = 0.5f; // 中心から
            config_.emitCount = 150;    // 一気にたくさん
            config_.emitLife = 0.8f;    // 短命で派手に
            config_.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // Flash White
            config_.midColor = { 1.0f, 0.9f, 0.0f, 1.0f };  // Bright Gold
            config_.endColor = { 1.0f, 0.5f, 0.0f, 0.0f };
            config_.colorMidTime = 0.3f;
            config_.colorIntensity = 10.0f; // まぶしく
            config_.baseSize = 0.6f;
            config_.midSize = 0.3f;
            config_.endSize = 0.0f;
            config_.sizeMidTime = 0.3f;
            config_.emitVelocity = { 0.0f, 0.0f, 0.0f }; 
            config_.velocityVariance = 12.0f; // 四方八方に高速で弾ける
            config_.envGravity = { 0.0f, -2.0f, 0.0f }; // 重力で落ちる
            config_.envDrag = 0.9f;     // 失速して消える
            config_.texturePath = "Resources/sprite/particle/particle.png";
            config_.rotSpeed = 10.0f;
        }

        // --- 2段目 ---
        if (ImGui::Button("💫 オーラ (Aura)", ImVec2(120, 30))) {
            config_.blendModeIndex = 0; // Additive
            config_.shapeType = 1;      // Sphere
            config_.shapeRadius = 2.0f; // 広め
            config_.emitCount = 60;
            config_.emitLife = 1.5f;
            config_.baseColor = { 0.3f, 0.7f, 1.0f, 0.0f }; // 水色
            config_.midColor = { 0.1f, 0.4f, 1.0f, 1.0f };
            config_.endColor = { 0.0f, 0.0f, 1.0f, 0.0f };
            config_.colorMidTime = 0.5f;
            config_.colorIntensity = 2.0f;
            config_.baseSize = 2.0f;    // 大きくぼんやり
            config_.midSize = 0.8f;
            config_.endSize = 0.0f;
            config_.sizeMidTime = 0.5f;
            config_.emitVelocity = { 0.0f, 0.0f, 0.0f }; 
            config_.velocityVariance = 0.3f; // ほとんど動かない
            config_.envGravity = { 0.0f, 0.0f, 0.0f };
            config_.texturePath = "Resources/sprite/particle/white.png";
            config_.rotSpeed = 1.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("🔥 焚き火 (Fire)", ImVec2(120, 30))) {
            config_.blendModeIndex = 0; // Additive
            config_.shapeType = 1;      // Sphere
            config_.shapeRadius = 1.0f;
            config_.emitCount = 500;
            config_.emitLife = 1.0f;
            config_.baseColor = { 1.0f, 0.3f, 0.1f, 1.0f };
            config_.midColor = { 1.0f, 0.1f, 0.0f, 1.0f };
            config_.endColor = { 0.2f, 0.0f, 0.0f, 0.0f };
            config_.colorMidTime = 0.4f;
            config_.colorIntensity = 8.0f;
            config_.baseSize = 0.8f;
            config_.midSize = 1.2f;
            config_.endSize = 0.2f;
            config_.sizeMidTime = 0.6f;
            config_.emitVelocity = { 0.0f, 5.0f, 0.0f };
            config_.velocityVariance = 1.5f;
            config_.envGravity = { 0.0f, 2.0f, 0.0f };
            config_.texturePath = "Resources/sprite/particle/particle.png";
        }
        ImGui::SameLine();
        if (ImGui::Button("💨 煙 (Smoke)", ImVec2(120, 30))) {
            config_.blendModeIndex = 1; // Alpha
            config_.shapeType = 0;      // Box
            config_.emitArea = { 2.0f, 0.1f, 2.0f };
            config_.emitCount = 100;
            config_.emitLife = 3.0f;
            config_.baseColor = { 0.3f, 0.3f, 0.3f, 0.5f };
            config_.midColor = { 0.5f, 0.5f, 0.5f, 0.3f };
            config_.endColor = { 0.8f, 0.8f, 0.8f, 0.0f };
            config_.colorMidTime = 0.5f;
            config_.colorIntensity = 1.0f;
            config_.baseSize = 1.0f;
            config_.midSize = 3.0f;
            config_.endSize = 5.0f;
            config_.sizeMidTime = 0.5f;
            config_.emitVelocity = { 0.0f, 1.0f, 0.0f };
            config_.velocityVariance = 0.5f;
            config_.envWind = { 1.0f, 0.0f, 0.0f };
            config_.texturePath = "Resources/sprite/particle/white.png";
        }
    }

    ImGui::Separator();

    // 💾 保存と読み込み
    if (ImGui::CollapsingHeader(ICON_FA_SAVE " 保存と読み込み (Save & Load)", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string dirPath = "Resources/json/gpu_particles/";
        std::vector<std::string> presetList;
        if (std::filesystem::exists(dirPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
                if (entry.path().extension() == ".json") {
                    presetList.push_back(entry.path().stem().string());
                }
            }
        }

        static int selectedIndex = 0;
        std::vector<const char*> presetNamesCStr;
        for (int i = 0; i < presetList.size(); ++i) {
            presetNamesCStr.push_back(presetList[i].c_str());
            if (presetList[i] == presetNameInput_) {
                selectedIndex = i;
            }
        }

        if (!presetNamesCStr.empty()) {
            if (ImGui::Combo(ICON_FA_FOLDER_OPEN " 既存ファイル一覧", &selectedIndex, presetNamesCStr.data(), static_cast<int>(presetNamesCStr.size()))) {
                strcpy_s(presetNameInput_, sizeof(presetNameInput_), presetList[selectedIndex].c_str());
            }
        }
        else {
            ImGui::Text("※保存されたプリセットがありません");
        }

        ImGui::Separator();
        ImGui::Text(ICON_FA_IMAGES " プリセットギャラリー (Click to Preview)");
        ImGui::BeginChild("Gallery", ImVec2(0, 150), true);
        for (int i = 0; i < presetList.size(); ++i) {
            if (ImGui::Button(presetList[i].c_str(), ImVec2(140, 30))) {
                Load(presetList[i]);
                strcpy_s(presetNameInput_, sizeof(presetNameInput_), presetList[i].c_str());
                EmitWithPreview();
            }
            if ((i % 3) != 2) ImGui::SameLine();
        }
        ImGui::EndChild();

        ImGui::Separator();

        ImGui::InputText(ICON_FA_FILE_SIGNATURE " プリセット名", presetNameInput_, sizeof(presetNameInput_));

        if (ImGui::Button(ICON_FA_DOWNLOAD " 保存 (Save)", ImVec2(120, 0))) {
            Save(presetNameInput_);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UPLOAD " 読み込み (Load)", ImVec2(120, 0))) {
            Load(presetNameInput_);
        }
    }
#endif
}

void GPUParticleEditor::EmitWithPreview() {
    GPUParticleConfig tempConfig = config_;
    if (isPreviewMode_) {
        const Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
        if (camera) {
            Vector3 camEye = camera->GetEye();
            Vector3 camTarget = camera->GetTargetPoint();
            Vector3 dir = { camTarget.x - camEye.x, camTarget.y - camEye.y, camTarget.z - camEye.z };
            float length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            Vector3 forward = { 0, 0, 1 };
            if (length > 0.0001f) {
                forward = { dir.x / length, dir.y / length, dir.z / length };
            }
            tempConfig.emitPos = {
                camEye.x + forward.x * previewDistance_,
                camEye.y + forward.y * previewDistance_,
                camEye.z + forward.z * previewDistance_
            };
        }
    }
    GPUParticleManager::GetInstance()->EmitFromConfig(tempConfig);
}

void GPUParticleEditor::Save(const std::string& presetName) {
    json j;
    j["emitPos"] = { config_.emitPos.x, config_.emitPos.y, config_.emitPos.z };
    j["emitArea"] = { config_.emitArea.x, config_.emitArea.y, config_.emitArea.z };
    j["emitVelocity"] = { config_.emitVelocity.x, config_.emitVelocity.y, config_.emitVelocity.z };
    j["emitCount"] = config_.emitCount;
    j["emitLife"] = config_.emitLife;
    j["velocityVariance"] = config_.velocityVariance;
    j["baseColor"] = { config_.baseColor.x, config_.baseColor.y, config_.baseColor.z, config_.baseColor.w };
    j["envGravity"] = { config_.envGravity.x, config_.envGravity.y, config_.envGravity.z };
    j["envDrag"] = config_.envDrag;
    j["envWind"] = { config_.envWind.x, config_.envWind.y, config_.envWind.z };
    j["envTurbulence"] = config_.envTurbulence;
    j["baseSize"] = config_.baseSize;
    j["endSize"] = config_.endSize;
    j["rotSpeed"] = config_.rotSpeed;
    j["endColor"] = { config_.endColor.x, config_.endColor.y, config_.endColor.z, config_.endColor.w };
    j["shapeType"] = config_.shapeType;
    j["shapeRadius"] = config_.shapeRadius;
    j["shapeAngle"] = config_.shapeAngle;
    j["blendModeIndex"] = config_.blendModeIndex;
    j["isLooping"] = config_.isLooping;
    j["emitInterval"] = config_.emitInterval;
    j["midColor"] = { config_.midColor.x, config_.midColor.y, config_.midColor.z, config_.midColor.w };
    j["colorMidTime"] = config_.colorMidTime;
    j["midSize"] = config_.midSize;
    j["sizeMidTime"] = config_.sizeMidTime;
    j["softParticleFade"] = config_.softParticleFade;
    j["sizeEaseType"] = config_.sizeEaseType;
    j["colorEaseType"] = config_.colorEaseType;
    j["enableCollision"] = config_.enableCollision;
    j["restitution"] = config_.restitution;
    j["colorIntensity"] = config_.colorIntensity;
    j["texturePath"] = config_.texturePath;
    std::string filepath = "Resources/json/gpu_particles/" + presetName + ".json";
    std::ofstream file(filepath);
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
        if (DebugConsole::GetInstance()) {
            DebugConsole::GetInstance()->AddLog("Saved GPU Particle Preset: " + presetName);
        }
    }
}

void GPUParticleEditor::Load(const std::string& presetName) {
    std::string filepath = "Resources/json/gpu_particles/" + presetName + ".json";
    std::ifstream file(filepath);
    if (file.is_open()) {
        json j;
        file >> j;
        file.close();

        if (j.contains("emitPos")) { config_.emitPos.x = j["emitPos"][0]; config_.emitPos.y = j["emitPos"][1]; config_.emitPos.z = j["emitPos"][2]; }
        if (j.contains("emitArea")) { config_.emitArea.x = j["emitArea"][0]; config_.emitArea.y = j["emitArea"][1]; config_.emitArea.z = j["emitArea"][2]; }
        if (j.contains("emitVelocity")) { config_.emitVelocity.x = j["emitVelocity"][0]; config_.emitVelocity.y = j["emitVelocity"][1]; config_.emitVelocity.z = j["emitVelocity"][2]; }
        if (j.contains("emitCount")) config_.emitCount = j["emitCount"];
        if (j.contains("emitLife")) config_.emitLife = j["emitLife"];
        if (j.contains("envGravity")) { config_.envGravity.x = j["envGravity"][0]; config_.envGravity.y = j["envGravity"][1]; config_.envGravity.z = j["envGravity"][2]; }
        if (j.contains("envDrag")) config_.envDrag = j["envDrag"];
        if (j.contains("envWind")) { config_.envWind.x = j["envWind"][0]; config_.envWind.y = j["envWind"][1]; config_.envWind.z = j["envWind"][2]; }
        if (j.contains("envTurbulence")) config_.envTurbulence = j["envTurbulence"];
        if (j.contains("velocityVariance")) config_.velocityVariance = j["velocityVariance"];

        if (j.contains("baseColor")) { config_.baseColor.x = j["baseColor"][0]; config_.baseColor.y = j["baseColor"][1]; config_.baseColor.z = j["baseColor"][2]; config_.baseColor.w = j["baseColor"][3]; }
        if (j.contains("baseSize")) config_.baseSize = j["baseSize"];
        if (j.contains("endSize")) config_.endSize = j["endSize"];
        if (j.contains("rotSpeed")) config_.rotSpeed = j["rotSpeed"];
        if (j.contains("endColor")) { config_.endColor.x = j["endColor"][0]; config_.endColor.y = j["endColor"][1]; config_.endColor.z = j["endColor"][2]; config_.endColor.w = j["endColor"][3]; }
        if (j.contains("shapeType")) config_.shapeType = j["shapeType"];
        if (j.contains("shapeRadius")) config_.shapeRadius = j["shapeRadius"];
        if (j.contains("shapeAngle")) config_.shapeAngle = j["shapeAngle"];
        // パラメータの読み込み反映
        if (j.contains("blendModeIndex")) config_.blendModeIndex = j["blendModeIndex"];
        if (j.contains("isLooping")) config_.isLooping = j["isLooping"];
        if (j.contains("emitInterval")) config_.emitInterval = j["emitInterval"];
        if (j.contains("midColor")) { config_.midColor.x = j["midColor"][0]; config_.midColor.y = j["midColor"][1]; config_.midColor.z = j["midColor"][2]; config_.midColor.w = j["midColor"][3]; }
        if (j.contains("colorMidTime")) config_.colorMidTime = j["colorMidTime"];
        if (j.contains("midSize")) config_.midSize = j["midSize"];
        if (j.contains("sizeMidTime")) config_.sizeMidTime = j["sizeMidTime"];
        if (j.contains("softParticleFade")) config_.softParticleFade = j["softParticleFade"];
        if (j.contains("sizeEaseType")) config_.sizeEaseType = j["sizeEaseType"];
        if (j.contains("colorEaseType")) config_.colorEaseType = j["colorEaseType"];
        if (j.contains("enableCollision")) config_.enableCollision = j["enableCollision"];
        if (j.contains("restitution")) config_.restitution = j["restitution"];
        if (j.contains("colorIntensity")) config_.colorIntensity = j["colorIntensity"];
        if (j.contains("texturePath")) {
            config_.texturePath = j["texturePath"];
        }
        if (DebugConsole::GetInstance()) {
            DebugConsole::GetInstance()->AddLog("Loaded GPU Particle Preset: " + presetName);
        }
    }
}