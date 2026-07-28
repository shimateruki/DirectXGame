#define NOMINMAX
#include "ModelOptimizerWindow.h"

#include "DebugConsole.h"
#include "DebugEditor.h"
#include "EditorManager.h"
#include "EditorAssetDragPayload.h"
#include "EffectPreviewStage.h"
#include "IconsFontAwesome5.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "CameraManager.h"
#include "imgui.h"
#include "../../../math/Math.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {
namespace fs = std::filesystem;

constexpr const char* kLodPreviewPrefix = "__Editor_LODPreview_";

std::string NormalizeSlashes(std::string text) {
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool IsModelSourceExtension(const fs::path& path) {
    const std::string ext = ToLowerAscii(path.extension().string());
    return ext == ".obj" || ext == ".gltf" || ext == ".glb";
}

bool IsGeneratedLodFile(const fs::path& path) {
    const std::string stem = ToLowerAscii(path.stem().string());
    const std::string filename = ToLowerAscii(path.filename().string());
    return stem.find("_lod") != std::string::npos ||
        filename.find("_lod_report") != std::string::npos;
}

bool IsInsideDirectory(const fs::path& path, const fs::path& root) {
    std::error_code ec;
    const fs::path fullPath = fs::absolute(path, ec).lexically_normal();
    if (ec) return false;
    const fs::path fullRoot = fs::absolute(root, ec).lexically_normal();
    if (ec) return false;

    const std::string normalizedPath = ToLowerAscii(NormalizeSlashes(fullPath.generic_string()));
    std::string normalizedRoot = ToLowerAscii(NormalizeSlashes(fullRoot.generic_string()));
    if (!normalizedRoot.empty() && normalizedRoot.back() != '/') {
        normalizedRoot.push_back('/');
    }
    return normalizedPath.rfind(normalizedRoot, 0) == 0;
}

bool SafeDeleteGeneratedLodFile(const fs::path& path) {
    if (path.empty()) return false;
    if (!IsGeneratedLodFile(path)) return false;
    if (!IsInsideDirectory(path, "Resources/3DModel")) return false;

    std::error_code ec;
    if (!fs::exists(path, ec) || ec) {
        return false;
    }
    return fs::remove(path, ec) && !ec;
}

std::string ToModelName(const fs::path& fullPath) {
    std::error_code ec;
    fs::path relative = fs::relative(fullPath, "Resources/3DModel", ec);
    if (ec || relative.empty()) {
        return NormalizeSlashes(fullPath.generic_string());
    }

    const fs::path parent = relative.parent_path();
    const std::string stem = relative.stem().string();
    if (!parent.empty() && parent.filename().string() == stem) {
        return NormalizeSlashes(parent.generic_string());
    }

    return NormalizeSlashes(relative.generic_string());
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return L"";
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return "";
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::string QuoteCommandArg(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char c : value) {
        if (c == '"') {
            escaped += "\\\"";
        }
        else {
            escaped.push_back(c);
        }
    }
    escaped.push_back('"');
    return escaped;
}

std::string FormatFloat(float value) {
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    return buffer;
}

std::string FormatByteSize(std::int64_t bytes) {
    const char* units[] = { "B", "KB", "MB", "GB" };
    double value = static_cast<double>((std::max)(std::int64_t{ 0 }, bytes));
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < 3) {
        value /= 1024.0;
        ++unitIndex;
    }

    char buffer[64]{};
    if (unitIndex == 0) {
        std::snprintf(buffer, sizeof(buffer), "%lld %s", static_cast<long long>(bytes), units[unitIndex]);
    }
    else {
        std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unitIndex]);
    }
    return buffer;
}

ImU32 GetPreviewLabelColor(int level) {
    switch (level) {
    case 0:
        return IM_COL32(255, 255, 255, 255);
    case 1:
        return IM_COL32(110, 220, 255, 255);
    case 2:
        return IM_COL32(255, 205, 95, 255);
    default:
        return IM_COL32(210, 210, 230, 255);
    }
}

std::string BuildPreviewLabelText(const nlohmann::json& lod) {
    const int level = (lod.is_object() && lod.contains("level") && lod["level"].is_number())
        ? lod["level"].get<int>()
        : 0;
    std::ostringstream stream;
    stream << "LOD" << level;
    if (level == 0) {
        stream << " 元モデル";
    }
    else {
        const float distance = (lod.is_object() && lod.contains("distance") && lod["distance"].is_number())
            ? lod["distance"].get<float>()
            : 0.0f;
        stream << " " << FormatFloat(distance) << "m";
        const float reduction = (lod.is_object() && lod.contains("triangleReduction") && lod["triangleReduction"].is_number())
            ? lod["triangleReduction"].get<float>()
            : 0.0f;
        if (reduction > 0.0f) {
            stream << " -" << static_cast<int>(std::round(reduction * 100.0f)) << "%";
        }
    }
    return stream.str();
}

