#pragma once

#include "IEditable.h"
#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.h"

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
    /// 選択中のモデルだけに使う、視点操作付きプレビューの描画元を登録する。
    void SetPreviewProvider(std::function<uint64_t(const std::string&, float, float, float)> provider);

    /// パレットと設定ペインを描画する。
    void DrawImGui() override;

    /// Editor のウィンドウ名を返す。
    std::string GetName() override;

    /// ProjectとInspectorで、同じカタログ・選択・配置操作を共有します。
    void DrawLibrary(bool fromProject = false);
    /// 下部のAssetドックを広げる予約を、レイアウト管理側で1回だけ受け取ります。
    bool ConsumeLibraryDockExpandRequest() { const bool requested = requestLibraryDockExpand_; requestLibraryDockExpand_ = false; return requested; }
    /// 選択プリセットの変化を通知する処理を登録する。
    void SetSelectionCallback(std::function<void()> callback);
    /// 更新されたサムネイルのキャッシュを破棄する処理を登録する。
    void SetThumbnailInvalidator(std::function<void(const std::string&)> callback);
    const std::string& GetSelectedPresetName() const { return selectedName_; }
    /// 保存キーと JSON からパレットに表示する名前を決める。
    std::string GetDisplayName(const std::string& key, const json& data) const;
    /// プリセットを選択する。notify が true のとき選択変更を通知する。
    void SelectPreset(const std::string& name, bool notify = true);
    /// 使用したプリセットを最近使った一覧へ反映する。
    void NotifyPresetUsed(const std::string& name);


