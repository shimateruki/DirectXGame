#pragma once

#include "json.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine { class Object3d; }

/// Component追加時に選べる初期設定Presetです。
/// Inspector側はこの登録を列挙するだけで、Component固有の分岐を持ちません。
struct EditorComponentPresetDescriptor {
    std::string id;
    std::string displayName;
    std::string description;
    std::function<bool(engine::Object3d&)> apply;
};

/// ComponentのProperty欄を自動生成するか、既存の専用UIへ任せるかを表します。
enum class EditorComponentInspectorMode {
    Automatic,
    Custom,
};

/// Editorと保存処理が共有するComponent型情報です。
struct EditorComponentDescriptor {
    std::string typeId;
    std::string displayName;
    std::string description;
    int displayOrder = 0;
    bool required = false;
    bool removable = false;
    std::function<bool(const engine::Object3d&)> applicable;
    std::function<bool(const engine::Object3d&)> present;
    EditorComponentInspectorMode inspectorMode = EditorComponentInspectorMode::Automatic;
    std::function<bool(engine::Object3d&)> add;
    std::function<bool(engine::Object3d&)> remove;
    /// Prefab/Scene JSONからComponentの存在状態を判定します。旧形式は値から推測し、明示Markerを優先します。
    std::function<bool(const nlohmann::json&)> serializedPresent;
    /// Prefab継承用にComponentの存在状態をJSONへ明示します。削除時は実行時機能も無効化します。
    std::function<void(nlohmann::json&, bool)> setSerializedPresent;
    /// Collider Presetなど、Component固有の追加候補です。
    std::vector<EditorComponentPresetDescriptor> presets;
    /// 専用Inspectorだけで扱う内部Componentはfalseにします。
    bool showInInspector = true;
};

/// Editor上で扱うObject3dプロパティの型です。
enum class EditorPropertyType {
    Bool,
    Integer,
    Number,
    String,
    Vector2,
    Vector3,
    Vector4,
};

/// Registryから共通ImGui Drawerへ渡す表示方法です。
/// Property固有の見た目をDrawer側のPath分岐に増やさないため、型情報と一緒に管理します。
struct EditorPropertyUiHints {
    bool configured = false;
    bool useSlider = false;
    bool displayAsDegrees = false;
    bool useColorPicker = false;
    float speed = 0.05f;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    std::string format;
};

enum class EditorPropertyFlags : std::uint32_t {
    None = 0,
    MultiEdit = 1u << 0,
    PrefabOverride = 1u << 1,
    Animatable = 1u << 2,
    ReadOnly = 1u << 3,
};