bool StartHiddenProcess(const std::string& commandLine, PROCESS_INFORMATION& processInfo) {
    std::wstring command = Utf8ToWide(commandLine);
    if (command.empty()) {
        return false;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    processInfo = {};

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    const BOOL created = CreateProcessW(
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

    if (!created) {
        return false;
    }

    return true;
}

bool ReadJsonFile(const fs::path& path, nlohmann::json& outJson) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    try {
        file >> outJson;
        return true;
    }
    catch (...) {
        return false;
    }
}

bool HasGltfUnsafeLodFeature(const nlohmann::json& gltf) {
    if (gltf.contains("skins") && gltf["skins"].is_array() && !gltf["skins"].empty()) {
        return true;
    }
    if (gltf.contains("animations") && gltf["animations"].is_array() && !gltf["animations"].empty()) {
        return true;
    }

    if (gltf.contains("nodes") && gltf["nodes"].is_array()) {
        for (const auto& node : gltf["nodes"]) {
            if (!node.is_object()) continue;
            if (node.contains("skin")) {
                return true;
            }
            if (node.contains("name") && node["name"].is_string()) {
                std::string name = ToLowerAscii(node["name"].get<std::string>());
                if (name.find("armature") != std::string::npos) {
                    return true;
                }
            }
        }
    }

    if (gltf.contains("meshes") && gltf["meshes"].is_array()) {
        for (const auto& mesh : gltf["meshes"]) {
            if (!mesh.is_object() || !mesh.contains("primitives") || !mesh["primitives"].is_array()) continue;
            for (const auto& primitive : mesh["primitives"]) {
                if (!primitive.is_object()) continue;
                if (primitive.contains("targets")) {
                    return true;
                }
                const int mode = primitive.value("mode", 4);
                if (mode != 4) {
                    return true;
                }
                if (primitive.contains("attributes") && primitive["attributes"].is_object()) {
                    const auto& attributes = primitive["attributes"];
                    if (attributes.contains("JOINTS_0") || attributes.contains("WEIGHTS_0")) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

bool IsSupportedOptimizerSource(const fs::path& path) {
    const std::string ext = ToLowerAscii(path.extension().string());
    if (ext == ".obj") {
        return true;
    }
    if (ext == ".glb") {
        return true;
    }
    if (ext != ".gltf") {
        return false;
    }

    nlohmann::json gltf;
    if (!ReadJsonFile(path, gltf)) {
        return false;
    }
    return !HasGltfUnsafeLodFeature(gltf);
}

int ReadIntOrZero(const nlohmann::json& value, const char* key) {
    if (!value.is_object() || !value.contains(key)) return 0;
    if (!value[key].is_number()) return 0;
    return value[key].get<int>();
}

float ReadFloatOrZero(const nlohmann::json& value, const char* key) {
    if (!value.is_object() || !value.contains(key)) return 0.0f;
    if (!value[key].is_number()) return 0.0f;
    return value[key].get<float>();
}

std::int64_t ReadInt64OrZero(const nlohmann::json& value, const char* key) {
    if (!value.is_object() || !value.contains(key)) return 0;
    if (!value[key].is_number_integer() && !value[key].is_number_unsigned()) return 0;
    return value[key].get<std::int64_t>();
}

bool IsLodPreviewObject(const Object3d* object) {
    if (!object) return false;
    return object->GetName().rfind(kLodPreviewPrefix, 0) == 0 || object->GetClassName() == "EditorOnly_LODPreview";
}
}

void ModelOptimizerWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
    RefreshModelList();
}

void ModelOptimizerWindow::SetGameViewRegion(const Vector2& offset, const Vector2& size) {
    gameViewOffsetX_ = offset.x;
    gameViewOffsetY_ = offset.y;
    gameViewWidth_ = size.x;
    gameViewHeight_ = size.y;
}

void ModelOptimizerWindow::DrawImGui() {
#ifdef USE_IMGUI
    UpdateBuilderProcess();
    UpdatePreviewCreation();
    DrawPreviewLabels();

    ImGui::Text(ICON_FA_COMPRESS_ARROWS_ALT " モデル最適化 / LOD生成");
    ImGui::Separator();

    ImGui::TextWrapped("外部ツールで軽量LOD候補とレポートを生成します。エンジン実行時に毎回作るのではなく、制作時に確認して採用するためのツールです。");
    ImGui::TextDisabled("対象: OBJ / 静的glTF。スキン、ボーンアニメ、モーフ、glbは自動LOD候補から除外します。");
    ImGui::Spacing();

    if (ImGui::Button(ICON_FA_SYNC " モデル一覧更新")) {
        RefreshModelList();
    }
    ImGui::SameLine();
    if (editor_ && editor_->GetSelectedObject3D() && ImGui::Button(ICON_FA_CROSSHAIRS " 選択Objectから取得")) {
        SetModelName(editor_->GetSelectedObject3D()->GetModelName());
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("モデル", modelNameBuffer_[0] ? modelNameBuffer_ : "選択なし")) {
        for (int i = 0; i < static_cast<int>(modelCandidates_.size()); ++i) {
            const bool selected = i == selectedModelIndex_;
            if (ImGui::Selectable(modelCandidates_[i].c_str(), selected)) {
                selectedModelIndex_ = i;
                SetModelName(modelCandidates_[i]);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
            const std::string modelName = ReadEditorAssetDragPath(payload->Data, payload->DataSize);
            if (!modelName.empty()) {
                SetModelName(modelName);
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::InputText("直接指定", modelNameBuffer_, sizeof(modelNameBuffer_));

    ImGui::Separator();
    ImGui::Text("LOD設定");
    ImGui::DragFloat("LOD1 保持率（高いほど形を残す）", &lodRatios_[0], 0.01f, 0.05f, 0.95f, "%.2f");
    ImGui::DragFloat("LOD1 距離", &lodDistances_[0], 0.5f, 1.0f, 500.0f, "%.1f");
    ImGui::DragFloat("LOD2 保持率（高いほど形を残す）", &lodRatios_[1], 0.01f, 0.02f, 0.90f, "%.2f");
    ImGui::DragFloat("LOD2 距離", &lodDistances_[1], 0.5f, 1.0f, 800.0f, "%.1f");
    ImGui::TextDisabled("保持率はBlender Decimateのratioです。1.00に近いほど元モデルに近く、低いほど強く簡略化します。");
    const char* backendLabels[] = {
        "Auto: Blender優先 / なければ内蔵簡易",
        "Blender CLI: 高品質LOD生成",
        "内蔵簡易: グリッド簡略化"
    };
    ImGui::Combo("生成方式", &selectedBackendIndex_, backendLabels, IM_ARRAYSIZE(backendLabels));
    ImGui::InputText("Blender.exe パス", blenderPathBuffer_, sizeof(blenderPathBuffer_));
    ImGui::TextDisabled("未指定なら tools/blender/blender.exe を優先し、無ければPATHから探します。Autoでは見つからない場合だけ内蔵簡易へ戻します。");
    ImGui::Checkbox("既存LODを上書き", &forceOverwrite_);
    ImGui::Checkbox("Effect Preview Stageで比較", &autoUseEffectPreviewStage_);
    ImGui::Checkbox("Previewラベルを表示", &showPreviewLabels_);

    if (builderRunning_) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "外部LODツールをバックグラウンド実行中... PID: %lu", builderProcessId_);
    }
    if (previewCreationActive_) {
        ImGui::TextColored(
            ImVec4(0.35f, 0.85f, 1.0f, 1.0f),
            "LOD Previewを分割生成中... %zu / %zu",
            pendingPreviewIndex_,
            pendingPreviewItems_.size());
    }

    if (builderRunning_) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(ICON_FA_SEARCH " 解析のみ")) {
        RunBuilder(true);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_COG " LOD生成")) {
        RunBuilder(false);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FILE_IMPORT " レポート再読込")) {
        LoadLatestReport();
    }
    if (builderRunning_) {
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    ImGui::TextWrapped("%s", lastStatus_.c_str());

    if (!hasReport_) {
        return;
    }

    ImGui::Separator();
    ImGui::Text("レポート: %s", BuildReportSummary().c_str());
    if (latestReport_.contains("selectedBackend") && latestReport_["selectedBackend"].is_string()) {
        ImGui::TextDisabled("使用バックエンド: %s", latestReport_["selectedBackend"].get<std::string>().c_str());
    }

    if (ImGui::Button(ICON_FA_EYE " 生成LODを並べて確認")) {
        CreatePreviewObjects();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CHECK " 選択ObjectにLOD設定を採用")) {
        ApplyLodConfigToSelected();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TRASH " Preview削除")) {
        RemovePreviewObjects();
    }
    ImGui::TextDisabled("Preview数: %d / 採用すると元モデルを保ったまま、距離で軽量モデルへ切り替わります。", CountPreviewObjects());

    if (hasPendingGeneratedReview_) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.35f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " 生成したLODを確認中");
        ImGui::TextWrapped("見た目を確認して、問題なければ採用してください。違和感がある場合は破棄すると生成LODファイルを自動で削除します。");
        if (ImGui::Button(ICON_FA_CHECK " このLODを採用", ImVec2(180.0f, 0.0f))) {
            AcceptGeneratedLods();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TRASH " 破棄して削除", ImVec2(180.0f, 0.0f))) {
            RejectGeneratedLods();
        }
    }

    if (latestReport_.contains("warnings") && latestReport_["warnings"].is_array() && !latestReport_["warnings"].empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.25f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " Warning");
        for (const auto& warning : latestReport_["warnings"]) {
            if (warning.is_string()) {
                ImGui::BulletText("%s", warning.get<std::string>().c_str());
            }
        }
    }

    if (!latestReport_.contains("lods") || !latestReport_["lods"].is_array()) {
        return;
    }

    if (ImGui::BeginTable("ModelOptimizerLodTable", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("LOD", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("モデル", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("距離", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("頂点", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("三角形", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("削減", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("状態", ImGuiTableColumnFlags_WidthFixed, 108.0f);
        ImGui::TableSetupColumn("容量", ImGuiTableColumnFlags_WidthFixed, 112.0f);
        ImGui::TableHeadersRow();

        for (const auto& lod : latestReport_["lods"]) {
            if (!lod.is_object()) continue;
            const int level = ReadIntOrZero(lod, "level");
            const std::string modelName = lod.value("modelName", "");
            const auto stats = lod.value("stats", nlohmann::json::object());
            const int vertices = ReadIntOrZero(stats, "vertices");
            const int triangles = ReadIntOrZero(stats, "triangles");
            const float distance = ReadFloatOrZero(lod, "distance");
            const float reduction = ReadFloatOrZero(lod, "triangleReduction");
            const std::int64_t fileSize = ReadInt64OrZero(stats, "fileSizeBytes");
            const std::int64_t storageDelta = ReadInt64OrZero(lod, "storageDeltaBytes");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("LOD%d", level);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", modelName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.1f", distance);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", vertices);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", triangles);
            ImGui::TableSetColumnIndex(5);
            if (level == 0) {
                ImGui::TextDisabled("-");
            }
            else {
                ImGui::Text("%.0f%%", reduction * 100.0f);
            }
            ImGui::TableSetColumnIndex(6);
            if (level == 0) {
                ImGui::TextDisabled("元");
            }
            else {
                ImGui::TextDisabled("生成済み");
            }
            ImGui::TableSetColumnIndex(7);
            if (level == 0) {
                ImGui::Text("%s", FormatByteSize(fileSize).c_str());
            }
            else {
                const ImVec4 color = storageDelta <= 0
                    ? ImVec4(0.45f, 1.0f, 0.55f, 1.0f)
                    : ImVec4(1.0f, 0.78f, 0.25f, 1.0f);
                ImGui::Text("%s", FormatByteSize(fileSize).c_str());
                ImGui::SameLine();
                ImGui::TextColored(color, "(%+lld)", static_cast<long long>(storageDelta));
            }
        }
        ImGui::EndTable();
    }
#endif
}

void ModelOptimizerWindow::RefreshModelList() {
    modelCandidates_.clear();

    const fs::path root = "Resources/3DModel";
    if (!fs::exists(root)) {
        lastStatus_ = "Resources/3DModel が見つかりません。";
        return;
    }

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const fs::path path = entry.path();
        if (!IsModelSourceExtension(path) || IsGeneratedLodFile(path)) continue;
        if (!IsSupportedOptimizerSource(path)) continue;

        const std::string modelName = ToModelName(path);
        if (std::find(modelCandidates_.begin(), modelCandidates_.end(), modelName) == modelCandidates_.end()) {
            modelCandidates_.push_back(modelName);
        }
    }

    std::sort(modelCandidates_.begin(), modelCandidates_.end());

    selectedModelIndex_ = -1;
    for (int i = 0; i < static_cast<int>(modelCandidates_.size()); ++i) {
        if (modelCandidates_[i] == modelNameBuffer_) {
            selectedModelIndex_ = i;
            break;
        }
    }

    lastStatus_ = "モデル一覧を更新しました。";
}

void ModelOptimizerWindow::SetModelName(const std::string& modelName) {
    strncpy_s(modelNameBuffer_, modelName.c_str(), _TRUNCATE);
    selectedModelIndex_ = -1;
    for (int i = 0; i < static_cast<int>(modelCandidates_.size()); ++i) {
        if (modelCandidates_[i] == modelName) {
            selectedModelIndex_ = i;
            break;
        }
    }
}

bool ModelOptimizerWindow::RunBuilder(bool analyzeOnly) {
    if (builderRunning_) {
        lastStatus_ = "外部LODツールはすでに実行中です。完了を待ってください。";
        return false;
    }

    const std::string modelName = modelNameBuffer_;
    if (modelName.empty()) {
        lastStatus_ = "モデルが選択されていません。";
        return false;
    }

    if (!analyzeOnly) {
        pendingApplyTarget_ = editor_ ? editor_->GetSelectedObject3D() : nullptr;
        hasPendingGeneratedReview_ = false;
    }

    std::string command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File ";
    command += QuoteCommandArg("tools/model_lod/model_lod_builder.ps1");
    command += " -Model " + QuoteCommandArg(modelName);
    command += " -Ratio1 " + FormatFloat(lodRatios_[0]);
    command += " -Ratio2 " + FormatFloat(lodRatios_[1]);
    command += " -Distance1 " + FormatFloat(lodDistances_[0]);
    command += " -Distance2 " + FormatFloat(lodDistances_[1]);
    if (!analyzeOnly || forceOverwrite_) {
        command += " -Force";
    }
    const char* backendArgs[] = { "auto", "blender", "native" };
    const int backendIndex = std::clamp(selectedBackendIndex_, 0, 2);
    command += " -Backend " + QuoteCommandArg(backendArgs[backendIndex]);
    if (blenderPathBuffer_[0] != '\0') {
        command += " -BlenderPath " + QuoteCommandArg(blenderPathBuffer_);
    }
    if (analyzeOnly) {
        command += " -AnalyzeOnly";
    }
    PROCESS_INFORMATION processInfo{};
    if (!StartHiddenProcess(command, processInfo)) {
        lastStatus_ = "外部LODツールを開始できませんでした。";
        DebugConsole::GetInstance()->AddLog(lastStatus_);
        return false;
    }

    builderRunning_ = true;
    builderAnalyzeOnly_ = analyzeOnly;
    builderProcessHandle_ = processInfo.hProcess;
    builderThreadHandle_ = processInfo.hThread;
    builderProcessId_ = processInfo.dwProcessId;

    lastStatus_ = analyzeOnly
        ? "モデル解析をバックグラウンド実行中です。Editor操作は継続できます。"
        : "LOD生成をバックグラウンド実行中です。完了後にプレビュー確認を実行してください。";
    DebugConsole::GetInstance()->AddLog(lastStatus_);
    return true;
}

void ModelOptimizerWindow::UpdateBuilderProcess() {
    if (!builderRunning_ || !builderProcessHandle_) {
        return;
    }

    HANDLE processHandle = static_cast<HANDLE>(builderProcessHandle_);
    const DWORD waitResult = WaitForSingleObject(processHandle, 0);
    if (waitResult == WAIT_TIMEOUT) {
        return;
    }

    DWORD exitCode = 1;
    if (waitResult == WAIT_OBJECT_0) {
        GetExitCodeProcess(processHandle, &exitCode);
    }

    CloseBuilderProcessHandles();
    builderRunning_ = false;
    builderProcessId_ = 0;
    FinishBuilderProcess(exitCode);
}

void ModelOptimizerWindow::FinishBuilderProcess(unsigned long exitCode) {
    if (exitCode != 0) {
        lastStatus_ = "外部LODツールの実行に失敗しました。";
        DebugConsole::GetInstance()->AddLog(lastStatus_);
        return;
    }

    if (!LoadLatestReport()) {
        lastStatus_ = "LODツールは完了しましたが、レポートを読み込めませんでした。";
        DebugConsole::GetInstance()->AddLog(lastStatus_);
        return;
    }

    if (!builderAnalyzeOnly_) {
        hasPendingGeneratedReview_ = true;
        lastStatus_ = "LOD生成が完了しました。必要に応じて「生成LODを並べて確認」を押してから採用または破棄してください。";
    }
    else {
        lastStatus_ = "モデル解析が完了しました。";
    }
    DebugConsole::GetInstance()->AddLog(lastStatus_);
}

void ModelOptimizerWindow::CloseBuilderProcessHandles() {
    if (builderThreadHandle_) {
        CloseHandle(static_cast<HANDLE>(builderThreadHandle_));
        builderThreadHandle_ = nullptr;
    }
    if (builderProcessHandle_) {
        CloseHandle(static_cast<HANDLE>(builderProcessHandle_));
        builderProcessHandle_ = nullptr;
    }
}

void ModelOptimizerWindow::UpdatePreviewCreation() {
    if (!previewCreationActive_) {
        return;
    }
    if (!editor_ || !editor_->GetSceneManager() || !editor_->GetSceneManager()->GetCurrentScene()) {
        pendingPreviewItems_.clear();
        pendingPreviewIndex_ = 0;
        firstPendingPreview_ = nullptr;
        previewCreationActive_ = false;
        lastStatus_ = "LOD Previewを生成できるシーンがありません。";
        DebugConsole::GetInstance()->AddLog(lastStatus_);
        return;
    }

    BaseScene* scene = editor_->GetSceneManager()->GetCurrentScene();
    Object3dCommon* common = scene->GetObject3dCommon();
    if (!common) {
        pendingPreviewItems_.clear();
        pendingPreviewIndex_ = 0;
        firstPendingPreview_ = nullptr;
        previewCreationActive_ = false;
        lastStatus_ = "Object3dCommonが取得できません。";
        DebugConsole::GetInstance()->AddLog(lastStatus_);
        return;
    }

    if (pendingPreviewIndex_ >= pendingPreviewItems_.size()) {
        previewCreationActive_ = false;
        pendingPreviewItems_.clear();
        pendingPreviewIndex_ = 0;
        if (firstPendingPreview_) {
            editor_->SetSelectedObject(firstPendingPreview_);
            EditorManager::GetInstance()->SetSelectedObject(this);
        }
        firstPendingPreview_ = nullptr;
        lastStatus_ = "生成LODのPreviewを配置しました。Hierarchyでは __Editor_LODPreview_ で確認できます。";
        DebugConsole::GetInstance()->AddLog(lastStatus_);
        return;
    }

    const PendingPreviewItem item = pendingPreviewItems_[pendingPreviewIndex_];
    auto object = std::make_unique<Object3d>();
    object->Initialize(common);
    object->SetName(std::string(kLodPreviewPrefix) + "LOD" + std::to_string(item.level));
    object->SetClassName("EditorOnly_LODPreview");
    object->SetSaveCategory("Object");
    object->SetIsLocked(true);
    object->SetCollisionAttribute(0);
    object->SetCollisionMask(0);
    object->SetModel(item.modelName);
    object->SetTranslate({ item.x, item.y, item.z });
    object->SetScale({ 1.0f, 1.0f, 1.0f });
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();

    Object3d* rawPreview = object.get();
    scene->GetObjects().push_back(std::move(object));
    if (!firstPendingPreview_) {
        firstPendingPreview_ = rawPreview;
    }
    ++pendingPreviewIndex_;
}

void ModelOptimizerWindow::DrawPreviewLabels() {
#ifdef USE_IMGUI
    if (!showPreviewLabels_) {
        return;
    }
    if (gameViewWidth_ <= 1.0f || gameViewHeight_ <= 1.0f) {
        return;
    }
    if (!editor_ || !editor_->GetSceneManager() || !editor_->GetSceneManager()->GetCurrentScene()) {
        return;
    }
    if (!hasReport_ || !latestReport_.contains("lods") || !latestReport_["lods"].is_array()) {
        return;
    }

    BaseScene* scene = editor_->GetSceneManager()->GetCurrentScene();
    ImDrawList* drawList = ImGui::GetForegroundDrawList();

    for (const auto& lod : latestReport_["lods"]) {
        if (!lod.is_object()) {
            continue;
        }
        const int level = (lod.contains("level") && lod["level"].is_number()) ? lod["level"].get<int>() : 0;
        const std::string previewName = std::string(kLodPreviewPrefix) + "LOD" + std::to_string(level);

        Object3d* previewObject = nullptr;
        for (auto& object : scene->GetObjects()) {
            if (object && object->GetName() == previewName) {
                previewObject = object.get();
                break;
            }
        }
        if (!previewObject) {
            continue;
        }

        const AABB bounds = previewObject->GetModelWorldAABB();
        Vector3 labelWorld = previewObject->GetWorldPosition();
        labelWorld.y = bounds.max.y + 0.75f;

        Vector2 labelScreen;
        if (!ProjectWorldToGameView(labelWorld, labelScreen)) {
            continue;
        }

        const std::string label = BuildPreviewLabelText(lod);
        const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        const ImVec2 textPos = {
            labelScreen.x - textSize.x * 0.5f,
            labelScreen.y - textSize.y * 0.5f
        };
        const ImVec2 rectMin = { textPos.x - 8.0f, textPos.y - 5.0f };
        const ImVec2 rectMax = { textPos.x + textSize.x + 8.0f, textPos.y + textSize.y + 5.0f };
        const ImU32 labelColor = GetPreviewLabelColor(level);

        drawList->AddRectFilled(rectMin, rectMax, IM_COL32(12, 16, 22, 210), 6.0f);
        drawList->AddRect(rectMin, rectMax, labelColor, 6.0f, 0, 1.6f);
        drawList->AddText(textPos, IM_COL32(0, 0, 0, 180), label.c_str());
        drawList->AddText({ textPos.x, textPos.y - 1.0f }, labelColor, label.c_str());
    }
#endif
}

bool ModelOptimizerWindow::ProjectWorldToGameView(const Vector3& world, Vector2& screenOut) const {
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) {
        return false;
    }

    Matrix4x4 viewProjection = Math::Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    Vector3 ndc = Math::Transform(world, viewProjection);
    if (ndc.z < 0.0f || ndc.z > 1.0f) {
        return false;
    }

    screenOut.x = gameViewOffsetX_ + (ndc.x + 1.0f) * 0.5f * gameViewWidth_;
    screenOut.y = gameViewOffsetY_ + (1.0f - ndc.y) * 0.5f * gameViewHeight_;
    return screenOut.x >= gameViewOffsetX_ - 96.0f &&
           screenOut.x <= gameViewOffsetX_ + gameViewWidth_ + 96.0f &&
           screenOut.y >= gameViewOffsetY_ - 96.0f &&
           screenOut.y <= gameViewOffsetY_ + gameViewHeight_ + 96.0f;
}

bool ModelOptimizerWindow::LoadLatestReport() {
    nlohmann::json report;
    const fs::path reportPath = "Resources/.cache/model_lod/latest_report.json";
    if (!ReadJsonFile(reportPath, report)) {
        hasReport_ = false;
        return false;
    }

    latestReport_ = std::move(report);
    hasReport_ = true;
    if (latestReport_.contains("sourceModel") && latestReport_["sourceModel"].is_string()) {
        SetModelName(latestReport_["sourceModel"].get<std::string>());
    }
    return true;
}

bool ModelOptimizerWindow::ApplyLodConfigToSelected() {
    return ApplyLodConfigToObject(editor_ ? editor_->GetSelectedObject3D() : nullptr);
}

bool ModelOptimizerWindow::ApplyLodConfigToObject(Object3d* object) {
    if (!editor_ || !object) {
        lastStatus_ = "LODを適用するObjectが選択されていません。";
        return false;
    }

    if (!hasReport_ || !latestReport_.contains("lods") || !latestReport_["lods"].is_array()) {
        lastStatus_ = "LODレポートが読み込まれていません。";
        return false;
    }

    const std::string sourceModel = latestReport_.value("sourceModel", "");
    if (sourceModel.empty()) {
        lastStatus_ = "元モデル名がレポートにありません。";
        return false;
    }

    std::vector<Object3d::LodLevel> levels;
    for (const auto& lod : latestReport_["lods"]) {
        if (!lod.is_object()) continue;
        const int level = ReadIntOrZero(lod, "level");
        if (level <= 0) continue;

        Object3d::LodLevel levelData;
        levelData.level = level;
        levelData.modelName = lod.value("modelName", "");
        levelData.distance = ReadFloatOrZero(lod, "distance");
        if (!levelData.modelName.empty()) {
            levels.push_back(levelData);
        }
    }

    if (levels.empty()) {
        lastStatus_ = "生成済みLODがありません。OBJモデルでLOD生成を実行してください。";
        return false;
    }

    const nlohmann::json beforeState = editor_->CaptureObjectState(object);
    object->SetModel(sourceModel);
    object->SetLodLevels(levels);
    object->SetLodEnabled(true);
    editor_->RegisterObjectEdited(object, beforeState, "Apply LOD Config");
    editor_->MarkDirtyForObject(object);

    lastStatus_ = "選択Objectに距離LOD設定を採用しました: " + sourceModel;
    DebugConsole::GetInstance()->AddLog(lastStatus_);
    return true;
}

bool ModelOptimizerWindow::AcceptGeneratedLods() {
    Object3d* target = pendingApplyTarget_;
    if (!target && editor_) {
        target = editor_->GetSelectedObject3D();
    }

    if (!ApplyLodConfigToObject(target)) {
        return false;
    }

    hasPendingGeneratedReview_ = false;
    pendingApplyTarget_ = nullptr;
    RemovePreviewObjects();
    lastStatus_ = "LODを採用しました。距離に応じて軽量モデルへ切り替わります。";
    DebugConsole::GetInstance()->AddLog(lastStatus_);
    return true;
}

bool ModelOptimizerWindow::RejectGeneratedLods() {
    const int deletedCount = DeleteGeneratedLodFilesFromReport();
    RemovePreviewObjects();
    hasPendingGeneratedReview_ = false;
    pendingApplyTarget_ = nullptr;

    lastStatus_ = "LODを破棄しました。生成ファイル削除数: " + std::to_string(deletedCount);
    DebugConsole::GetInstance()->AddLog(lastStatus_);
    return true;
}

int ModelOptimizerWindow::DeleteGeneratedLodFilesFromReport() {
    if (!hasReport_ || !latestReport_.is_object()) {
        return 0;
    }

    std::set<std::string> deleteTargets;
    if (latestReport_.contains("lods") && latestReport_["lods"].is_array()) {
        for (const auto& lod : latestReport_["lods"]) {
            if (!lod.is_object()) continue;
            if (ReadIntOrZero(lod, "level") <= 0) continue;
            if (!lod.value("generated", true)) continue;

            const std::string file = lod.value("file", std::string{});
            if (file.empty()) continue;

            fs::path lodPath = file;
            deleteTargets.insert(lodPath.generic_string());
            if (ToLowerAscii(lodPath.extension().string()) == ".gltf") {
                deleteTargets.insert(lodPath.replace_extension(".bin").generic_string());
            }
        }
    }

    const std::string sourceFile = latestReport_.value("sourceFile", std::string{});
    if (!sourceFile.empty()) {
        const fs::path sourcePath = sourceFile;
        const fs::path sourceDir = sourcePath.parent_path();
        const std::string sourceStem = sourcePath.stem().string();
        deleteTargets.insert((sourceDir / (sourceStem + "_lod.json")).generic_string());
        deleteTargets.insert((sourceDir / (sourceStem + "_lod_report.json")).generic_string());
    }

    int deletedCount = 0;
    for (const std::string& target : deleteTargets) {
        if (SafeDeleteGeneratedLodFile(target)) {
            ++deletedCount;
        }
    }
    return deletedCount;
}

void ModelOptimizerWindow::CreatePreviewObjects() {
    Object3d* selectedBeforeRemove = editor_ ? editor_->GetSelectedObject3D() : nullptr;
    if (IsLodPreviewObject(selectedBeforeRemove)) {
        selectedBeforeRemove = nullptr;
    }

    previewCreationActive_ = false;
    pendingPreviewItems_.clear();
    pendingPreviewIndex_ = 0;
    firstPendingPreview_ = nullptr;

    RemovePreviewObjects();

    if (!editor_ || !editor_->GetSceneManager() || !editor_->GetSceneManager()->GetCurrentScene()) {
        lastStatus_ = "LOD Previewを生成できるシーンがありません。";
        return;
    }
    if (!hasReport_ || !latestReport_.contains("lods") || !latestReport_["lods"].is_array()) {
        lastStatus_ = "LOD Preview用のレポートがありません。";
        return;
    }

    BaseScene* scene = editor_->GetSceneManager()->GetCurrentScene();
    Object3dCommon* common = scene->GetObject3dCommon();
    if (!common) {
        lastStatus_ = "Object3dCommonが取得できません。";
        return;
    }

    Vector3 origin = { 0.0f, 2.0f, 0.0f };
    if (autoUseEffectPreviewStage_) {
        EffectPreviewStage* stage = EffectPreviewStage::GetInstance();
        stage->EnableForToolPreview();
        stage->RequestCameraRecenter();
        origin = stage->GetPreviewPosition();
    }
    else if (selectedBeforeRemove) {
        origin = selectedBeforeRemove->GetTranslate();
        origin.x += 3.0f;
    }

    int previewCount = 0;
    for (const auto& lod : latestReport_["lods"]) {
        if (!lod.is_object()) continue;
        if (!lod.value("modelName", std::string{}).empty()) {
            ++previewCount;
        }
    }

    const float spacing = 4.0f;
    const float startOffset = previewCount > 0 ? -spacing * static_cast<float>(previewCount - 1) * 0.5f : 0.0f;
    int previewIndex = 0;
    for (const auto& lod : latestReport_["lods"]) {
        if (!lod.is_object()) continue;
        const int level = ReadIntOrZero(lod, "level");
        const std::string modelName = lod.value("modelName", "");
        if (modelName.empty()) continue;
        const std::string reportFilePath = lod.value("file", std::string{});
        if (!reportFilePath.empty() && !fs::exists(fs::path(reportFilePath))) {
            DebugConsole::GetInstance()->AddLog("LOD Preview skipped missing file: " + reportFilePath);
            continue;
        }

        PendingPreviewItem item;
        item.level = level;
        item.modelName = modelName;
        item.x = origin.x + startOffset + static_cast<float>(previewIndex) * spacing;
        item.y = origin.y;
        item.z = origin.z;
        pendingPreviewItems_.push_back(std::move(item));
        ++previewIndex;
    }

    if (pendingPreviewItems_.empty()) {
        lastStatus_ = "LOD Preview対象がありません。";
        DebugConsole::GetInstance()->AddLog(lastStatus_);
        return;
    }

    previewCreationActive_ = true;
    lastStatus_ = "LOD Previewを分割生成中です。Editor操作を止めずに1つずつ配置します。";
    DebugConsole::GetInstance()->AddLog(lastStatus_);
}

void ModelOptimizerWindow::RemovePreviewObjects() {
    previewCreationActive_ = false;
    pendingPreviewItems_.clear();
    pendingPreviewIndex_ = 0;
    firstPendingPreview_ = nullptr;

    if (!editor_ || !editor_->GetSceneManager() || !editor_->GetSceneManager()->GetCurrentScene()) {
        return;
    }

    BaseScene* scene = editor_->GetSceneManager()->GetCurrentScene();
    std::vector<Object3d*> targets;
    for (auto& object : scene->GetObjects()) {
        if (IsLodPreviewObject(object.get())) {
            targets.push_back(object.get());
        }
    }

    for (Object3d* object : targets) {
        scene->RequestRemoveObject(object);
    }
}

int ModelOptimizerWindow::CountPreviewObjects() const {
    if (!editor_ || !editor_->GetSceneManager() || !editor_->GetSceneManager()->GetCurrentScene()) {
        return 0;
    }

    int count = 0;
    for (const auto& object : editor_->GetSceneManager()->GetCurrentScene()->GetObjects()) {
        if (IsLodPreviewObject(object.get())) {
            ++count;
        }
    }
    return count;
}

std::string ModelOptimizerWindow::BuildReportSummary() const {
    if (!hasReport_ || !latestReport_.is_object()) {
        return "";
    }

    std::ostringstream stream;
    stream << latestReport_.value("sourceModel", "(unknown)");
    if (latestReport_.contains("source")) {
        const auto& source = latestReport_["source"];
        stream << " / 頂点 " << ReadIntOrZero(source, "vertices");
        stream << " / 三角形 " << ReadIntOrZero(source, "triangles");
    }
    if (latestReport_.contains("supportedForGeneration") && !latestReport_["supportedForGeneration"].get<bool>()) {
        stream << " / 生成非対応形式";
    }
    return stream.str();
}

std::string ModelOptimizerWindow::GetLodModelName(int lodLevel) const {
    if (!hasReport_ || !latestReport_.contains("lods") || !latestReport_["lods"].is_array()) {
        return "";
    }

    for (const auto& lod : latestReport_["lods"]) {
        if (!lod.is_object()) continue;
        if (ReadIntOrZero(lod, "level") == lodLevel && lod.contains("modelName") && lod["modelName"].is_string()) {
            return lod["modelName"].get<std::string>();
        }
    }
    return "";
}
