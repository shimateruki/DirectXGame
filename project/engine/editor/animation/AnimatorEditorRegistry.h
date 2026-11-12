#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

class AnimatorControllerRuntime;
struct AnimatorStateDefinition;
struct AnimatorParameterDefinition;

namespace engine {
class Object3D;

// 担当：ゲーム固有Animatorの編集コールバックをEditorへ登録する。
// 関連：AnimationClipEditor（呼出側）、Object3D（編集対象）
// 主な入口：Register / Entries / Find
// ObjectとRuntimeは所有せず、コールバック呼出し中だけ借りる。
class AnimatorEditorRegistry {
public:
    using SaveAction = std::function<void()>;

    // Animatorの保存名、対応対象判定と専用編集・再読込の接続。
    // 関連：AnimatorEditorRegistry（この型を使う機能の本体）
    struct Entry {
        // Controllerのファイル名（拡張子なし）。JSONの保存形式は変更しない。
        std::string assetName;

        // 同じAssetを開いていても、対応する型以外は通常Objectの編集を続ける。
        std::function<bool(Object3D&)> accepts;

        // 保存成功時に現在シーンの対応Objectへ通知する。選択対象だけに限定しない。
        std::function<void(Object3D&)> reload;

        // 専用プレビュー領域を描く。未選択Stateはnullptr、saveは同期呼出し専用。
        std::function<void(Object3D&, const AnimatorStateDefinition*, const SaveAction&)> drawPreview;

        // Stateの注意書きはObject未選択でも表示できる。
        std::function<void(const AnimatorStateDefinition&)> drawStateHelp;

        // ゲーム固有のRuntimeを借りる。未読込ならnullptrを返す。
        std::function<AnimatorControllerRuntime*(Object3D&)> getRuntime;

        // 特殊なParameter表示を処理したらtrue。falseなら共通の値編集を使う。
        std::function<bool(AnimatorControllerRuntime&, const AnimatorParameterDefinition&)> drawRuntimeParameter;
    };

    /// <summary>起動時に登録。同じAssetの再登録は置換する。ObjectやEditor表示中には登録しない。</summary>
    static bool Register(Entry entry) {
        if (entry.assetName.empty() || !entry.accepts) return false;
        const std::string key = entry.assetName;
        Entries().insert_or_assign(key, std::move(entry));
        return true;
    }

    /// <summary>Asset固有の注意書き用。戻り値は登録の置換をまたいで保持しない。</summary>
    static const Entry* Find(const std::string& assetName) {
        const auto& entries = Entries();
        const auto found = entries.find(assetName);
        return found == entries.end() ? nullptr : &found->second;
    }

    /// <summary>毎回現在のObjectを判定する。型が違えば専用処理へ渡さない。</summary>
    static const Entry* Find(const std::string& assetName, Object3D& target) {
        const auto* entry = Find(assetName);
        return entry && entry->accepts(target) ? entry : nullptr;
    }

private:
    static std::unordered_map<std::string, Entry>& Entries() {
        static std::unordered_map<std::string, Entry> entries;
        return entries;
    }
};
} // namespace engine