inline EditorPropertyFlags operator|(EditorPropertyFlags lhs, EditorPropertyFlags rhs) {
    return static_cast<EditorPropertyFlags>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

inline bool HasEditorPropertyFlag(EditorPropertyFlags value, EditorPropertyFlags flag) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

/// 1つの編集可能プロパティを、安定したパスとGetter/Setterで表します。
struct EditorPropertyDescriptor {
    std::string path;
    std::string displayName;
    std::string category;
    EditorPropertyType type = EditorPropertyType::String;
    EditorPropertyFlags flags = EditorPropertyFlags::None;
    std::function<nlohmann::json(const engine::Object3d&)> getter;
    std::function<bool(engine::Object3d&, const nlohmann::json&)> setter;
    /// ObjectのClassやComponent状態に応じて、このPropertyを表示できるか判定します。
    std::function<bool(const engine::Object3d&)> applicable;
    /// Integer PropertyをCombo表示する場合の選択肢です。
    std::vector<std::string> enumLabels;
    /// Propertyが属する安定したComponent Type IDです。
    std::string componentTypeId;
    /// Scene/Prefab JSONで使う保存パスです。空なら保存対象外です。
    std::string serializedPath;
    /// 旧Assetに値が存在しない場合の既定値です。nullは未定義を表します。
    nlohmann::json defaultValue;
    /// Inspectorなどの編集画面で共有するUI表示情報です。
    EditorPropertyUiHints ui;
};

/// Inspector、Prefab、Sequencerから共通利用するObject3dプロパティ台帳です。
class EditorPropertyRegistry {
public:
    /// <summary>共有のプロパティ台帳を返します。</summary>
    static EditorPropertyRegistry* GetInstance();

    /// <summary>標準コンポーネントとプロパティを定義順に一度だけ登録します。</summary>
    void InitializeBuiltInProperties();

    /// <summary>コンポーネントの型情報を登録します。</summary>
    /// <param name="descriptor">安定した型IDと追加・削除処理を含む定義。</param>
    bool RegisterComponent(EditorComponentDescriptor descriptor);

    /// <summary>編集可能なプロパティを登録します。</summary>
    /// <param name="descriptor">保存パス、値の取得・設定処理を含む定義。</param>
    bool Register(EditorPropertyDescriptor descriptor);

    /// <summary>型IDからコンポーネント定義を探します。</summary>
    const EditorComponentDescriptor* FindComponent(const std::string& typeId) const;

    /// <summary>コンポーネントに属する追加用プリセットを探します。</summary>
    const EditorComponentPresetDescriptor* FindComponentPreset(
        const std::string& typeId,
        const std::string& presetId) const;

    /// <summary>パスからプロパティ定義を探します。</summary>
    const EditorPropertyDescriptor* Find(const std::string& path) const;

    /// <summary>登録順のコンポーネント定義とプロパティ定義を返します。</summary>
    const std::vector<EditorComponentDescriptor>& GetComponents() const { return components_; }
    const std::vector<EditorPropertyDescriptor>& GetProperties() const { return properties_; }

    /// <summary>対象に適用できるコンポーネントを表示順で返します。</summary>
    std::vector<const EditorComponentDescriptor*> GetApplicableComponentsForObject(const engine::Object3d* object) const;

    /// <summary>対象に実際に存在するコンポーネントを返します。</summary>
    std::vector<const EditorComponentDescriptor*> GetComponentsForObject(const engine::Object3d* object) const;

    /// <summary>指定した型に属するプロパティを登録順で返します。</summary>
    std::vector<const EditorPropertyDescriptor*> GetPropertiesForComponent(const std::string& typeId) const;

    /// <summary>対象の種類にコンポーネントを適用できるか調べます。</summary>
    bool IsComponentApplicable(const engine::Object3d* object, const std::string& typeId) const;

    /// <summary>実体または保存JSONにコンポーネントが存在するか調べます。</summary>
    bool IsComponentPresent(const engine::Object3d* object, const std::string& typeId) const;
    bool IsComponentPresent(const nlohmann::json& serializedObject, const std::string& typeId) const;

    /// <summary>保存JSONのコンポーネント存在マーカーを更新します。</summary>
    bool SetComponentPresent(nlohmann::json& serializedObject, const std::string& typeId, bool present) const;

    /// <summary>対象へコンポーネントを追加し、存在マーカーも反映します。</summary>
    bool AddComponent(engine::Object3d* object, const std::string& typeId) const;

    /// <summary>追加済みコンポーネントに選択したプリセットを適用します。</summary>
    bool ApplyComponentPreset(
        engine::Object3d* object,
        const std::string& typeId,
        const std::string& presetId) const;

    /// <summary>削除可能なコンポーネントを対象から外します。</summary>
    bool RemoveComponent(engine::Object3d* object, const std::string& typeId) const;

    /// <summary>対象のプロパティ値を取得・設定します。</summary>
    nlohmann::json GetValue(const engine::Object3d* object, const std::string& path) const;
    bool SetValue(engine::Object3d* object, const std::string& path, const nlohmann::json& value) const;

    /// <summary>対象にプロパティを表示・編集できるか調べます。</summary>
    bool IsApplicable(const engine::Object3d* object, const std::string& path) const;

    /// <summary>複数対象のプロパティ値に差があるか調べます。</summary>
    bool HasMixedValue(const std::vector<engine::Object3d*>& objects, const std::string& path) const;

private:
    /// <summary>標準定義を所有領域ごとに登録します。登録順はInspectorの表示順に影響します。</summary>
    void RegisterBuiltInComponents();
    void RegisterBuiltInBaseProperties();
    void RegisterBuiltInRenderAndComponentProperties();
    void RegisterBuiltInCameraAndGameplayProperties();

    // 重複登録を防ぎ、各種IDから定義を定数時間で探すための索引を保持します。
    bool initialized_ = false;
    std::vector<EditorComponentDescriptor> components_;
    std::unordered_map<std::string, std::size_t> componentIndices_;
    std::vector<EditorPropertyDescriptor> properties_;
    std::unordered_map<std::string, std::size_t> propertyIndices_;
};
