#pragma once

#include "json.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Object3d;

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
    std::function<bool(const Object3d&)> applicable;
    std::function<bool(const Object3d&)> present;
    EditorComponentInspectorMode inspectorMode = EditorComponentInspectorMode::Automatic;
    std::function<bool(Object3d&)> add;
    std::function<bool(Object3d&)> remove;
    /// Prefab/Scene JSONからComponentの存在状態を判定します。旧形式は値から推測し、明示Markerを優先します。
    std::function<bool(const nlohmann::json&)> serializedPresent;
    /// Prefab継承用にComponentの存在状態をJSONへ明示します。削除時は実行時機能も無効化します。
    std::function<void(nlohmann::json&, bool)> setSerializedPresent;
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
    std::function<nlohmann::json(const Object3d&)> getter;
    std::function<bool(Object3d&, const nlohmann::json&)> setter;
    /// ObjectのClassやComponent状態に応じて、このPropertyを表示できるか判定します。
    std::function<bool(const Object3d&)> applicable;
    /// Integer PropertyをCombo表示する場合の選択肢です。
    std::vector<std::string> enumLabels;
    /// Propertyが属する安定したComponent Type IDです。
    std::string componentTypeId;
    /// Scene/Prefab JSONで使う保存パスです。空なら保存対象外です。
    std::string serializedPath;
    /// 旧Assetに値が存在しない場合の既定値です。nullは未定義を表します。
    nlohmann::json defaultValue;
    /// InspectorとProperty Matrixが共有するUI表示情報です。
    EditorPropertyUiHints ui;
};

/// Inspector、Prefab、Sequencerから共通利用するObject3dプロパティ台帳です。
class EditorPropertyRegistry {
public:
    static EditorPropertyRegistry* GetInstance();

    void InitializeBuiltInProperties();
    bool RegisterComponent(EditorComponentDescriptor descriptor);
    bool Register(EditorPropertyDescriptor descriptor);
    const EditorComponentDescriptor* FindComponent(const std::string& typeId) const;
    const EditorPropertyDescriptor* Find(const std::string& path) const;
    const std::vector<EditorComponentDescriptor>& GetComponents() const { return components_; }
    const std::vector<EditorPropertyDescriptor>& GetProperties() const { return properties_; }
    std::vector<const EditorComponentDescriptor*> GetComponentsForObject(const Object3d* object) const;
    std::vector<const EditorPropertyDescriptor*> GetPropertiesForComponent(const std::string& typeId) const;
    bool IsComponentApplicable(const Object3d* object, const std::string& typeId) const;
    bool IsComponentPresent(const Object3d* object, const std::string& typeId) const;
    bool IsComponentPresent(const nlohmann::json& serializedObject, const std::string& typeId) const;
    bool SetComponentPresent(nlohmann::json& serializedObject, const std::string& typeId, bool present) const;
    bool AddComponent(Object3d* object, const std::string& typeId) const;
    bool RemoveComponent(Object3d* object, const std::string& typeId) const;

    nlohmann::json GetValue(const Object3d* object, const std::string& path) const;
    bool SetValue(Object3d* object, const std::string& path, const nlohmann::json& value) const;
    bool IsApplicable(const Object3d* object, const std::string& path) const;
    bool HasMixedValue(const std::vector<Object3d*>& objects, const std::string& path) const;

private:
    bool initialized_ = false;
    std::vector<EditorComponentDescriptor> components_;
    std::unordered_map<std::string, std::size_t> componentIndices_;
    std::vector<EditorPropertyDescriptor> properties_;
    std::unordered_map<std::string, std::size_t> propertyIndices_;
};
