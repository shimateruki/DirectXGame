#define NOMINMAX
#include "TerrainEditorWindow.h"

#include "BaseScene.h"
#include "DebugConsole.h"
#include "DebugEditor.h"
#include "IconsFontAwesome5.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "imgui.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

constexpr std::array<const char*, 3> kPaintPresetLabels = {
    "草地ベース",
    "砂地ベース",
    "岩場ベース",
};

constexpr std::array<const char*, 3> kPaintPresetArgs = {
    "Grass",
    "Sand",
    "Rock",
};

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return L"";
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    return result;
}

std::string QuoteCommandArg(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char c : value) {
        if (c == '"') {
            escaped += "\\\"";
        } else {
            escaped.push_back(c);
        }
    }
    escaped.push_back('"');
    return escaped;
}

std::string FormatFloat(float value) {
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
}

bool RunHiddenProcessAndWait(const std::string& command, DWORD* exitCode) {
    std::wstring wideCommand = Utf8ToWide(command);
    if (wideCommand.empty()) return false;

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION processInfo{};
    std::wstring mutableCommand = wideCommand;
    const BOOL ok = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);
    if (!ok) {
        return false;
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(processInfo.hProcess, &code);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    if (exitCode) {
        *exitCode = code;
    }
    return true;
}

bool ReadJsonFile(const fs::path& path, nlohmann::json& outJson) {
    std::ifstream file(path);
    if (!file) return false;
    try {
        file >> outJson;
    }
    catch (...) {
        return false;
    }
    return outJson.is_object();
}

std::string MakeSafeObjectName(const std::string& baseName) {
    std::string result = baseName;
    for (char& c : result) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    if (result.empty()) {
        result = "Terrain";
    }
    return result;
}

float JsonFloat(const nlohmann::json& json, const char* key, float fallback) {
    if (!json.contains(key) || !json[key].is_number()) return fallback;
    return json[key].get<float>();
}

std::string NormalizePathString(const fs::path& path) {
    return path.generic_string();
}

std::string ResolveTerrainCollisionPathFromReport(const nlohmann::json& report) {
    if (report.contains("terrainCollisionPath") && report["terrainCollisionPath"].is_string()) {
        return report["terrainCollisionPath"].get<std::string>();
    }

    const std::string relativeObjPath = report.value("relativeObjPath", "");
    const std::string name = report.value("name", "");
    if (!relativeObjPath.empty() && !name.empty()) {
        return NormalizePathString(fs::path(relativeObjPath).parent_path() / (name + "_terrain.json"));
    }

    const std::string modelName = report.value("modelName", "");
    if (!modelName.empty() && !name.empty()) {
        return NormalizePathString(fs::path("Resources/3DModel") / modelName / (name + "_terrain.json"));
    }

    return "";
}

uint32_t Hash2D(int x, int z, int seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u;
    h ^= static_cast<uint32_t>(z) * 668265263u;
    h ^= static_cast<uint32_t>(seed) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float Hash01(int x, int z, int seed) {
    return static_cast<float>(Hash2D(x, z, seed) & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
}

float Smooth01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float ValueNoise(float x, float z, int seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = Smooth01(x - static_cast<float>(ix));
    const float fz = Smooth01(z - static_cast<float>(iz));
    const float a = Hash01(ix, iz, seed);
    const float b = Hash01(ix + 1, iz, seed);
    const float c = Hash01(ix, iz + 1, seed);
    const float d = Hash01(ix + 1, iz + 1, seed);
    return Lerp(Lerp(a, b, fx), Lerp(c, d, fx), fz);
}

ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        Lerp(a.x, b.x, t),
        Lerp(a.y, b.y, t),
        Lerp(a.z, b.z, t),
        Lerp(a.w, b.w, t),
    };
}

} // namespace

void TerrainEditorWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
    LoadLatestReport();
}

