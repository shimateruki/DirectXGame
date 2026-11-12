#pragma once
#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <algorithm>
#include "BaseGimmick.h"
#include "Object3DCommon.h"

// 文字列のギミックタイプ名から、対応するギミックを生成するファクトリ
// 関連：登録された生成関数と型判定を保持し、LevelLoaderやEditorの生成要求へ応える。
// 主な入口：CreateGimmick / GetRegisteredTypes
class GimmickFactory {
  public:
    /// <summary>共有のギミックファクトリを返す。</summary>
    static GimmickFactory* GetInstance();

    /// <summary>種別名に対応するギミックを生成し、基本初期化する。</summary>
    /// <returns>未対応の種別名では仮表示用のBaseGimmickを返す。</returns>
    std::unique_ptr<BaseGimmick> CreateGimmick(const std::string& gimmickName, engine::Object3DCommon* common);

    // 保存用のタイプID、Editor表示名、既存Objectの型を照合する関数。
    struct TypeInfo { std::string id; std::string displayName; std::function<bool(const engine::Object3D&)> matches; };

    /// ゲーム側の起動登録です。保存IDとモデルはギミックのクラス、日本語名は登録側が指定します。
    template<class T>
    bool RegisterGimmick(const std::string& displayName, const char* typeId = T::kTypeId,
                         const char* model = T::kModelPath, std::function<void(T&)> configure = {}) {
        if (displayName.empty() || std::any_of(registered_.begin(), registered_.end(),
            [typeId](const Entry& entry) { return entry.info.id == typeId; })) return false;
        registered_.push_back({{typeId, displayName, [](const engine::Object3D& object) {
            return dynamic_cast<const T*>(&object) != nullptr;
        }}, [model = std::string(model), configure](engine::Object3DCommon* common) {
            auto object = std::make_unique<T>();
            object->Initialize(common, model);
            if (configure) configure(*object);
            return object;
        }});
        return true;
    }

    std::vector<TypeInfo> GetRegisteredTypes() const {
        std::vector<TypeInfo> types;
        for (const auto& entry : registered_) types.push_back(entry.info);
        return types;
    }

    /// Editorでは未知IDを仮Objectへ置き換えず、種類変更を中止するために使います。
    bool IsKnownType(const std::string& type) const;

    /// Editorの型復元用です。編集で書き換わる種類名ではなく、生成した実クラスからIDを返します。
    std::string GetCreationType(const BaseGimmick& object) const;

  private:
    GimmickFactory();

    // 登録された一種類の生成関数と、一覧・型照合用の情報。
    struct Entry {
        TypeInfo info;
        std::function<std::unique_ptr<BaseGimmick>(engine::Object3DCommon*)> create;
    };
    std::vector<Entry> registered_;


    ~GimmickFactory() = default;

    GimmickFactory(const GimmickFactory&) = delete;

    const GimmickFactory& operator=(const GimmickFactory&) = delete;
};