private:
    enum class Category {
        All,
        Stage1,
        Enemy,
        Gimmick,
        Item,
        Model
    };

    // 保存に使う識別文字列と UI に表示するラベルの対応。
    struct TypeOption {
        const char* value;
        const char* label;
    };

    PresetEditor() = default;
    ~PresetEditor() = default;

    // パレット表示と配置操作。
    void DrawPalettePane();

    /// カテゴリの選択ボタンを描画する。
    void DrawCategoryButton(const char* label, Category category);

    /// 新規プリセットを作る操作を表示する。
    void DrawCreateButtons();

    /// 検索・カテゴリで絞ったプリセット一覧を描画する。
    void DrawPresetList();

    /// 一つのプリセットのサムネイルと選択操作を描画する。
    void DrawPresetListItem(const std::string& name, const json& data);

    /// プリセットのサムネイルボタンを描画する。
    void DrawThumbnailButton(const std::string& name, const ImVec4& accent, float size = 128.0f);

    // 選択中プリセットの詳細編集。
    void DrawSettingsPane();

    // 種別ごとのパラメータ編集。
    /// 敵固有の JSON パラメータを編集し、変更があれば true を返す。
    bool DrawEnemySettings(json& data);

    /// 現在の敵種別に不要な管理項目を取り除く。
    bool RemoveManagedEnemyFields(json& data);

    /// ギミック固有の JSON パラメータを編集する。
    bool DrawGimmickSettings(json& data);

    /// アイテム固有の JSON パラメータを編集する。
    bool DrawItemSettings(json& data);

    /// カテゴリに対応する種別候補を表示し、選択値を JSON へ反映する。
    bool DrawTypeCombo(const char* label, json& data, json& param, const char* key, const std::vector<TypeOption>& options, Category category);

    /// カテゴリ切替時に表示しない保存項目を整理する。
    void ApplyHiddenCategoryFields(json& data, Category category);

    /// 種別を変更した際の既定パラメータを設定する。
    void ApplyTypeDefaults(json& data, Category category, const std::string& type);

    /// 指定した形状と大きさで当たり判定の既定値を入れる。
    void SetColliderDefaults(json& data, int type, const engine::Vector3& size);

    /// モデル参照を編集し、変更があれば true を返す。
    bool DrawModelField(json& data);

    /// 当たり判定を編集し、変更があれば true を返す。
    bool DrawCollider(json& data);

    // JSON値を型安全に編集する共通UI。
    bool DrawStringField(json& data, const char* label, const char* key, const std::string& defaultValue);

    bool DrawFloatField(json& data, const char* label, const char* key, float defaultValue, float speed);

    bool DrawIntField(json& data, const char* label, const char* key, int defaultValue);

    bool DrawColorField(json& data, const char* label, const char* key, const engine::Vector4& defaultValue);

    bool DrawVector3Field(json& data, const char* label, const char* key, const engine::Vector3& defaultValue, float speed);

    bool DrawParamFloat(json& param, const char* label, const char* key, float defaultValue, float speed);

    bool DrawParamInt(json& param, const char* label, const char* key, int defaultValue);

    bool DrawParamBool(json& param, const char* label, const char* key, bool defaultValue);

    bool DrawParamIntCombo(json& param, const char* label, const char* key, int defaultValue, const std::vector<const char*>& labels);

    /// プリセット JSON にパラメータオブジェクトがなければ作成して返す。
    json& EnsureParam(json& data);

    // 新規定義の生成と削除予約。
    /// 指定カテゴリの空プリセットを追加する。
    void AddBlankPreset(Category category);

    /// 名前とカテゴリから保存用の初期 JSON を作る。
    json BuildBlankPreset(const std::string& name, Category category) const;

    /// 削除確認後に保留していたプリセット削除を実行する。
    void HandleDeferredDelete();

    // 検索・分類・表示用カタログ情報。
    /// パレットで選べるモデル一覧を再収集する。
    void RefreshModelList();

    /// 名前と内容が現在の検索語に一致するか返す。
    bool MatchesSearch(const std::string& name, const json& data) const;

    /// プリセットが指定カテゴリに属するか返す。
    bool MatchesCategory(const json& data, Category category) const;

    /// ステージ用パレットに載せるプリセットか判定する。
    bool IsStageLibraryPreset(const json& data) const;

    /// 保存データから表示カテゴリを推定する。
    Category DetectCategory(const json& data) const;

    /// カテゴリの UI 表示名を返す。
    const char* GetCategoryLabel(Category category) const;

    /// カテゴリに割り当てる UI 色を返す。
    ImVec4 GetCategoryColor(Category category) const;

    /// ステージ1の区画番号に割り当てる UI 色を返す。
    ImVec4 GetStage1SectionColor(int sectionOrder) const;

    /// モデルのパスから一覧用の短い名前を作る。
    std::string ShortModelName(const std::string& modelName) const;

    std::string ReadString(const json& data, const char* key, const std::string& defaultValue) const;

    float ReadFloat(const json& data, const char* key, float defaultValue) const;

    int ReadInt(const json& data, const char* key, int defaultValue) const;

    bool ReadBool(const json& data, const char* key, bool defaultValue) const;

    engine::Vector3 ReadVector3(const json& data, const char* key, const engine::Vector3& defaultValue) const;

    std::string ToLower(const std::string& value) const;

    std::vector<TypeOption> GetEnemyOptions() const;

    std::vector<TypeOption> GetGimmickOptions() const;

    std::vector<TypeOption> GetItemOptions() const;

private:
    // コールバックはPresetEditorが所有せず、DebugEditor側の操作へ委譲する。
    std::function<void(const std::string&)> placePresetCallback_;
    std::function<void(const std::string&)> brushPresetCallback_;
    std::function<uint64_t(const std::string&)> thumbnailProvider_;
    std::function<uint64_t(const std::string&, float, float, float)> previewProvider_;
    float previewYaw_ = 0.0f;
    float previewPitch_ = 0.0f;
    float previewZoom_ = 1.0f;
    std::string previewSelectionName_;
    std::vector<std::string> modelNames_;
    std::string selectedName_;
    Category activeCategory_ = Category::Stage1;
    // ステージ別パレットの選択。種類・検索条件とは独立して保持します。
    std::string stageCollection_ = "";
    char searchBuffer_[128]{};
    char newName_[64] = "NewPreset";
    bool requestDelete_ = false;
    std::string pendingLibraryDeleteName_;
    bool bulkDeleteMode_ = false;
    std::vector<std::string> bulkDeleteSelection_;
    std::function<void()> selectionCallback_;
    std::function<void(const std::string&)> thumbnailInvalidator_;
    std::vector<std::string> recentPresets_;
    int libraryCategory_ = 0;
    int thumbnailSize_ = 128;
    bool listView_ = false;
    bool requestLibraryDockExpand_ = false;
    bool selectionFromProject_ = false;

};