void TerrainEditorWindow::DrawImGui() {
#ifdef USE_IMGUI
    PollTerrainBuilder();

    ImGui::Text(ICON_FA_MOUNTAIN " 地形生成 / Terrain Builder");
    ImGui::Separator();
    ImGui::TextWrapped("外部ツールで高さ付きOBJ地形を生成し、Editor上でその場プレビューします。ハイトマップ読み込み、簡易ペイントマップ生成、コリジョン高さ調整に対応しています。");
    ImGui::TextDisabled("生成先: Resources/3DModel/GeneratedTerrain/<name>/<name>.obj");

    if (noticeTimer_ > 0.0f) {
        noticeTimer_ = std::max(0.0f, noticeTimer_ - ImGui::GetIO().DeltaTime);
        const ImVec4 color = noticeSuccess_ ? ImVec4(0.45f, 1.0f, 0.55f, 1.0f) : ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        ImGui::TextColored(color, "%s", lastStatus_.c_str());
    } else {
        ImGui::TextWrapped("%s", lastStatus_.c_str());
    }

    ImGui::Spacing();
    ImGui::InputText("地形名", terrainNameBuffer_, sizeof(terrainNameBuffer_));
    ImGui::DragInt("解像度", &resolution_, 1.0f, 2, 256);
    ImGui::DragFloat("横幅 X", &sizeX_, 0.5f, 1.0f, 10000.0f, "%.1f");
    ImGui::DragFloat("奥行 Z", &sizeZ_, 0.5f, 1.0f, 10000.0f, "%.1f");
    ImGui::DragFloat("高さ", &height_, 0.1f, 0.0f, 1000.0f, "%.2f");
    ImGui::DragInt("Seed", &seed_, 1.0f, -999999, 999999);
    ImGui::DragFloat("ノイズの細かさ", &noiseScale_, 0.005f, 0.001f, 10.0f, "%.3f");
    ImGui::DragInt("なめらかさ", &smoothSteps_, 1.0f, 0, 32);
    ImGui::DragFloat("段差化", &terrace_, 0.1f, 0.0f, 64.0f, "%.2f");
    ImGui::DragInt("Material Type", &materialType_, 1.0f, 0, 64);

    DrawRealtimePreview();

    if (ImGui::CollapsingHeader("ハイトマップ読み込み", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("画像パス", heightMapPathBuffer_, sizeof(heightMapPathBuffer_));
        ImGui::TextDisabled("PNG/JPG/BMPの白黒値を高さに加算します。空ならノイズ地形だけで生成します。");
        ImGui::DragFloat("ハイトマップ強度", &heightMapStrength_, 0.05f, 0.0f, 8.0f, "%.2f");
        ImGui::Checkbox("白黒を反転", &invertHeightMap_);
    }

    if (ImGui::CollapsingHeader("地形ペイント", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("ペイントマップを生成して貼る", &generatePaintMap_);
        ImGui::Combo("ペイントプリセット", &paintPresetIndex_, kPaintPresetLabels.data(), static_cast<int>(kPaintPresetLabels.size()));
        ImGui::DragFloat("色ムラの強さ", &paintStrength_, 0.02f, 0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("高さと傾斜から確認用のアルベドPNGを生成します。後から本格的なブラシ編集へ拡張する前提です。");
    }

    if (ImGui::CollapsingHeader("コリジョン", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("高さ地形コリジョンを付ける", &useTerrainHeightCollider_);
        ImGui::Checkbox("高さデータがない時は簡易AABBにする", &useSimpleAabbCollider_);
        ImGui::Checkbox("簡易AABBを生成高さに合わせる", &fitColliderToHeight_);
        ImGui::TextDisabled("高さ地形コリジョンは生成されたTerrain JSONを使って、地形面の高さで判定します。");
    }

    ImGui::Checkbox("生成後すぐシーン配置プレビューまで行う（重い場合あり）", &autoPreviewAfterGenerate_);

    if (isGenerating_) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(ICON_FA_COG " OBJ地形を生成")) {
        RunTerrainBuilder();
    }
    if (isGenerating_) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.45f, 0.9f, 1.0f, 1.0f), "生成中...");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FILE_IMPORT " レポート再読込")) {
        if (LoadLatestReport()) {
            SetNotice("Terrainレポートを読み込みました。", true);
        } else {
            SetNotice("Terrainレポートが見つかりません。", false);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS " シーンへ配置")) {
        CreateTerrainPreview();
    }

    if (!hasReport_) {
        return;
    }

    ImGui::Separator();
    ImGui::Text("最新レポート");
    if (ImGui::BeginTable("TerrainBuilderReport", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        auto row = [](const char* label, const std::string& value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", value.c_str());
        };
        row("Model", latestReport_.value("modelName", ""));
        row("OBJ", latestReport_.value("relativeObjPath", ""));
        row("Paint", latestReport_.value("paintMapPath", ""));
        row("Height", std::to_string(JsonFloat(latestReport_, "minHeight", 0.0f)) + " ～ " + std::to_string(JsonFloat(latestReport_, "maxHeight", 0.0f)));
        row("Resolution", std::to_string(latestReport_.value("resolution", 0)));
        row("Vertices", std::to_string(latestReport_.value("vertexCount", 0)));
        row("Triangles", std::to_string(latestReport_.value("triangleCount", 0)));
        ImGui::EndTable();
    }
#endif
}

