#pragma once

#include "json.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Object3d;

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
};

/// Inspector、Prefab、Sequencerから共通利用するObject3dプロパティ台帳です。
class EditorPropertyRegistry {
public:
    static EditorPropertyRegistry* GetInstance();

    void InitializeBuiltInProperties();
    bool Register(EditorPropertyDescriptor descriptor);
    const EditorPropertyDescriptor* Find(const std::string& path) const;
    const std::vector<EditorPropertyDescriptor>& GetProperties() const { return properties_; }

    nlohmann::json GetValue(const Object3d* object, const std::string& path) const;
    bool SetValue(Object3d* object, const std::string& path, const nlohmann::json& value) const;
    bool IsApplicable(const Object3d* object, const std::string& path) const;
    bool HasMixedValue(const std::vector<Object3d*>& objects, const std::string& path) const;

private:
    bool initialized_ = false;
    std::vector<EditorPropertyDescriptor> properties_;
    std::unordered_map<std::string, std::size_t> propertyIndices_;
};
