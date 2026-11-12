#pragma once

#include "IEditable.h"
#include "json.hpp"

#include <cstdint>
#include <future>
#include <map>
#include <string>
#include <vector>

class DebugEditor;

// 担当：アセット監査結果から容量・未使用候補・欠損を表示し、整理を支援する。
// 関連：AssetDatabase（資産情報）、Model / TextureManager（プレビュー）、DebugEditor
// 主な入口：Initialize / DrawImGui
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

    // 監査結果全体の件数・容量を表示する。
    void DrawSummary();

    // Asset種別ごとの件数・容量を表示する。
    void DrawCategorySummary();

    // 容量の大きいAssetの一覧を描画する。
    void DrawHeavyAssets();

    /// JSON/コードから参照が見つからない削除候補を一覧表示する。
    void DrawUnusedAssets();

    // 欠損参照の一覧と確認用UIを描画する。
    void DrawMissingReferences();

    /// 完全削除前に対象と相方ファイルの削除を確認させるモーダルを描画する。
    void DrawDeleteConfirmPopup();

    // 指定Assetの種類に応じたプレビューを表示する。
    void DrawAssetPreview(const nlohmann::json& item, float size);

    // 外部アプリへ渡す前に実在を確認し、失敗理由を UI へ残す。
    bool OpenExternalPath(const std::string& relativePath);

    uint32_t GetPreviewTextureHandle(const std::string& relativePath);

    // 試聴のたびに音声を再ロードしないよう、読み込み済みハンドルを使う。
    bool PlayAudioPreview(const std::string& relativePath, bool isBgm);

    // 既存シーンへ一時配置するので、シーンと描画共通資源がある場合だけ進める。
    void CreateModelPreview(const std::string& relativePath);

    // 走査中に配列を変更しないよう対象を先に集め、シーンへ削除を予約する。
    void RemoveModelPreviews();

    // エディター専用の一時 Object だけ数え、通常のシーン資産は含めない。
    int CountModelPreviews() const;

    // カテゴリや参照元も検索対象に含め、一覧ごとに別の検索処理を持たない。
    bool MatchesSearch(const nlohmann::json& item) const;

    /// 保護パスを避けながら、選択ファイルと同名DDSなどの相方ファイルをまとめて削除する。
    bool DeleteAssetFiles(const std::string& relativePath, std::vector<std::string>& deletedPaths, std::string& errorMessage);

    // 再監査を待たずに一覧から消すが、更新するのは今回削除したパスだけに限る。
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
    std::future<std::uint32_t> auditFuture_; // 非同期の監査プロセス終了待ち。

    std::map<std::string, uint32_t> previewTextureHandles_;
    std::map<std::string, uint32_t> previewAudioHandles_;
};
