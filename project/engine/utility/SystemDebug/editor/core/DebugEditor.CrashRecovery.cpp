#define NOMINMAX
#include "DebugEditor.h"
#include "SceneManager.h"    
#include "BaseScene.h"      
#include "Object3d.h"
#include "imgui.h"
#include <fstream>
#include <string>
#include <vector>
#include "json.hpp"
#include "ImGuizmo.h"
#include "CameraManager.h"
#include "WinApp.h"
#include "Math.h"
#include "DirectXCommon.h"
#include "CollisionConfig.h"
#include "ModelManager.h"      
#include "InputManager.h"    
#include <cmath>
#include <cassert> 
#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include "GhostRecorder.h" 
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include "Transform.h"
#include "ParticleManager.h"
#include "EditorManager.h"
#include "PostEffectEditor.h"
#include "SpriteDebugEditor.h"
#include "ParticleEditor.h"
#include "GPUParticleEditor.h"
#include "VFXSequencerEditor.h"
#include "LightEditor.h"      
#include "IconsFontAwesome5.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <DebugConsole.h>
#include <CollisionManager.h>
#include <filesystem> // ファイル操作用
#include <BulletManager.h>
#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.h"
#include <PresetEditor.h>
#include <MeshEffectManager.h>
#include <unordered_map>
#include <utility>
namespace fs = std::filesystem;




namespace {
    constexpr float kCrashRecoveryAutosaveInterval = 3.0f;
    constexpr float kCrashRecoveryHeartbeatInterval = 5.0f;
    constexpr const char* kCrashRecoveryRoot = "Resources/.backup/crash_recovery";
    constexpr const char* kCrashRecoverySessionFile = "session.json";

    std::string ToGenericPath(const fs::path& path) {
        return path.generic_string();
    }

