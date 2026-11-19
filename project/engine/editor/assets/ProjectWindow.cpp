#include "engine/editor/assets/JsonAssetBrowser.h"
#define NOMINMAX
#include "ProjectWindow.h"
#include "engine/editor/preview/AssetThumbnailRenderer.h"
#include "PresetEditor.h"
#include "TextureManager.h"
#include <limits>
#include <cfloat>
#include <functional>
#include "DebugEditor.h"
#include "imgui.h"
#include "engine/editor/assets/preset/PresetManager.h"
#include "DebugConsole.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3D.h"
#include "IconsFontAwesome5.h"
#include "AssetDatabase.h"
#include "EditorAssetDragPayload.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>


namespace fs = std::filesystem;

namespace {
std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool IsModelAssetFile(const fs::path& path) {
    const std::string ext = ToLowerAscii(path.extension().string());
    return ext == ".obj" || ext == ".gltf" || ext == ".glb";
}

} // namespace

ProjectWindow::ProjectWindow()
    : thumbnailRenderer_(std::make_unique<AssetThumbnailRenderer>()) {}
ProjectWindow::~ProjectWindow() = default;

void ProjectWindow::Initialize(DebugEditor* editor, engine::DirectXCommon* dxCommon) {
    editor_ = editor;
    thumbnailRenderer_->Initialize(dxCommon);
}

void ProjectWindow::CapturePendingThumbnails() {
    thumbnailRenderer_->CapturePendingThumbnails();
}

void ProjectWindow::ExportCapturedHudPortraits() {
    thumbnailRenderer_->ExportCapturedHudPortraits();
}

uint64_t ProjectWindow::GetPresetThumbnailGpuPtr(const std::string& presetName) {
    return thumbnailRenderer_->GetPresetThumbnailGpuPtr(presetName);
}

uint64_t ProjectWindow::GetPresetPreviewGpuPtr(const std::string& presetName, float yaw, float pitch, float zoom) {
    return thumbnailRenderer_->GetPresetPreviewGpuPtr(presetName, yaw, pitch, zoom);
}

void ProjectWindow::InvalidatePresetThumbnail(const std::string& presetName) {
    thumbnailRenderer_->InvalidatePresetThumbnail(presetName);
}

uint64_t ProjectWindow::GetPbrThumbnailGpuPtr(const std::string& name,
    const std::string& albedoPath, const std::string& normalPath, const std::string& ormPath) {
    return thumbnailRenderer_->GetPbrThumbnailGpuPtr(name, albedoPath, normalPath, ormPath);
}

void ProjectWindow::InvalidatePbrThumbnails() {
    thumbnailRenderer_->InvalidatePbrThumbnails();
}

