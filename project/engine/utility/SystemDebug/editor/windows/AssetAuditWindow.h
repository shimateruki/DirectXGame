#pragma once

#include "IEditable.h"
#include "json.hpp"

#include <cstdint>
#include <future>
#include <map>
#include <string>
#include <vector>

class DebugEditor;

/// Resources配下のアセットを監査し、重い素材・未使用候補・欠落参照をエディタ上で確認するウィンドウ。
class AssetAuditWindow : public IEditable {
public:
    /// DebugEditorとの連携に必要な参照を受け取り、監査ウィンドウの操作対象を準備する。
    void Initialize(DebugEditor* editor);
    /// 監査実行、結果表示、検索、プレビュー、削除確認までのUI全体を描画する。
    void DrawImGui() override;
    std::string GetName() override { return "アセット監査 (Asset Audit)"; }

private:
    /// 外部PowerShell監査ツールを非同期で起動し、結果JSONを生成させる。
    bool RunAuditTool();
    /// 実行中の監査タスクを監視し、完了したら最新レポートを読み込む。
    void UpdateAuditProcess();
    /// Resources/.cache/asset_audit の最新JSONを読み込み、画面表示用データへ反映する。
    bool LoadLatestReport();
    void DrawSummary();
    void DrawCategorySummary();
    void DrawHeavyAssets();
    /// JSON/コードから参照が見つからない削除候補を一覧表示する。
    void DrawUnusedAssets();
    void DrawMissingReferences();
    /// 完全削除前に対象と相方ファイルの削除を確認させるモーダルを描画する。
    void DrawDeleteConfirmPopup();
    void DrawAssetPreview(const nlohmann::json& item, float size);
    bool OpenExternalPath(const std::string& relativePath);
    uint32_t GetPreviewTextureHandle(const std::string& relativePath);
    bool PlayAudioPreview(const std::string& relativePath, bool isBgm);
    void CreateModelPreview(const std::string& relativePath);
    void RemoveModelPreviews();
    int CountModelPreviews() const;
    bool MatchesSearch(const nlohmann::json& item) const;
    /// 保護パスを避けながら、選択ファイルと同名DDSなどの相方ファイルをまとめて削除する。
    bool DeleteAssetFiles(const std::string& relativePath, std::vector<std::string>& deletedPaths, std::string& errorMessage);
    void RemoveDeletedAssetsFromReport(const std::vector<std::string>& deletedPaths);

private:
    DebugEditor* editor_ = nullptr;
    nlohmann::json latestReport_;
    bool hasReport_ = false;
    char searchBuffer_[256] = "";
    std::string pendingDeletePath_;
    bool pendingDeletePopupRequested_ = false;
    std::string lastStatus_ = "必要なタイミングで監査ツールを実行、または前回レポートを読み込んでください。";
    bool showPreviewThumbnails_ = false;
    bool auditRunning_ = false;
    int categoryFilter_ = 0;
    int maxRowsToDraw_ = 200;
    std::future<std::uint32_t> auditFuture_;
    std::map<std::string, uint32_t> previewTextureHandles_;
    std::map<std::string, uint32_t> previewAudioHandles_;
};