void TerrainEditorWindow::DrawRealtimePreview() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("生成前リアルタイムプレビュー", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Checkbox("軽量プレビューを表示", &livePreviewEnabled_);
    ImGui::SameLine();
    ImGui::Checkbox("斜めワイヤーも表示", &previewWireframe_);
    ImGui::DragInt("プレビュー解像度", &previewResolution_, 1.0f, 8, 64);
    previewResolution_ = std::clamp(previewResolution_, 8, 64);
    ImGui::TextDisabled("ファイル生成前の低解像度確認です。生成後のモデル読み込みは行わないため、操作中に固まりにくくなります。");
    if (std::strlen(heightMapPathBuffer_) > 0) {
        ImGui::TextDisabled("注意: ハイトマップ画像の正確な反映はOBJ生成時に行います。ここではノイズと段差の目安だけ表示します。");
    }
    if (!livePreviewEnabled_) {
        return;
    }

    const int res = std::clamp(previewResolution_, 8, 64);
    const std::vector<float> heights = BuildPreviewHeightField(res);
    if (heights.empty()) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float availableWidth = std::max(220.0f, ImGui::GetContentRegionAvail().x);
    const float previewSize = std::clamp(availableWidth * 0.46f, 220.0f, 360.0f);
    const ImVec2 topOrigin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##TerrainTopdownPreview", ImVec2(previewSize, previewSize));

    const float cell = previewSize / static_cast<float>(res);
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const float h = heights[z * res + x];
            const ImU32 color = HeightToColor(h);
            const ImVec2 p0(topOrigin.x + x * cell, topOrigin.y + z * cell);
            const ImVec2 p1(topOrigin.x + (x + 1) * cell + 0.5f, topOrigin.y + (z + 1) * cell + 0.5f);
            drawList->AddRectFilled(p0, p1, color);
        }
    }
    drawList->AddRect(topOrigin, ImVec2(topOrigin.x + previewSize, topOrigin.y + previewSize), IM_COL32(170, 230, 255, 220), 4.0f, 0, 2.0f);
    ImGui::Text("上面プレビュー: %d x %d / 高さ %.2f", res, res, height_);

    if (!previewWireframe_) {
        return;
    }

    const float wireWidth = std::clamp(availableWidth * 0.70f, 300.0f, 560.0f);
    const float wireHeight = 210.0f;
    const ImVec2 wireOrigin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##TerrainWirePreview", ImVec2(wireWidth, wireHeight));

    const float gridScale = std::min(wireWidth / static_cast<float>(res) * 0.78f, 9.0f);
    const float heightScale = std::clamp(height_ * 8.0f, 10.0f, 80.0f);
    const ImVec2 center(wireOrigin.x + wireWidth * 0.5f, wireOrigin.y + wireHeight * 0.34f);
    const int step = std::max(1, res / 24);

    auto project = [&](int x, int z) -> ImVec2 {
        const float h = heights[z * res + x];
        const float fx = static_cast<float>(x) - static_cast<float>(res - 1) * 0.5f;
        const float fz = static_cast<float>(z) - static_cast<float>(res - 1) * 0.5f;
        return {
            center.x + (fx - fz) * gridScale,
            center.y + (fx + fz) * gridScale * 0.42f - h * heightScale,
        };
    };

    drawList->AddRectFilled(wireOrigin, ImVec2(wireOrigin.x + wireWidth, wireOrigin.y + wireHeight), IM_COL32(22, 38, 46, 120), 6.0f);
    for (int z = 0; z < res; z += step) {
        for (int x = 0; x + step < res; x += step) {
            const float h = heights[z * res + x];
            const ImU32 color = ImGui::ColorConvertFloat4ToU32(LerpColor(
                ImVec4(0.28f, 0.72f, 1.0f, 0.75f),
                ImVec4(1.0f, 0.95f, 0.45f, 0.95f),
                h));
            drawList->AddLine(project(x, z), project(x + step, z), color, 1.2f);
        }
    }
    for (int x = 0; x < res; x += step) {
        for (int z = 0; z + step < res; z += step) {
            const float h = heights[z * res + x];
            const ImU32 color = ImGui::ColorConvertFloat4ToU32(LerpColor(
                ImVec4(0.28f, 0.72f, 1.0f, 0.65f),
                ImVec4(1.0f, 0.95f, 0.45f, 0.90f),
                h));
            drawList->AddLine(project(x, z), project(x, z + step), color, 1.2f);
        }
    }
    drawList->AddRect(wireOrigin, ImVec2(wireOrigin.x + wireWidth, wireOrigin.y + wireHeight), IM_COL32(170, 230, 255, 150), 6.0f, 0, 1.0f);
    ImGui::Text("斜めプレビュー: 生成前の起伏目安");