void ProjectWindow::Draw() {
#ifdef USE_IMGUI
    AssetDatabase* assetDatabase = AssetDatabase::GetInstance();

    // Preset、Model、Prefab、VFXを一つのProjectウィンドウのタブで切り替える。
    ImGui::Begin("Project (Assets)");

    if (!ImGui::BeginTabBar("ProjectAssetTabs")) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabItem("プリセット")) {
        PresetEditor::GetInstance()->DrawLibrary(true);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("モデル")) {
        ImGui::SeparatorText("Asset Database");

    // 初回索引中は進捗、完了後はAsset数・再解析数・エラー数を示す。
    const AssetDatabaseRefreshResult& databaseStatus = assetDatabase->GetLastRefreshResult();

    if (assetDatabase->IsInitialIndexBuildInProgress()) {
        if (assetDatabase->IsInitialDirectoryScanInProgress()) {
            ImGui::Text(
                "Asset一覧を確認中: %zu件検出",
                assetDatabase->GetInitialDiscoveredAssetCount());
            // 探索中は全件数が未確定なので、検出件数と循環するバーで進行中と示す。
            const float scanAnimation = static_cast<float>(std::fmod(ImGui::GetTime() * 0.5, 1.0));
            ImGui::ProgressBar(scanAnimation, ImVec2(-1.0f, 0.0f));
        }
        else {
            const std::size_t progress = assetDatabase->GetInitialIndexProgress();
            const std::size_t total = assetDatabase->GetInitialIndexTotal();
            const float fraction = total > 0
                ? static_cast<float>(progress) / static_cast<float>(total)
                : 1.0f;
            ImGui::Text(
                "差分索引を確認中: %zu / %zu  (Cache %zu / 再解析 %zu)",
                progress,
                total,
                assetDatabase->GetInitialReusedAssetCount(),
                assetDatabase->GetInitialReparsedAssetCount());
            ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f));
        }
    }
    else {
        ImGui::Text(
            "%zu Assets / Cache %zu / 再解析 %zu / Meta作成 %zu / Error %zu",
            databaseStatus.assetCount,
            databaseStatus.reusedAssetCount,
            databaseStatus.reparsedAssetCount,
            databaseStatus.createdMetaCount,
            databaseStatus.errorCount);
    }

    // Asset索引とDDS変換が重なる間は、手動更新と追加の変換要求を無効にする。
    const DdsCacheBuildState ddsState = assetDatabase->GetDdsCacheBuildState();
    const bool ddsProcessBusy = ddsState == DdsCacheBuildState::Queued ||
        ddsState == DdsCacheBuildState::Running;
    ImGui::BeginDisabled(assetDatabase->IsInitialIndexBuildInProgress() || ddsProcessBusy);

    if (ImGui::Button(ICON_FA_SYNC " Asset Databaseを更新")) {
        assetDatabase->RequestRefresh(true);
    }

    ImGui::EndDisabled();

    if (ddsState == DdsCacheBuildState::Running || ddsState == DdsCacheBuildState::Queued) {
        ImGui::TextUnformatted(assetDatabase->GetDdsCacheBuildMessage().c_str());
        const float animation = static_cast<float>(std::fmod(ImGui::GetTime() * 0.35, 1.0));
        ImGui::ProgressBar(animation, ImVec2(-1.0f, 0.0f), "DDS Cache");
    }
    else if (ddsState == DdsCacheBuildState::Succeeded) {
        ImGui::TextColored(
            ImVec4(0.45f, 0.9f, 0.55f, 1.0f),
            "%s",
            assetDatabase->GetDdsCacheBuildMessage().c_str());
    }
    else if (ddsState == DdsCacheBuildState::Failed) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
            "%s",
            assetDatabase->GetDdsCacheBuildMessage().c_str());
    }

    const bool ddsBusy = ddsProcessBusy ||
        assetDatabase->IsInitialIndexBuildInProgress();
    ImGui::BeginDisabled(ddsBusy);

    if (ImGui::Button(ICON_FA_IMAGE " 未変換DDSを生成")) {
        assetDatabase->RequestDdsCacheBuild();
    }

    ImGui::EndDisabled();

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("未変換または元画像より古いDDSだけを非同期で生成します。");
    }

    if (!assetDatabase->GetIssues().empty() && ImGui::TreeNode("Asset Database Issues")) {
        for (const AssetDatabaseIssue& issue : assetDatabase->GetIssues()) {
            const ImVec4 color = issue.severity == AssetDatabaseIssueSeverity::Error
                ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                : ImVec4(1.0f, 0.78f, 0.25f, 1.0f);
            ImGui::TextColored(color, "%s", issue.path.c_str());
            ImGui::TextWrapped("  %s", issue.message.c_str());
        }

        ImGui::TreePop();
    }

    // Modelの実ファイルとディレクトリを、サムネイル付きの格子へ並べる。
    if (ImGui::CollapsingHeader("Models (Source)", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string baseDirectory = "Resources/3DModel/";

        if (currentModelDirectory_.empty() || currentModelDirectory_.find(baseDirectory) != 0) {
            currentModelDirectory_ = baseDirectory;
        }

        if (currentModelDirectory_ != baseDirectory) {
            if (ImGui::Button(ICON_FA_ARROW_UP " Back")) {
                fs::path p(currentModelDirectory_);
                currentModelDirectory_ = p.parent_path().parent_path().generic_string() + "/";
                if (currentModelDirectory_.length() < baseDirectory.length() ||
                    currentModelDirectory_.find(baseDirectory) == std::string::npos) {
                    currentModelDirectory_ = baseDirectory;
                }
            }

            ImGui::SameLine();
        }

        ImGui::TextDisabled("%s", currentModelDirectory_.c_str());
        ImGui::Separator();

        // ディレクトリ内のAssetだけを表示し、下位フォルダーは個別の入口として描く。
        const std::vector<std::string> modelDirectories = assetDatabase->GetSubdirectories(currentModelDirectory_);
        const std::vector<const EditorAssetRecord*> directoryAssets =
            assetDatabase->GetAssetsInDirectory(currentModelDirectory_, false);
        ImGui::TextDisabled("Drag & Drop to Scene to Place");
        ImGui::Separator();

        const float thumbnailSize = 64.0f;
        const float padding = 16.0f;
        const float cellSize = thumbnailSize + padding;
        const float panelWidth = ImGui::GetContentRegionAvail().x;
        const int columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));

        if (ImGui::BeginTable("ModelAssetTable", columnCount)) {
            for (const std::string& directoryPath : modelDirectories) {
                const fs::path path(directoryPath);
                const std::string folderName = path.filename().string();
                std::string payloadName = fs::relative(path, baseDirectory).generic_string();
                std::replace(payloadName.begin(), payloadName.end(), '\\', '/');
                const std::vector<const EditorAssetRecord*> childAssets =
                    assetDatabase->GetAssetsInDirectory(directoryPath, false);
                const auto modelAssetIterator = std::find_if(
                    childAssets.begin(),
                    childAssets.end(),
                    [](const EditorAssetRecord* asset) {
                        return asset && asset->type == EditorAssetType::Model;
                    });

                // 直下にModelを含むフォルダーは、フォルダー自体を配置可能なModelとして扱う。
                const bool isModelFolder = modelAssetIterator != childAssets.end();

                ImGui::TableNextColumn();

                if (isModelFolder) {
                    const uint64_t modelThumbnail = thumbnailRenderer_->GetModelThumbnailGpuPtr(payloadName);

                    ImGui::PushID(directoryPath.c_str());
                    ImGui::BeginGroup();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::ImageButton(
                        payloadName.c_str(),
                        static_cast<ImTextureID>(modelThumbnail),
                        ImVec2(thumbnailSize, thumbnailSize));
                    ImGui::PopStyleColor();

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        const EditorAssetDragPayload dragPayload = MakeEditorAssetDragPayload(
                            (*modelAssetIterator)->guid,
                            payloadName);
                        ImGui::SetDragDropPayload("MODEL_ASSET", &dragPayload, sizeof(dragPayload));
                        ImGui::Image(
                            static_cast<ImTextureID>(modelThumbnail),
                            ImVec2(32.0f, 32.0f));
                        ImGui::SameLine();
                        ImGui::Text("Place: %s", folderName.c_str());

                        if (!(*modelAssetIterator)->guid.empty()) {
                            ImGui::TextDisabled("GUID: %s", (*modelAssetIterator)->guid.c_str());
                        }

                        ImGui::EndDragDropSource();
                    }

                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                    ImGui::TextWrapped("%s", folderName.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndGroup();
                    ImGui::PopID();
                }
                else {
                    ImGui::PushID(directoryPath.c_str());
                    ImGui::BeginGroup();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.2f, 1.0f));
                    ImGui::SetWindowFontScale(3.0f);

                    if (ImGui::Button(ICON_FA_FOLDER, ImVec2(thumbnailSize, thumbnailSize))) {
                        currentModelDirectory_ = directoryPath + "/";
                    }

                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::PopStyleColor(4);
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                    ImGui::TextWrapped("%s", folderName.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndGroup();
                    ImGui::PopID();
                }
            }

            // 個別ModelはGUIDをDrag Payloadへ載せ、Sceneへ配置するときの参照を保つ。
            for (const EditorAssetRecord* asset : directoryAssets) {
                if (!asset || asset->type != EditorAssetType::Model) {
                    continue;
                }

                const fs::path sourcePath(asset->sourcePath);
                const std::string fileName = sourcePath.filename().string();
                std::string payloadName = fs::relative(sourcePath, baseDirectory).generic_string();
                std::replace(payloadName.begin(), payloadName.end(), '\\', '/');

                const uint64_t modelThumbnail = thumbnailRenderer_->GetModelThumbnailGpuPtr(payloadName);

                ImGui::TableNextColumn();
                ImGui::PushID(asset->guid.empty() ? asset->sourcePath.c_str() : asset->guid.c_str());
                ImGui::BeginGroup();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));

                ImGui::ImageButton(
                    payloadName.c_str(),
                    static_cast<ImTextureID>(modelThumbnail),
                    ImVec2(thumbnailSize, thumbnailSize));
                ImGui::PopStyleColor(3);

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    const EditorAssetDragPayload dragPayload = MakeEditorAssetDragPayload(asset->guid, payloadName);
                    ImGui::SetDragDropPayload("MODEL_ASSET", &dragPayload, sizeof(dragPayload));
                    ImGui::Image(
                        static_cast<ImTextureID>(modelThumbnail),
                        ImVec2(32.0f, 32.0f));
                    ImGui::SameLine();
                    ImGui::Text("Place: %s", fileName.c_str());

                    if (!asset->guid.empty()) {
                        ImGui::TextDisabled("GUID: %s", asset->guid.c_str());
                    }

                    ImGui::EndDragDropSource();
                }

                if (ImGui::IsItemHovered() && !asset->guid.empty()) {
                    ImGui::SetTooltip(
                        "%s\nGUID: %s\nImporter: %s",
                        asset->sourcePath.c_str(),
                        asset->guid.c_str(),
                        asset->importer.c_str());
                }

                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                ImGui::TextWrapped("%s", fileName.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }

    ImGui::EndTabItem();
    }

    // Presetの作成・上書き・名前変更など、内容を変更する操作は管理タブに置く。
    if (ImGui::BeginTabItem("管理")) {
    bool openDeleteConfirmation = false;

    if (ImGui::CollapsingHeader("Presets (Configured)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), ICON_FA_PLUS_SQUARE " Create New Preset");
        ImGui::TextDisabled(" (Use '/' in name for folders, e.g. 'Enemy/Slime')");

        if (editor_->GetSelectedObject()) {
            static char presetNameBuf[64] = "NewPreset";
            ImGui::PushItemWidth(200);
            ImGui::InputText("##PresetName", presetNameBuf, 64);
            ImGui::PopItemWidth();

            ImGui::SameLine();

            if (ImGui::Button(ICON_FA_SAVE " Save Selection")) {
                if (strlen(presetNameBuf) > 0) {
                    std::string pName = presetNameBuf;
                    PresetManager::GetInstance()->AddPresetFromObject(pName, editor_->GetSelectedObject());
                    InvalidatePresetThumbnail(pName);
                    DebugConsole::GetInstance()->AddLog("Saved Preset: " + pName);
                }
            }
        }
        else {
            ImGui::TextDisabled("(Select an object in Scene to save)");
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("HUDアイコンを再生成")) {
            thumbnailRenderer_->QueueHudPortraitExports(true);
            DebugConsole::GetInstance()->AddLog("HUD icon regeneration queued.");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("実際の敵プリセットから通常・被弾PNGを生成します");

        ImGui::Separator();
        ImGui::Spacing();

        const auto& presets = PresetManager::GetInstance()->GetPresets();

        if (presets.empty()) {
            ImGui::TextDisabled("(No Presets Saved)");
        }
        else {
            // 名前の先頭のスラッシュ区切りを仮想フォルダーとして表示する。
            std::map<std::string, std::vector<std::string>> folders;
            std::vector<std::string> rootPresets;

            for (const auto& [name, data] : presets) {
                size_t slashPos = name.find('/');

                if (slashPos != std::string::npos) {
                    folders[name.substr(0, slashPos)].push_back(name);
                }
                else {
                    rootPresets.push_back(name);
                }
            }

            // どのフォルダーでも同じサムネイルとコンテキスト操作を使う。
            auto drawPresetGrid = [&](const std::vector<std::string>& list, const char* tableId) {
                float thumbnailSize = 80.0f;
                float padding = 16.0f;
                float cellSize = thumbnailSize + padding;
                float panelWidth = ImGui::GetContentRegionAvail().x;
                int cols = std::max(1, (int)(panelWidth / cellSize));

                if (ImGui::BeginTable(tableId, cols)) {
                    for (const std::string& name : list) {
                        ImGui::TableNextColumn();
                        // スクロールで見えている項目だけ撮影を要求し、一覧全体のサムネイルを一度に生成しない。
                        const uint64_t thumbnail = ImGui::IsRectVisible(ImVec2(thumbnailSize, thumbnailSize)) ? GetPresetThumbnailGpuPtr(name) : 0;



                        ImGui::PushID(name.c_str());
                        ImGui::BeginGroup();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.15f, 1.0f));
                        const bool selected = thumbnail
                            ? ImGui::ImageButton(name.c_str(), (ImTextureID)(uintptr_t)thumbnail, ImVec2(thumbnailSize, thumbnailSize))
                            : ImGui::Button(name.c_str(), ImVec2(thumbnailSize, thumbnailSize));
                        if (selected)
                            PresetEditor::GetInstance()->SelectPreset(name);
                        ImGui::PopStyleColor();

                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                            ImGui::SetDragDropPayload("PRESET_ASSET", name.c_str(), name.size() + 1);
                            ImGui::Image((ImTextureID)(uintptr_t)thumbnail, ImVec2(32.0f, 32.0f));
                            ImGui::SameLine();
                            ImGui::Text("Preset: %s", name.c_str());
                            ImGui::EndDragDropSource();
                        }

                        size_t lastSlash = name.find_last_of('/');
                        std::string shortName = PresetEditor::GetInstance()->GetDisplayName(name, presets.at(name));
                        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                        ImGui::TextWrapped("%s", shortName.c_str());
                        ImGui::PopTextWrapPos();

                        bool openRename = false; // リネーム画面を開くフラグ

                        // 右クリックから上書き、Prefab化、サムネイル再生成、削除を行う。
                        if (ImGui::BeginPopupContextItem("PresetContextMenu")) {
                            if (ImGui::MenuItem(ICON_FA_EDIT " Rename")) {
                                openRename = true; // 直接開かずフラグを立てる
                            }

                            if (ImGui::MenuItem(ICON_FA_SAVE " Overwrite")) {
                                if (editor_->GetSelectedObject()) {
                                    PresetManager::GetInstance()->AddPresetFromObject(name, editor_->GetSelectedObject());
                                    InvalidatePresetThumbnail(name);
                                }
                            }

                            if (ImGui::MenuItem(ICON_FA_CODE_BRANCH " Prefabへ変換")) {
                                std::string prefabName = name;
                                int suffix = 1;

                                // 変換元と同名の Prefab がある場合は連番で空きを探し、既存の定義を上書きしない。
                                while (PresetManager::GetInstance()->HasPrefab(prefabName)) {
                                    prefabName = name + "_Prefab" + std::to_string(suffix++);
                                }

                                if (PresetManager::GetInstance()->CreatePrefabFromPreset(name, prefabName)) {
                                    DebugConsole::GetInstance()->AddLog(
                                        "Converted Preset to Prefab: " + name + " -> " + prefabName);
                                }
                            }

                            if (ImGui::MenuItem(ICON_FA_CAMERA " Update Thumbnail"))
                                InvalidatePresetThumbnail(name);

                            ImGui::Separator();

                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.27f, 0.30f, 1.0f));

                            if (ImGui::MenuItem(ICON_FA_TRASH_ALT " 削除...")) {
                                pendingDeletePresetName_ = name;
                                openDeleteConfirmation = true;
                            }

                            ImGui::PopStyleColor();
                            ImGui::EndPopup();
                        }

                        // ImGuiのPopupはメニューを閉じた後に開くため、改名要求だけ保持する。
                        if (openRename) {
                            ImGui::OpenPopup("RenamePresetPopup");
                        }

                        // 名前変更後はサムネイルのキャッシュキーも新名称へ移す。
                        if (ImGui::BeginPopup("RenamePresetPopup")) {
                            static char renameBuf[64] = "";

                            // 開いた瞬間だけ文字列をコピーして初期化する
                            if (openRename) {
                                strncpy_s(renameBuf, name.c_str(), sizeof(renameBuf));
                            }

                            ImGui::Text("New Name:");
                            bool isEnter = ImGui::InputText("##NewName", renameBuf, sizeof(renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);

                            bool isOk = ImGui::Button("OK");
                            ImGui::SameLine();
                            bool isCancel = ImGui::Button("Cancel");

                            // エンターかOKが押されたらリネーム実行
                            if (isEnter || isOk) {
                                std::string newName = renameBuf;

                                if (!newName.empty() && newName != name) {
                                    auto* presets = PresetManager::GetInstance();
                                    presets->RenamePreset(name, newName);
                                    if (!presets->HasPreset(name)) {
                                        thumbnailRenderer_->RenamePresetThumbnail(name, newName);
                                    }
                                }

                                ImGui::CloseCurrentPopup();
                            }
                            else if (isCancel) {
                                ImGui::CloseCurrentPopup();
                            }

                            ImGui::EndPopup();
                        }

                        ImGui::EndGroup();
                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }
                };

            // フォルダーごとに同じ描画ラムダを呼び、ルート直下だけ最後へ置く。
            for (const auto& [folderName, list] : folders) {
                if (ImGui::TreeNodeEx(folderName.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    drawPresetGrid(list, folderName.c_str());
                    ImGui::TreePop();
                }
            }

            // ルート直下の表示
            if (!rootPresets.empty()) {
                if (folders.empty())
                    drawPresetGrid(rootPresets, "RootPresetsTable");
                else if (ImGui::TreeNodeEx("Others", ImGuiTreeNodeFlags_DefaultOpen)) {
                    drawPresetGrid(rootPresets, "RootPresetsTable");
                    ImGui::TreePop();
                }
            }
        }
    }

    // 一覧のサムネイル描画中は登録を削除せず、一覧の参照を使い終えてから確認する。
    if (openDeleteConfirmation)
        ImGui::OpenPopup("プリセットの削除を確認");
    if (ImGui::BeginPopupModal("プリセットの削除を確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("「%s」を登録一覧から削除します。", pendingDeletePresetName_.c_str());
        ImGui::TextDisabled("シーン上のObjectやモデルファイルは削除されません。");

        if (ImGui::Button("削除する")) {
            PresetManager::GetInstance()->RemovePreset(pendingDeletePresetName_);

            // 撮影済みのGPU資源はここで破棄せず、キャッシュの再利用時に解放する。
            pendingDeletePresetName_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("キャンセル")) {
            pendingDeletePresetName_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // VFXのAsset一覧は別タブへ置き、Preset管理の操作と混ぜない。
        ImGui::Separator();
    ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Prefab")) {
        if (ImGui::CollapsingHeader("Prefabs (v3 / Variants)", ImGuiTreeNodeFlags_DefaultOpen)) {
            static char prefabNameBuf[64] = "NewPrefab";
            ImGui::InputText("Prefab名", prefabNameBuf, IM_ARRAYSIZE(prefabNameBuf));

            engine::Object3D* selectedObject = editor_ ? editor_->GetSelectedObject() : nullptr;

            if (!selectedObject) {
                ImGui::TextDisabled("保存するObjectをHierarchyまたはGame Viewで選択してください。");
            }

            if (ImGui::Button("選択ObjectをPrefab保存") && selectedObject && prefabNameBuf[0] != '\0') {
                PresetManager::GetInstance()->AddPrefabFromObject(prefabNameBuf, selectedObject);
            }

            ImGui::TextDisabled("Scene上のInstanceは元Prefabとのリンクを保持し、InspectorからApply/Revertできます。");
            ImGui::TextDisabled("旧Prefab v1/v2は読み込み時にv3へ移行されます。Presetは右クリックからPrefabへ明示変換できます。");

            const auto& prefabs = PresetManager::GetInstance()->GetPrefabs();

            if (prefabs.empty()) {
                ImGui::TextDisabled("Prefabはまだありません。");
            }

            int prefabIndex = 0;

            for (const auto& [prefabName, prefabJson] : prefabs) {
                const bool isVariant = PresetManager::GetInstance()->IsPrefabVariant(prefabName);
                const std::string basePrefabName = PresetManager::GetInstance()->GetPrefabBaseName(prefabName);
                const auto structureSummary =
                    PresetManager::GetInstance()->GetPrefabStructureOverrideSummary(prefabName);
                ImGui::PushID(prefabName.c_str());
                ImGui::BeginGroup();
                ImGui::Button(isVariant ? "Variant" : "Prefab", ImVec2(86.0f, 46.0f));

                // ダブルクリックは定義の編集へ移る。シーンにインスタンスを置く操作はドラッグで受け付ける。
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && editor_) {
                    editor_->BeginPrefabEditSession(prefabName);
                }

                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("PREFAB_ASSET", prefabName.c_str(), prefabName.size() + 1);
                    ImGui::Text("Prefab: %s", prefabName.c_str());
                    ImGui::EndDragDropSource();
                }

                ImGui::TextWrapped("%s", prefabName.c_str());

                if (isVariant) {
                    ImGui::TextDisabled("Base: %s", basePrefabName.empty() ? "Missing" : basePrefabName.c_str());

                    if (structureSummary.HasOverrides()) {
                        ImGui::TextDisabled("Structure: +%d -%d Move:%d",
                            structureSummary.addedObjects,
                            structureSummary.removedObjects,
                            structureSummary.reparentedObjects);
                    }
                }

                if (ImGui::BeginPopupContextItem("PrefabContextMenu")) {
                    if (ImGui::MenuItem(ICON_FA_CUBE " Prefab Modeで開く") && editor_) {
                        editor_->BeginPrefabEditSession(prefabName);
                    }

                    ImGui::Separator();

                    // 同じ基底から複数の派生を作れるよう、既存のPrefab名と衝突しない名前を探す。
                    if (ImGui::MenuItem(ICON_FA_CODE_BRANCH " Variantを作成")) {
                        std::string variantName = prefabName + "_Variant";
                        int suffix = 1;

                        while (PresetManager::GetInstance()->HasPrefab(variantName)) {
                            variantName = prefabName + "_Variant" + std::to_string(suffix++);
                        }

                        if (PresetManager::GetInstance()->CreatePrefabVariant(prefabName, variantName)) {
                            DebugConsole::GetInstance()->AddLog(
                                "Created Prefab Variant: " + variantName + " from " + prefabName);
                        }
                    }

                    ImGui::TextDisabled("Asset ID: %s", prefabJson.value("assetId", "").c_str());
                    ImGui::EndPopup();
                }

                ImGui::EndGroup();
                ImGui::PopID();

                ++prefabIndex;

                if (prefabIndex % 4 != 0) {
                    ImGui::SameLine();
                }
            }
        }

    ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("VFX")) {
if (ImGui::CollapsingHeader("VFX / Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string particleDir = "Resources/json/gpu_particles/";

        if (fs::exists(particleDir)) {
            float thumbnailSize = 64.0f;
            float padding = 16.0f;
            float cellSize = thumbnailSize + padding;
            float panelWidth = ImGui::GetContentRegionAvail().x;
            int columnCount = std::max(1, (int)(panelWidth / cellSize));

            if (ImGui::BeginTable("ParticleAssetTable", columnCount)) {
                for (const auto& presetName : JsonAssetBrowser::ListKeys("Resources/json/gpu_particles")) {
                        ImGui::TableNextColumn();

                        ImGui::PushID(("Particle_" + presetName).c_str());
                        ImGui::BeginGroup();

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.1f, 0.1f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.15f, 0.15f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.2f, 0.2f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));

                        ImGui::SetWindowFontScale(2.5f);
                        ImGui::Button(ICON_FA_FIRE, ImVec2(thumbnailSize, thumbnailSize));
                        ImGui::SetWindowFontScale(1.0f);
                        ImGui::PopStyleColor(4);

                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                            // 拡張子なしのルート相対名を渡し、置いた先が粒子設定を解決する。
                            ImGui::SetDragDropPayload("PARTICLE_ASSET", presetName.c_str(), presetName.size() + 1);
                            ImGui::Text("Place Particle: %s", presetName.c_str());
                            ImGui::EndDragDropSource();
                        }

                        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                        ImGui::TextWrapped("%s", presetName.c_str());
                        ImGui::PopTextWrapPos();

                        ImGui::EndGroup();
                        ImGui::PopID();
                }

                ImGui::EndTable();
            }
        }
    }

    ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
    ImGui::End();
#endif
}
