#pragma once

#include "IEditable.h"
#include "PresetManager.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct ImVec4;

/// 配置プリセットの検索、作成、編集、配置操作をまとめるEditorウィンドウ。
/// UI実装は責務別のcppへ分離し、このヘッダーは公開契約と状態だけを保持します。
class PresetEditor : public IEditable {
public:
    /// 配置プリセット編集ウィンドウの共有インスタンスを返す。
    static PresetEditor* GetInstance();

    /// プリセット定義とモデル一覧を読み込み、編集開始時の状態を整える。
    void Initialize();

    /// 選択プリセットをGame Viewへ配置する処理を外部から受け取る。
    void SetPlacePresetCallback(std::function<void(const std::string&)> callback);

    /// 連続配置ブラシへ切り替える処理を外部から受け取る。
    void SetBrushPresetCallback(std::function<void(const std::string&)> callback);

    /// プリセット名からサムネイルTexture IDを解決する処理を登録する。
    void SetThumbnailProvider(std::function<uint64_t(const std::string&)> provider);

    /// パレットと設定ペインを描画する。
    void DrawImGui() override;

    std::string GetName() override;

private:
    enum class Category {
        All,
        Enemy,
        Gimmick,
        Item,
        Model
    };

    struct TypeOption {
        std::string value;
        std::string label;
    };

    PresetEditor() = default;
    ~PresetEditor() = default;

    // パレット表示と配置操作。
    void DrawPalettePane();

    void DrawCategoryButton(const char* label, Category category);

    void DrawCreateButtons();

    void DrawPresetList();

    void DrawPresetListItem(const std::string& name, const json& data);

    void DrawThumbnailButton(const std::string& name, const ImVec4& accent);

    // 選択中プリセットの詳細編集。
    void DrawSettingsPane();

    // 種別ごとのパラメータ編集。
    bool DrawEnemySettings(json& data);

    bool RemoveManagedEnemyFields(json& data);

    bool DrawGimmickSettings(json& data);

    bool DrawItemSettings(json& data);

    bool DrawTypeCombo(const char* label, json& data, json& param, const char* key, const std::vector<TypeOption>& options, Category category);

    void ApplyHiddenCategoryFields(json& data, Category category);

    void ApplyTypeDefaults(json& data, Category category, const std::string& type);

    void SetColliderDefaults(json& data, int type, const Vector3& size);

    bool DrawModelField(json& data);

    bool DrawCollider(json& data);

    // JSON値を型安全に編集する共通UI。
    bool DrawStringField(json& data, const char* label, const char* key, const std::string& defaultValue);

    bool DrawFloatField(json& data, const char* label, const char* key, float defaultValue, float speed);

    bool DrawIntField(json& data, const char* label, const char* key, int defaultValue);

    bool DrawColorField(json& data, const char* label, const char* key, const Vector4& defaultValue);

    bool DrawVector3Field(json& data, const char* label, const char* key, const Vector3& defaultValue, float speed);

    bool DrawParamFloat(json& param, const char* label, const char* key, float defaultValue, float speed);

    bool DrawParamInt(json& param, const char* label, const char* key, int defaultValue);

    bool DrawParamBool(json& param, const char* label, const char* key, bool defaultValue);

    bool DrawParamIntCombo(json& param, const char* label, const char* key, int defaultValue, const std::vector<const char*>& labels);

    json& EnsureParam(json& data);

    // 新規定義の生成と削除予約。
    void AddBlankPreset(Category category);

    json BuildBlankPreset(const std::string& name, Category category) const;

    void HandleDeferredDelete();

    // 検索・分類・表示用カタログ情報。
    void RefreshModelList();

    bool MatchesSearch(const std::string& name, const json& data) const;

    bool MatchesCategory(const json& data, Category category) const;


    Category DetectCategory(const json& data) const;

    const char* GetCategoryLabel(Category category) const;

    ImVec4 GetCategoryColor(Category category) const;


    std::string ShortModelName(const std::string& modelName) const;

    std::string ReadString(const json& data, const char* key, const std::string& defaultValue) const;

    float ReadFloat(const json& data, const char* key, float defaultValue) const;

    int ReadInt(const json& data, const char* key, int defaultValue) const;

    bool ReadBool(const json& data, const char* key, bool defaultValue) const;

    Vector3 ReadVector3(const json& data, const char* key, const Vector3& defaultValue) const;

    std::string ToLower(const std::string& value) const;

    std::vector<TypeOption> GetEnemyOptions() const;

    std::vector<TypeOption> GetGimmickOptions() const;

    std::vector<TypeOption> GetItemOptions() const;

private:
    // コールバックはPresetEditorが所有せず、DebugEditor側の操作へ委譲する。
    std::function<void(const std::string&)> placePresetCallback_;
    std::function<void(const std::string&)> brushPresetCallback_;
    std::function<uint64_t(const std::string&)> thumbnailProvider_;
    std::vector<std::string> modelNames_;
    std::string selectedName_;
    Category activeCategory_ = Category::All;
    char searchBuffer_[128]{};
    char newName_[64] = "NewPreset";
    bool requestDelete_ = false;
};