    std::string NowIsoString() {
        // 復元情報の人が読む時刻はローカル時間のISO形式で記録する。
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &time);
        std::ostringstream out;
        out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
        return out.str();
    }

    std::string MakeCrashRecoveryStamp() {
        // 同じ秒内の退避ファイルが重ならないようミリ秒をファイル名へ含める。
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        std::tm tm{};
        localtime_s(&tm, &time);
        std::ostringstream out;
        out << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_" << std::setw(3) << std::setfill('0') << millis;
        return out.str();
    }

    bool ReadJsonFileSafe(const fs::path& path, nlohmann::json& outJson) {
        // 復元データが存在しても不正なJSONなら採用しない。
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        try {
            file >> outJson;
            return outJson.is_object();
        }
        catch (...) {
            return false;
        }
    }

    bool WriteJsonFileSafe(const fs::path& path, const nlohmann::json& data) {
        // クラッシュ復元用のJSONを書き出し、失敗は復元状態として残さない。
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }

        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        file << data.dump(4);
        return file.good();
    }

    bool IsPathInside(const fs::path& child, const fs::path& parent) {
        // 回復対象は指定ルート配下だけに絞り、任意パスへの書き戻しを防ぐ。
        std::error_code ec;
        const fs::path childAbs = fs::absolute(child, ec).lexically_normal();
        if (ec) {
            return false;
        }
        const fs::path parentAbs = fs::absolute(parent, ec).lexically_normal();
        if (ec) {
            return false;
        }

        std::wstring childText = childAbs.native();
        std::wstring parentText = parentAbs.native();
        std::transform(childText.begin(), childText.end(), childText.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        std::transform(parentText.begin(), parentText.end(), parentText.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if (!parentText.empty() && parentText.back() != L'\\' && parentText.back() != L'/') {
            parentText.push_back(fs::path::preferred_separator);
        }
        return childText.rfind(parentText, 0) == 0;
    }

    bool IsCrashRecoverySourcePathAllowed(const fs::path& path) {
        // 自動復元対象を許可されたシーンデータの範囲へ限定する。
        const std::string generic = path.lexically_normal().generic_string();
        const std::string lower = [&generic]() {
            std::string value = generic;
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }();
        return lower.rfind("resources/json/", 0) == 0 &&
            lower.find("/../") == std::string::npos &&
            lower.find("resources/.backup/") != 0 &&
            lower.find("resources/.cache/") != 0;
    }
}

// ========================================================================
// 初期化
// ========================================================================

void DebugEditor::InitializeCrashRecovery() {
    // 前回セッションの候補を読む前に新しい保存先を作らず、復元の機会を残します。
    LoadCrashRecoveryCandidate();

    crashRecoverySessionId_ = "session_" + MakeCrashRecoveryStamp();
    crashRecoverySessionDir_ = ToGenericPath(fs::path(kCrashRecoveryRoot) / crashRecoverySessionId_);
    crashRecoveryDraftDir_ = ToGenericPath(fs::path(crashRecoverySessionDir_) / "draft");
    crashRecoveryLastFiles_ = nlohmann::json::array();
    crashRecoveryAutosaveTimer_ = kCrashRecoveryAutosaveInterval;
    crashRecoveryHeartbeatTimer_ = kCrashRecoveryHeartbeatInterval;
    crashRecoveryLastDirty_ = false;
    crashRecoverySessionActive_ = true;
    WriteCrashRecoverySession(false, false);
}

void DebugEditor::UpdateCrashRecovery(float deltaTime) {
#ifdef USE_IMGUI
    // 未保存状態の間だけドラフトを更新し、Heartbeatで異常終了を識別します。
    if (!crashRecoverySessionActive_) {
        return;
    }

    const bool dirty = HasAnyDirty();
    if (dirty && !crashRecoveryLastDirty_) {
        crashRecoveryAutosaveTimer_ = kCrashRecoveryAutosaveInterval;
    }

    crashRecoveryAutosaveTimer_ += deltaTime;
    crashRecoveryHeartbeatTimer_ += deltaTime;

    if (dirty && crashRecoveryAutosaveTimer_ >= kCrashRecoveryAutosaveInterval) {
        SaveCrashRecoveryDraft();
        crashRecoveryAutosaveTimer_ = 0.0f;
        crashRecoveryHeartbeatTimer_ = 0.0f;
    }
    else if (!dirty && (crashRecoveryLastDirty_ || crashRecoveryHeartbeatTimer_ >= kCrashRecoveryHeartbeatInterval)) {
        crashRecoveryLastFiles_ = nlohmann::json::array();
        WriteCrashRecoverySession(false, false);
        crashRecoveryHeartbeatTimer_ = 0.0f;
    }

    crashRecoveryLastDirty_ = dirty;
#endif
}

void DebugEditor::FinalizeCrashRecovery() {
    // 正常終了を記録し、次回起動時にこのセッションを復元候補から除外します。
    if (!crashRecoverySessionActive_) {
        return;
    }

    WriteCrashRecoverySession(true, HasAnyDirty(), crashRecoveryLastFiles_);
    crashRecoverySessionActive_ = false;
}

void DebugEditor::SaveCrashRecoveryDraft() {
    // シーン保存先のうち許可されたJSONだけをセッション専用領域へ複写します。
    if (!crashRecoverySessionActive_ || !sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return;
    }

    // 通常保存と同じシリアライザで全対象を作り、編集中Sceneの現在値をドラフトへ写します。
    auto targets = serializer_.BuildSceneSaveTargets(currentSceneFilename_, SaveMode::All);
    if (targets.empty()) {
        return;
    }

    nlohmann::json files = nlohmann::json::array();
    const fs::path draftRoot = fs::path(crashRecoveryDraftDir_);

    for (const auto& target : targets) {
        // Resources/json内の許可Path以外は復元対象にせず、ドラフトも作りません。
        if (!IsCrashRecoverySourcePathAllowed(target.path)) {
            continue;
        }

        const fs::path draftPath = draftRoot / fs::path(target.path);
        if (!IsPathInside(draftPath, draftRoot)) {
            continue;
        }

        if (!WriteJsonFileSafe(draftPath, target.data)) {
            continue;
        }

        files.push_back({
            { "label", target.label },
            { "source", fs::path(target.path).generic_string() },
            { "draft", ToGenericPath(draftPath) },
            { "metadata", target.isMetadata }
        });
    }

    if (files.empty()) {
        return;
    }

    // 少なくとも一件の書き込みに成功してから、未正常終了扱いのSessionへ反映します。
    crashRecoveryLastFiles_ = files;
    WriteCrashRecoverySession(false, true, crashRecoveryLastFiles_);
}

void DebugEditor::WriteCrashRecoverySession(bool cleanExit, bool dirty, const nlohmann::json& files) {
    // ドラフト一覧と終了状態をマニフェストにまとめ、起動時の復元判定に使います。
    if (crashRecoverySessionDir_.empty()) {
        return;
    }

    nlohmann::json session;
    const fs::path sessionPath = fs::path(crashRecoverySessionDir_) / kCrashRecoverySessionFile;
    ReadJsonFileSafe(sessionPath, session);
    if (!session.is_object()) {
        session = nlohmann::json::object();
    }

    // 既存の開始時刻・解決済みフラグは維持し、Heartbeatで変わる項目だけ更新します。
    session["version"] = 1;
    session["sessionId"] = crashRecoverySessionId_;
    session["updatedAt"] = NowIsoString();
    if (!session.contains("startedAt")) {
        session["startedAt"] = NowIsoString();
    }
    session["cleanExit"] = cleanExit;
    session["dirty"] = dirty;
    session["dirtySummary"] = GetDirtySummaryText();
    session["sceneFile"] = std::string(currentSceneFilename_);
    session["sessionDir"] = crashRecoverySessionDir_;
    session["draftRoot"] = crashRecoveryDraftDir_;
    session["files"] = files.is_array() ? files : nlohmann::json::array();
    if (!session.contains("resolved")) {
        session["resolved"] = false;
    }

    WriteJsonFileSafe(sessionPath, session);
}

void DebugEditor::LoadCrashRecoveryCandidate() {
    // 未正常終了かつ未保存データを持つ最新セッションのみを候補にします。
    crashRecoveryPending_ = false;
    crashRecoveryPromptOpened_ = false;
    crashRecoveryPendingManifest_.clear();

    const fs::path root = kCrashRecoveryRoot;
    std::error_code ec;
    if (!fs::exists(root, ec)) {
        return;
    }

    fs::file_time_type bestTime{};
    bool hasBest = false;
    nlohmann::json bestManifest;

    // 正常終了・解決済み・ドラフトなしを除き、更新時刻が最も新しい一件だけを提示します。
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (ec || !entry.is_directory()) {
            continue;
        }

        const fs::path manifestPath = entry.path() / kCrashRecoverySessionFile;
        nlohmann::json manifest;
        if (!ReadJsonFileSafe(manifestPath, manifest)) {
            continue;
        }

        if (manifest.value("cleanExit", true) ||
            manifest.value("resolved", false) ||
            !manifest.value("dirty", false) ||
            !manifest.contains("files") ||
            !manifest["files"].is_array() ||
            manifest["files"].empty()) {
            continue;
        }

        const fs::file_time_type writeTime = fs::last_write_time(manifestPath, ec);
        if (ec) {
            continue;
        }
        if (!hasBest || writeTime > bestTime) {
            bestTime = writeTime;
            bestManifest = manifest;
            bestManifest["_manifestPath"] = ToGenericPath(manifestPath);
            hasBest = true;
        }
    }

    if (hasBest) {
        crashRecoveryPendingManifest_ = bestManifest;
        crashRecoveryPending_ = true;
        crashRecoveryStatus_.clear();
    }
}