#endif
}

std::vector<float> TerrainEditorWindow::BuildPreviewHeightField(int previewResolution) const {
    const int res = std::clamp(previewResolution, 8, 64);
    std::vector<float> field(static_cast<size_t>(res * res), 0.0f);
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            field[static_cast<size_t>(z * res + x)] = SamplePreviewHeight(x, z, res);
        }
    }

    const int previewSmooth = std::clamp(smoothSteps_, 0, 4);
    for (int i = 0; i < previewSmooth; ++i) {
        std::vector<float> smoothed = field;
        for (int z = 1; z < res - 1; ++z) {
            for (int x = 1; x < res - 1; ++x) {
                float total = 0.0f;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        total += field[static_cast<size_t>((z + dz) * res + (x + dx))];
                    }
                }
                smoothed[static_cast<size_t>(z * res + x)] = total / 9.0f;
            }
        }
        field = std::move(smoothed);
    }
    return field;
}

float TerrainEditorWindow::SamplePreviewHeight(int x, int z, int previewResolution) const {
    const int res = std::max(2, previewResolution);
    const float u = static_cast<float>(x) / static_cast<float>(res - 1);
    const float v = static_cast<float>(z) / static_cast<float>(res - 1);
    const float scale = std::max(0.001f, noiseScale_);
    const float sx = u * std::max(1.0f, sizeX_) * scale;
    const float sz = v * std::max(1.0f, sizeZ_) * scale;

    float value = 0.0f;
    float amplitude = 1.0f;
    float amplitudeTotal = 0.0f;
    for (int octave = 0; octave < 4; ++octave) {
        const float frequency = std::pow(2.0f, static_cast<float>(octave));
        value += ValueNoise(sx * frequency, sz * frequency, seed_ + octave * 17) * amplitude;
        amplitudeTotal += amplitude;
        amplitude *= 0.5f;
    }
    float normalizedHeight = value / std::max(0.0001f, amplitudeTotal);

    if (terrace_ > 0.001f && height_ > 0.001f) {
        float actualHeight = normalizedHeight * height_;
        actualHeight = std::round(actualHeight / terrace_) * terrace_;
        normalizedHeight = std::clamp(actualHeight / height_, 0.0f, 1.0f);
    }
    return std::clamp(normalizedHeight, 0.0f, 1.0f);
}

