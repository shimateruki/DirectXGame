#include "GPUParticleEditor.h"
#include <imgui.h>
#include <fstream>
#include <json.hpp> // nlohmann/json
#include "DebugConsole.h"

using json = nlohmann::json;

void GPUParticleEditor::Initialize() {
    emitTimer_ = 0.0f;
}

void GPUParticleEditor::Update(float deltaTime) {

    if (config_.isLooping) {
        emitTimer_ += deltaTime;
        if (emitTimer_ >= config_.emitInterval) {
            // これ1行で、設定データ(config_)の内容が全てManagerに伝わり発生します！
            GPUParticleManager::GetInstance()->EmitFromConfig(config_);
            emitTimer_ = 0.0f;
        }
    }
}

void GPUParticleEditor::DrawImGui() {
    ImGui::Text("--- GPUパーティクルエディタ ---");

    if (ImGui::CollapsingHeader("発生パラメータ (Emit Parameters)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* blendModes[] = { "加算 (光・魔法)", "半透明 (霧・煙)", "歪み (衝撃波・陽炎)" };
        ImGui::Combo("合成モード (Blend Mode)", &config_.blendModeIndex, blendModes, IM_ARRAYSIZE(blendModes));
        ImGui::Separator();
        const char* shapeTypes[] = { "ボックス (Box)", "スフィア (Sphere)", "コーン (Cone)" };
        ImGui::Combo("発生形状 (Shape Type)", &config_.shapeType, shapeTypes, IM_ARRAYSIZE(shapeTypes));

        ImGui::DragFloat3("発生位置 (Position)", &config_.emitPos.x, 0.1f);
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

        // 色の設定のあたりに追加
        ImGui::Combo("色の変化カーブ", &config_.colorEaseType, easeTypes, IM_ARRAYSIZE(easeTypes));
        ImGui::Separator();

        // サイズの設定のあたりに追加
        ImGui::Combo("サイズの変形カーブ", &config_.sizeEaseType, easeTypes, IM_ARRAYSIZE(easeTypes));
        // 形に合わせて出すUIを変える！
        if (config_.shapeType == 0) {
            ImGui::DragFloat3("発生範囲 (Area)", &config_.emitArea.x, 0.1f);
        } else if (config_.shapeType == 1) {
            ImGui::DragFloat("半径 (Radius)", &config_.shapeRadius, 0.1f, 0.0f, 100.0f);
        } else if (config_.shapeType == 2) {
            ImGui::DragFloat("半径 (Radius)", &config_.shapeRadius, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("広がり角度 (Angle)", &config_.shapeAngle, 1.0f, 0.0f, 90.0f);
        }
        ImGui::Separator();
        ImGui::DragFloat3("初期速度 (Velocity)", &config_.emitVelocity.x, 0.1f);
        ImGui::DragInt("発生数 (Count)", &config_.emitCount, 10, 1, GPUParticleManager::kMaxParticles);
        ImGui::DragFloat("寿命 (Life Time)", &config_.emitLife, 0.05f, 0.1f, 10.0f);
        ImGui::DragFloat("速度のばらつき (Variance)", &config_.velocityVariance, 0.1f, 0.0f, 50.0f);
        ImGui::DragFloat("回転スピード (Rot Speed)", &config_.rotSpeed, 0.05f, 0.0f, 20.0f);
        ImGui::ColorEdit4("発生時の色 (Base Color)", &config_.baseColor.x);
        ImGui::ColorEdit4("中間の色 (Mid Color)", &config_.midColor.x);
        ImGui::DragFloat("色がMidになる時間(割合)", &config_.colorMidTime, 0.01f, 0.01f, 0.99f);
        ImGui::ColorEdit4("消滅時の色 (End Color)", &config_.endColor.x);


        ImGui::Separator();

        // 既存のサイズ設定も3段階に書き換える
        ImGui::DragFloat("中間の大きさ (Mid Size)", &config_.midSize, 0.1f, 0.0f, 50.0f);
        ImGui::DragFloat("サイズがMidになる時間(割合)", &config_.sizeMidTime, 0.01f, 0.01f, 0.99f);
        ImGui::DragFloat("発生時の大きさ (Base Size)", &config_.baseSize, 0.1f, 0.1f, 50.0f);
        ImGui::DragFloat("消滅時の大きさ (End Size)", &config_.endSize, 0.1f, 0.1f, 50.0f);
    }

    if (ImGui::CollapsingHeader("環境変化 (Environment)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("重力 (Gravity)", &config_.envGravity.x, 0.01f);
        ImGui::DragFloat("空気抵抗 (Air Drag)", &config_.envDrag, 0.001f, 0.8f, 1.0f);
        ImGui::DragFloat3("風 (Wind)", &config_.envWind.x, 0.1f);
        ImGui::DragFloat("乱流・ノイズ (Turbulence)", &config_.envTurbulence, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("地形との馴染み (Soft Fade)", &config_.softParticleFade, 0.1f, 0.1f, 20.0f);
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("エディタ操作 (Editor Controls)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("1回発生 (Emit Once)", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
            // ★ こちらも1行で出せます！
            GPUParticleManager::GetInstance()->EmitFromConfig(config_);
        }

        ImGui::Checkbox("連続発生テスト (Loop Emit)", &config_.isLooping);
        if (config_.isLooping) {
            ImGui::Indent();
            ImGui::DragFloat("発生間隔 (Interval)", &config_.emitInterval, 0.01f, 0.01f, 2.0f);
            ImGui::Unindent();
        }
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("保存と読み込み (Save & Load)", ImGuiTreeNodeFlags_DefaultOpen)) {
        // ★ .h に合わせて変数名を presetNameInput_ に変更
        ImGui::InputText("プリセット名", presetNameInput_, sizeof(presetNameInput_));

        if (ImGui::Button("保存 (Save)", ImVec2(120, 0))) {
            Save(presetNameInput_);
        }
        ImGui::SameLine();
        if (ImGui::Button("読み込み (Load)", ImVec2(120, 0))) {
            Load(presetNameInput_);
        }
    }
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
        // ★ 追加したパラメータの読み込み
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
        if (DebugConsole::GetInstance()) {
            DebugConsole::GetInstance()->AddLog("Loaded GPU Particle Preset: " + presetName);
        }
    }
}