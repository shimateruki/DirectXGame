#pragma once

#include "IEditable.h"
#include "json.hpp"

#include <string>
#include <vector>

class DebugEditor;
namespace engine { class Object3d; }
namespace engine { struct Vector2; }
namespace engine { struct Vector3; }

/// モデルの解析、LOD生成、生成結果レビュー、シーンへのLOD設定反映を行うエディタウィンドウ。
class ModelOptimizerWindow : public IEditable {
public:
    /// 選択ObjectやシーンへLOD設定を反映するため、DebugEditor参照を保持する。
    void Initialize(DebugEditor* editor);

    /// モデルの解析、生成物の採否、LOD プレビューの操作を表示します。
    void DrawImGui() override;

    /// ラベル投影に使うゲーム画面の位置と大きさを更新します。
    void SetGameViewRegion(const engine::Vector2& offset, const engine::Vector2& size);
    std::string GetName() override { return "モデル最適化 (Model Optimizer)"; }

private:
    /// Resources/3DModel配下から最適化対象にできるモデル候補を収集する。
    void RefreshModelList();
    /// UI とドラッグ操作の両方からモデル名を設定します。
    void SetModelName(const std::string& modelName);
    /// 外部LODビルダーを起動し、解析のみまたはLOD生成を非同期で実行する。
    bool RunBuilder(bool analyzeOnly);
    // 外部プロセスの終了を監視し、結果を読み取ってハンドルを解放します。
    void UpdateBuilderProcess();
    void FinishBuilderProcess(unsigned long exitCode);
    void CloseBuilderProcessHandles();
    // 生成された LOD をエディター専用オブジェクトとして段階的に配置します。
    void UpdatePreviewCreation();
    void DrawPreviewLabels();
    bool ProjectWorldToGameView(const engine::Vector3& world, engine::Vector2& screenOut) const;
    /// 生成ツールが出力した最新レポートを読み込みます。
    bool LoadLatestReport();
    /// 生成レポートのLOD設定を現在選択中のObjectへ適用する。
    bool ApplyLodConfigToSelected();
    bool ApplyLodConfigToObject(engine::Object3d* object);
    /// 生成したLODファイルを採用し、対象モデルのLOD manifestとして使える状態にする。
    bool AcceptGeneratedLods();
    /// 生成物を採用せず、安全性検証を通った生成ファイルだけを取り除きます。
    bool RejectGeneratedLods();
    int DeleteGeneratedLodFilesFromReport();
    void CreatePreviewObjects();
    void RemovePreviewObjects();
    std::string BuildReportSummary() const;
    std::string GetLodModelName(int lodLevel) const;
    int CountPreviewObjects() const;

private:
    // モデル選択、LOD の初期値、外部ビルダーの操作状態。
    DebugEditor* editor_ = nullptr;
    std::vector<std::string> modelCandidates_;
    int selectedModelIndex_ = -1;
    char modelNameBuffer_[256] = "";
    float lodRatios_[2] = { 0.55f, 0.25f };
    float lodDistances_[2] = { 35.0f, 70.0f };
    int selectedBackendIndex_ = 0;
    char blenderPathBuffer_[512] = "";
    bool builderRunning_ = false;
    bool builderAnalyzeOnly_ = false;
    void* builderProcessHandle_ = nullptr;
    void* builderThreadHandle_ = nullptr;
    unsigned long builderProcessId_ = 0;
    bool forceOverwrite_ = true;
    bool autoUseEffectPreviewStage_ = true;
    bool showPreviewLabels_ = true;
    /// 一フレームにまとめて生成せず、順次プレビューへ追加する項目。
    struct PendingPreviewItem {
        int level = 0;
        std::string modelName;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };
    std::vector<PendingPreviewItem> pendingPreviewItems_;
    size_t pendingPreviewIndex_ = 0;
    bool previewCreationActive_ = false;
    engine::Object3d* firstPendingPreview_ = nullptr;
    // ゲーム画面の投影領域と、生成結果の採否待ち状態。
    float gameViewOffsetX_ = 0.0f;
    float gameViewOffsetY_ = 0.0f;
    float gameViewWidth_ = 1280.0f;
    float gameViewHeight_ = 720.0f;
    bool hasPendingGeneratedReview_ = false;
    engine::Object3d* pendingApplyTarget_ = nullptr;
    nlohmann::json latestReport_;
    bool hasReport_ = false;
    std::string lastStatus_ = "モデルを選んで解析またはLOD生成を実行してください。";
};