uint32_t TerrainEditorWindow::HeightToColor(float normalizedHeight) const {
    normalizedHeight = std::clamp(normalizedHeight, 0.0f, 1.0f);

    ImVec4 low;
    ImVec4 mid;
    ImVec4 high;
    switch (paintPresetIndex_) {
    case 1:
        low = ImVec4(0.78f, 0.62f, 0.35f, 1.0f);
        mid = ImVec4(0.95f, 0.78f, 0.43f, 1.0f);
        high = ImVec4(0.86f, 0.74f, 0.58f, 1.0f);
        break;
    case 2:
        low = ImVec4(0.34f, 0.36f, 0.34f, 1.0f);
        mid = ImVec4(0.48f, 0.50f, 0.47f, 1.0f);
        high = ImVec4(0.72f, 0.72f, 0.68f, 1.0f);
        break;
    default:
        low = ImVec4(0.34f, 0.50f, 0.25f, 1.0f);
        mid = ImVec4(0.40f, 0.72f, 0.30f, 1.0f);
        high = ImVec4(0.76f, 0.70f, 0.56f, 1.0f);
        break;
    }

    const ImVec4 color = normalizedHeight < 0.55f
        ? LerpColor(low, mid, normalizedHeight / 0.55f)
        : LerpColor(mid, high, (normalizedHeight - 0.55f) / 0.45f);

    auto toByte = [](float value) -> uint32_t {
        return static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return (toByte(color.w) << 24) | (toByte(color.z) << 16) | (toByte(color.y) << 8) | toByte(color.x);
}

bool TerrainEditorWindow::RunTerrainBuilder() {
    if (isGenerating_) {
        SetNotice("Terrain生成中です。完了まで少し待ってください。", false);
        return false;
    }

    const std::string name = terrainNameBuffer_;
    if (name.empty()) {
        SetNotice("地形名が空です。", false);
        return false;
    }

    resolution_ = std::clamp(resolution_, 2, 256);
    smoothSteps_ = std::clamp(smoothSteps_, 0, 32);
    sizeX_ = std::clamp(sizeX_, 1.0f, 10000.0f);
    sizeZ_ = std::clamp(sizeZ_, 1.0f, 10000.0f);
    height_ = std::clamp(height_, 0.0f, 1000.0f);
    noiseScale_ = std::clamp(noiseScale_, 0.001f, 10.0f);
    terrace_ = std::clamp(terrace_, 0.0f, 64.0f);
    heightMapStrength_ = std::clamp(heightMapStrength_, 0.0f, 8.0f);
    paintStrength_ = std::clamp(paintStrength_, 0.0f, 1.0f);
    paintPresetIndex_ = std::clamp(paintPresetIndex_, 0, static_cast<int>(kPaintPresetArgs.size()) - 1);

    std::string command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File ";
    command += QuoteCommandArg("tools/terrain/terrain_mesh_builder.ps1");
    command += " -Name " + QuoteCommandArg(name);
    command += " -Resolution " + std::to_string(resolution_);
    command += " -SizeX " + FormatFloat(sizeX_);
    command += " -SizeZ " + FormatFloat(sizeZ_);
    command += " -Height " + FormatFloat(height_);
    command += " -Seed " + std::to_string(seed_);
    command += " -NoiseScale " + FormatFloat(noiseScale_);
    command += " -SmoothSteps " + std::to_string(smoothSteps_);
    command += " -Terrace " + FormatFloat(terrace_);
    command += " -HeightMapStrength " + FormatFloat(heightMapStrength_);
    command += " -PaintPreset " + QuoteCommandArg(kPaintPresetArgs[paintPresetIndex_]);
    command += " -PaintStrength " + FormatFloat(paintStrength_);
    if (std::strlen(heightMapPathBuffer_) > 0) {
        command += " -HeightMapPath " + QuoteCommandArg(heightMapPathBuffer_);
    }
    if (invertHeightMap_) {
        command += " -InvertHeightMap";
    }
    if (generatePaintMap_) {
        command += " -GeneratePaintMap";
    }

    isGenerating_ = true;
    previewAfterAsyncGenerate_ = autoPreviewAfterGenerate_;
    SetNotice("Terrain生成ツールをバックグラウンドで実行中...", true);
    generationFuture_ = std::async(std::launch::async, [command]() -> uint32_t {
        DWORD exitCode = 1;
        if (!RunHiddenProcessAndWait(command, &exitCode)) {
            return 0xFFFFFFFF;
        }
        return static_cast<uint32_t>(exitCode);
    });
    return true;
}

void TerrainEditorWindow::PollTerrainBuilder() {
    if (!isGenerating_ || !generationFuture_.valid()) {
        return;
    }

    if (generationFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    const uint32_t exitCode = generationFuture_.get();
    isGenerating_ = false;
    if (exitCode != 0) {
        SetNotice("Terrain生成ツールの実行に失敗しました。パスや入力画像を確認してください。", false);
        return;
    }

    if (!LoadLatestReport()) {
        SetNotice("Terrain生成は完了しましたが、レポートを読み込めませんでした。", false);
        return;
    }

    SetNotice("Terrain生成が完了しました。", true);
    if (previewAfterAsyncGenerate_) {
        CreateTerrainPreview();
    }
}

bool TerrainEditorWindow::LoadLatestReport() {
    nlohmann::json report;
    if (!ReadJsonFile("Resources/.cache/terrain_builder/latest_report.json", report)) {
        hasReport_ = false;
        return false;
    }
    latestReport_ = std::move(report);
    hasReport_ = true;
    if (latestReport_.contains("name") && latestReport_["name"].is_string()) {
        strncpy_s(terrainNameBuffer_, latestReport_["name"].get<std::string>().c_str(), _TRUNCATE);
    }
    return true;
}

void TerrainEditorWindow::CreateTerrainPreview() {
    if (!editor_ || !editor_->GetSceneManager() || !editor_->GetSceneManager()->GetCurrentScene()) {
        SetNotice("配置できるシーンがありません。", false);
        return;
    }
    if (!hasReport_ && !LoadLatestReport()) {
        SetNotice("配置するTerrainレポートがありません。先に生成してください。", false);
        return;
    }

    const std::string modelName = latestReport_.value("modelName", "");
    if (modelName.empty()) {
        SetNotice("TerrainレポートにmodelNameがありません。", false);
        return;
    }

    BaseScene* scene = editor_->GetSceneManager()->GetCurrentScene();
    Object3dCommon* common = scene->GetObject3dCommon();
    if (!common) {
        SetNotice("Object3dCommonが見つかりません。", false);
        return;
    }

    ModelManager::GetInstance()->ReloadModel(modelName);

    auto object = std::make_unique<Object3d>();
    object->Initialize(common);
    object->SetModel(modelName);
    object->SetClassName("Terrain");
    object->SetSaveCategory("Object");
    object->SetName("Terrain_" + MakeSafeObjectName(latestReport_.value("name", "Generated")));
    object->SetMaterialType(materialType_);
    object->SetTranslate({ 0.0f, 0.0f, 0.0f });

    const std::string paintMapPath = latestReport_.value("paintMapPath", "");
    if (!paintMapPath.empty()) {
        object->SetTexture(paintMapPath);
        object->SetAutoTextureTiling(false);
        object->SetTextureTiling({ 1.0f, 1.0f });
    }

    bool collisionConfigured = false;
    if (useTerrainHeightCollider_) {
        const std::string terrainCollisionPath = ResolveTerrainCollisionPathFromReport(latestReport_);
        if (!terrainCollisionPath.empty() && object->LoadTerrainCollisionFromFile(terrainCollisionPath)) {
            ColliderConfig config = object->GetColliderConfig();
            config.type = ColliderType::kTerrain;
            const float minHeight = JsonFloat(latestReport_, "minHeight", -0.05f);
            const float maxHeight = JsonFloat(latestReport_, "maxHeight", 0.05f);
            const float halfHeight = std::max(0.05f, (maxHeight - minHeight) * 0.5f);
            config.center = { 0.0f, (minHeight + maxHeight) * 0.5f, 0.0f };
            config.size = {
                std::max(0.05f, JsonFloat(latestReport_, "sizeX", sizeX_) * 0.5f),
                halfHeight,
                std::max(0.05f, JsonFloat(latestReport_, "sizeZ", sizeZ_) * 0.5f),
            };
            object->SetColliderConfig(config);
            object->SetCollisionAttribute(CollisionAttribute::kGround);
            object->SetCollisionMask(0xFFFFFFFF);
            object->SetStatic(true);
            collisionConfigured = true;
        } else if (!terrainCollisionPath.empty()) {
            SetNotice("Terrain高さコリジョンを読み込めません。簡易AABB設定を確認してください。", false);
        }
    }

    if (!collisionConfigured && useSimpleAabbCollider_) {
        ColliderConfig config = object->GetColliderConfig();
        config.type = ColliderType::kAABB;
        if (fitColliderToHeight_) {
            const float minHeight = JsonFloat(latestReport_, "minHeight", -0.05f);
            const float maxHeight = JsonFloat(latestReport_, "maxHeight", 0.05f);
            const float halfHeight = std::max(0.05f, (maxHeight - minHeight) * 0.5f);
            config.center = { 0.0f, (minHeight + maxHeight) * 0.5f, 0.0f };
            config.size = {
                std::max(0.05f, JsonFloat(latestReport_, "sizeX", sizeX_) * 0.5f),
                halfHeight,
                std::max(0.05f, JsonFloat(latestReport_, "sizeZ", sizeZ_) * 0.5f),
            };
        }
        object->SetColliderConfig(config);
        object->SetCollisionAttribute(CollisionAttribute::kGround);
        object->SetCollisionMask(0xFFFFFFFF);
        object->SetStatic(true);
        collisionConfigured = true;
    }

    if (!collisionConfigured) {
        object->SetColliderType(ColliderType::kNone);
        object->SetCollisionAttribute(0);
        object->SetCollisionMask(0);
    }
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();

    editor_->StartGameViewCreatePreview(std::move(object), "Create Terrain");
    SetNotice("Terrain配置プレビューを更新しました。Game Viewで位置を決めてください。", true);
}

void TerrainEditorWindow::SetNotice(const std::string& message, bool success) {
    lastStatus_ = message;
    noticeSuccess_ = success;
    noticeTimer_ = 3.0f;
    DebugConsole::GetInstance()->AddLog(message);
}
