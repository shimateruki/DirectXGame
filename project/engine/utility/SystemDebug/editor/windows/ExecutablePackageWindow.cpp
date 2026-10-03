#define NOMINMAX
#include "ExecutablePackageWindow.h"
#include "ExecutablePackageWindow.Internal.h"

#include "IconsFontAwesome5.h"
#include "imgui.h"
#include "json.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using ExecutablePackageInternal::kConfigurations;
using ExecutablePackageInternal::kTextureModeLabels;
using ExecutablePackageInternal::TexturePackageMode;

void ExecutablePackageWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
}

bool ExecutablePackageWindow::IsTaskRunning() {
    return task_.valid() && task_.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

void ExecutablePackageWindow::PollTask() {
    if (!task_.valid()) {
        return;
    }
    if (task_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    // ready を確認してから get し、描画スレッドを待機させません。
    PackageResult result = task_.get();
    statusText_ = result.success ? (ICON_FA_CHECK_CIRCLE " " + result.message) : (ICON_FA_EXCLAMATION_TRIANGLE " " + result.message);
}

void ExecutablePackageWindow::StartPackageTask(bool buildBeforePackage) {
    if (IsTaskRunning()) {
        return;
    }

    const int safeIndex = std::clamp(configurationIndex_, 0, static_cast<int>(kConfigurations.size()) - 1);
    PackageRequest request;
    request.packageName = packageNameBuffer_;
    request.configuration = kConfigurations[safeIndex];
    request.buildBeforePackage = buildBeforePackage;
    request.textureMode = std::clamp(textureModeIndex_, 0, static_cast<int>(kTextureModeLabels.size()) - 1);
    request.createZip = createZip_;
    request.includeProject = includeProject_;
    request.includeReadMe = includeProject_ && includeReadMe_;
    request.excludeUnusedResources = excludeUnusedResources_;

    const char* packageKind = includeProject_ ? "提出パッケージ" : "実行ファイルセット";
    statusText_ = buildBeforePackage
        ? std::string("ビルドしてから") + packageKind + "を作成しています..."
        : std::string("既存のビルド出力から") + packageKind + "を作成しています...";
    // ウィンドウの可変メンバーをワーカーから読まず、確定済み条件を値で渡します。
    task_ = std::async(std::launch::async, [request]() {
        return RunPackageTask(request);
    });
}

void ExecutablePackageWindow::DrawImGui() {
#ifdef USE_IMGUI
    PollTask();

    ImGui::TextColored(ImVec4(0.45f, 0.95f, 1.0f, 1.0f), ICON_FA_BOX_OPEN " パッケージ作成");
    ImGui::TextWrapped("実行ファイルセットだけ、または提出要件に合わせた完全パッケージをプロジェクト外へ作成します。元のファイルは変更しません。");
    ImGui::Separator();

    ImGui::InputTextWithHint("出力名", "例: LE3A_99_カマタ_タロウ", packageNameBuffer_, sizeof(packageNameBuffer_));
    ImGui::Combo("構成", &configurationIndex_, kConfigurations.data(), static_cast<int>(kConfigurations.size()));
    ImGui::Combo("テクスチャ構成", &textureModeIndex_, kTextureModeLabels.data(), static_cast<int>(kTextureModeLabels.size()));
    if (textureModeIndex_ == static_cast<int>(TexturePackageMode::CompactPng)) {
        ImGui::TextDisabled("同名DDS/PNGペアはPNGだけを残します。DDSしかないSkybox等は保持します。");
    } else if (textureModeIndex_ == static_cast<int>(TexturePackageMode::FastDds)) {
        ImGui::TextDisabled("同名DDS/PNGペアはDDSだけを残し、起動時の画像デコード負荷を抑えます。");
    } else {
        ImGui::TextDisabled("DDSとPNGを両方コピーします。容量は最大ですが、素材確認用に安全です。");
    }
    ImGui::Checkbox("Asset Auditの未使用候補を実行ファイルセットから除外", &excludeUnusedResources_);
    if (excludeUnusedResources_) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.78f, 0.28f, 1.0f),
            "作成直前に監査します。動的なパスは判定できない場合があるため、完成後に全シーンを確認してください。");
    }
    ImGui::Checkbox("project一式も含める（提出形式）", &includeProject_);
    if (includeProject_) {
        ImGui::Indent();
        ImGui::Checkbox("プロジェクト外のReadMe.mdを同梱", &includeReadMe_);
        ImGui::TextDisabled("参照元: projectと同じ階層のReadMe.md（このツールでは内容を生成・変更しません）");
        ImGui::Unindent();
    }
    ImGui::Checkbox("提出用ZIPも作成", &createZip_);

    // プレビューと実作成で同じ検証・パス変換を通し、表示先の食い違いを防ぎます。
    const std::string sanitizedName = ExecutablePackageInternal::PreviewPackageName(packageNameBuffer_);
    const fs::path detectedProjectRoot = ExecutablePackageInternal::PreviewProjectRoot();
    const fs::path previewRoot = detectedProjectRoot.empty() ? fs::path{} : detectedProjectRoot.parent_path();
    if (!previewRoot.empty()) {
        const fs::path outputFolder = includeProject_ ? fs::path("SubmissionPackages") : fs::path("ExecutableSets");
        ImGui::TextWrapped("出力先: %s", ExecutablePackageInternal::PreviewPathToUtf8(previewRoot / outputFolder / ExecutablePackageInternal::PreviewPathFromUtf8(sanitizedName)).c_str());
        if (createZip_) {
            ImGui::TextWrapped("ZIP: %s", ExecutablePackageInternal::PreviewPathToUtf8(previewRoot / outputFolder / ExecutablePackageInternal::PreviewPathFromUtf8(sanitizedName + ".zip")).c_str());
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "プロジェクトルートを検出できません。");
    }

    if (sanitizedName != packageNameBuffer_) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "実際の出力名: %s", sanitizedName.c_str());
    }

    const bool running = IsTaskRunning();
    ImGui::BeginDisabled(running);
    if (ImGui::Button(ICON_FA_ROCKET " ビルドして作成", ImVec2(180.0f, 0.0f))) {
        StartPackageTask(true);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " 既存出力から作成", ImVec2(180.0f, 0.0f))) {
        StartPackageTask(false);
    }
    ImGui::EndDisabled();

    if (running) {
        ImGui::SameLine();
        ImGui::TextDisabled("処理中...");
    }

    ImGui::Separator();
    ImGui::TextWrapped("%s", statusText_.c_str());
    if (includeProject_) {
        ImGui::BulletText("提出構成: 実行ファイルセット / project / ReadMe.md");
        ImGui::BulletText("project除外: .vs / .git / .agents / .codex / キャッシュ / バックアップ / 一時ファイル");
    } else {
        ImGui::BulletText("コピー対象: exe / dll / Runtime Resources / package_manifest.json");
        ImGui::BulletText("除外対象: .cache / .backup / .trash / tools / generated/editor");
    }
    if (excludeUnusedResources_) {
        ImGui::BulletText("追加除外: Asset Auditが未使用候補と判定したRuntime Resources");
    }
    ImGui::BulletText("DDS/PNGは同一フォルダ・同一名のペアだけを安全に整理します。");
    ImGui::BulletText("作成失敗時は既存の完成済みSetを残し、一時フォルダだけを削除します。");
#endif
}