void DebugEditor::DrawCrashRecoveryPrompt() {
#ifdef USE_IMGUI
    // 復元の可否を先に表示し、ユーザーが選ぶまでは現在のJSONを変更しません。
    if (!crashRecoveryPending_) {
        return;
    }

    const char* popupName = "クラッシュ復元###CrashRecoveryPrompt";
    if (!crashRecoveryPromptOpened_) {
        ImGui::OpenPopup(popupName);
        crashRecoveryPromptOpened_ = true;
    }

    if (ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("前回のEditorが正常終了していません。未保存の自動ドラフトがあります。");
        ImGui::TextWrapped("復元すると、現在のJSONは退避してからドラフトを Resources/json に戻します。");
        ImGui::Separator();

        ImGui::Text("シーン: %s", crashRecoveryPendingManifest_.value("sceneFile", std::string("-")).c_str());
        ImGui::Text("保存時刻: %s", crashRecoveryPendingManifest_.value("updatedAt", std::string("-")).c_str());

        const auto& files = crashRecoveryPendingManifest_.value("files", nlohmann::json::array());
        if (ImGui::BeginTable("CrashRecoveryFiles", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("復元先");
            ImGui::TableSetupColumn("ドラフト");
            ImGui::TableHeadersRow();
            for (const auto& file : files) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", file.value("source", std::string("-")).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", file.value("draft", std::string("-")).c_str());
            }
            ImGui::EndTable();
        }

        if (!crashRecoveryStatus_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "%s", crashRecoveryStatus_.c_str());
        }

        if (ImGui::Button("復元する", ImVec2(120.0f, 0.0f))) {
            if (RestoreCrashRecoveryDraft()) {
                ResolveCrashRecoveryCandidate("restored");
                crashRecoveryPending_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("今回は使わない", ImVec2(140.0f, 0.0f))) {
            ResolveCrashRecoveryCandidate("dismissed");
            crashRecoveryPending_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#endif
}

bool DebugEditor::RestoreCrashRecoveryDraft() {
    // 復元前の現行JSONを退避し、検証済みのドラフトだけを元の場所へ戻します。
    if (!crashRecoveryPendingManifest_.is_object() ||
        !crashRecoveryPendingManifest_.contains("files") ||
        !crashRecoveryPendingManifest_["files"].is_array()) {
        crashRecoveryStatus_ = "復元データを読み込めません。";
        return false;
    }

    // Manifest に保存されたPathをそのまま信用せず、現在の作業Rootと許可範囲で検証します。
    const fs::path projectRoot = fs::current_path();
    const fs::path jsonRoot = projectRoot / "Resources/json";
    const fs::path recoveryRoot = projectRoot / fs::path(kCrashRecoveryRoot);
    const fs::path backupRoot = projectRoot / fs::path(kCrashRecoveryRoot) / ("pre_restore_" + MakeCrashRecoveryStamp());
    int restoredCount = 0;
    int errorCount = 0;

    for (const auto& file : crashRecoveryPendingManifest_["files"]) {
        const std::string sourceText = file.value("source", std::string(""));
        const std::string draftText = file.value("draft", std::string(""));
        if (sourceText.empty() || draftText.empty() || !IsCrashRecoverySourcePathAllowed(sourceText)) {
            ++errorCount;
            continue;
        }

        const std::string draftRootText = crashRecoveryPendingManifest_.value("draftRoot", std::string(""));
        const fs::path source = projectRoot / fs::path(sourceText);
        const fs::path draft = projectRoot / fs::path(draftText);
        const fs::path draftRoot = projectRoot / fs::path(draftRootText);
        if (draftRootText.empty() ||
            !IsPathInside(draftRoot, recoveryRoot) ||
            !IsPathInside(source, jsonRoot) ||
            !IsPathInside(draft, draftRoot) ||
            !fs::exists(draft)) {
            ++errorCount;
            continue;
        }

        // 上書きする現行JSONは先に別フォルダへ退避し、ファイルごとの失敗を数えます。
        std::error_code ec;
        if (fs::exists(source, ec)) {
            const fs::path currentBackup = backupRoot / fs::path(sourceText);
            fs::create_directories(currentBackup.parent_path(), ec);
            fs::copy_file(source, currentBackup, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                ++errorCount;
                continue;
            }
        }

        fs::create_directories(source.parent_path(), ec);
        if (ec) {
            ++errorCount;
            continue;
        }

        fs::copy_file(draft, source, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            ++errorCount;
            continue;
        }
        ++restoredCount;
    }

    // 一件以上戻せたときだけ成功とし、部分失敗の件数も通知へ含めます。
    if (restoredCount <= 0) {
        crashRecoveryStatus_ = "復元できるファイルがありませんでした。";
        return false;
    }

    crashRecoveryStatus_ = "クラッシュ復元: " + std::to_string(restoredCount) + "件を復元しました。";
    if (errorCount > 0) {
        crashRecoveryStatus_ += " " + std::to_string(errorCount) + "件は失敗しました。";
    }
    DebugConsole::GetInstance()->AddLog(crashRecoveryStatus_);
    TriggerSaveNotification(crashRecoveryStatus_);

    // 編集中Sceneが安定している場合は再読込し、復元したJSONを画面へ反映します。
    if (sceneManager_ && !sceneManager_->GetCurrentSceneName().empty() && !sceneManager_->IsTransitioning()) {
        sceneManager_->ReloadCurrentScene();
    }
    return true;
}

void DebugEditor::ResolveCrashRecoveryCandidate(const std::string& resolution) {
    // 選択結果を元セッションのマニフェストへ記録して、同じ確認を繰り返しません。
    if (!crashRecoveryPendingManifest_.is_object()) {
        return;
    }

    const std::string manifestPathText = crashRecoveryPendingManifest_.value("_manifestPath", std::string(""));
    if (manifestPathText.empty()) {
        return;
    }

    nlohmann::json manifest;
    if (!ReadJsonFileSafe(manifestPathText, manifest)) {
        manifest = crashRecoveryPendingManifest_;
        manifest.erase("_manifestPath");
    }
    manifest["resolved"] = true;
    manifest["resolution"] = resolution;
    manifest["resolvedAt"] = NowIsoString();
    WriteJsonFileSafe(manifestPathText, manifest);
}

