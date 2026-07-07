#pragma once

#include "IEditable.h"
#include <string>
#include <vector>

class SceneManager;
class DebugEditor;
class Object3d;

/// 各マテリアルタイプや特殊表現を同じ条件で並べ、見た目を比較するためのプレビュー板。
class MaterialPreviewBoard : public IEditable {
public:
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "Material Preview Board"; }

private:
    /// プレビュー配置する1マス分のマテリアル種別、表示名、エフェクト設定を保持する。
    struct MaterialPreviewEntry {
        int materialType = 0;
        float effectType = 0.0f;
        std::string label;
        std::string shortLabel;
        std::string modeLabel;
    };

private:
    /// 現在設定から比較用Objectを並べ、マテリアル確認用の盤面を作る。
    void CreateBoard();
    /// プレビュー用に作成したObjectをシーンからまとめて削除する。
    void RemoveBoard();
    int CountBoardObjects() const;
    std::vector<MaterialPreviewEntry> GetEntries() const;
    void RefreshModelCandidates();
    void SetPreviewModel(const std::string& modelName);
    void DrawModelSelector();
    /// 各プレビューObjectへマテリアル番号、色、特殊効果の初期値を設定する。
    void ApplyPreviewDefaults(Object3d* object, const MaterialPreviewEntry& entry) const;

private:
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;
    char modelNameBuffer_[128] = "Primitives/sphere";
    float spacing_ = 3.0f;
    int columns_ = 4;
    bool placeNearSelected_ = true;
    bool useEffectPreviewStage_ = true;
    bool expandModeVariants_ = true;
    bool showOnlySpecialMaterials_ = false;
    std::vector<std::string> modelCandidates_;
    int selectedModelIndex_ = 0;
};
