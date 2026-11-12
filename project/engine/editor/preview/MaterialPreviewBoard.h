#pragma once

#include "IEditable.h"
#include <string>
#include <vector>

class SceneManager;
class DebugEditor;
namespace engine { class Object3D; }

// 担当：同じ条件で材質を並べ、モデルや描画表現の比較用配置を作る。
// 関連：Object3D / MeshRenderer（表示）、SceneManager（配置）、DebugEditor（UI）
// 主な入口：Initialize / DrawImGui
class MaterialPreviewBoard : public IEditable {
public:
    // 配置先SceneとEditorを借用し、使えるモデル候補を最初に集めます。
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);

    // このツールの設定・操作UIをImGuiで描画する。
    void DrawImGui() override;

    std::string GetName() override { return "Material Preview Board"; }

private:
    /// プレビュー配置する1マス分のマテリアル種別、表示名、エフェクト設定を保持する。
    // 関連：MaterialPreviewBoard（この型を使う機能の本体）
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

    // 現行Scene内のプレビュー専用Objectだけを数え、通常配置は含めません。
    int CountBoardObjects() const;

    std::vector<MaterialPreviewEntry> GetEntries() const;

    // 基本形状を残した上で利用可能なモデルを追加し、選択候補を一意にします。
    void RefreshModelCandidates();

    void SetPreviewModel(const std::string& modelName);

    // 比較用モデルの選択UIを描画する。
    void DrawModelSelector();

    /// 各プレビューObjectへマテリアル番号、色、特殊効果の初期値を設定する。
    void ApplyPreviewDefaults(engine::Object3D* object, const MaterialPreviewEntry& entry) const;

private:
    SceneManager* sceneManager_ = nullptr; // 現在シーンの取得元。借用参照。
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
