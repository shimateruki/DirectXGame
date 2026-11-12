#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace engine {
class Object3D;

// 担当：ゲーム固有Inspectorの描画コールバックを登録して呼び出す。
// 関連：InspectorWindow（呼出側）、Object3D（借用する編集対象）
// 主な入口：Register / Draw / Entries
// 対象と設定値の所有・保存は行わない。
class InspectorExtensionRegistry {
public:
    // 既存の表示位置を維持する。ItemはStatsが開かれ、ClassがItemの場合だけ呼ばれる。
    enum class Slot { Gameplay, Item };

    /// <summary>起動時に対象型と描画処理を登録。同じIDは元の順番で置換し、重複表示を防ぐ。</summary>
    template<class Owner>
    static bool Register(std::string id, Slot slot, std::function<void(Owner&)> draw) {
        if (id.empty() || !draw) return false;
        Entry entry{std::move(id), slot, [draw = std::move(draw)](Object3D& object) {
            // 現在の選択Objectを毎回判定し、対応型だけへ渡す。Objectのアドレスは登録しない。
            if (auto* owner = dynamic_cast<Owner*>(&object)) draw(*owner);
        }};
        auto& entries = Entries();
        const auto found = std::find_if(entries.begin(), entries.end(),
            [&entry](const Entry& value) { return value.id == entry.id; });
        if (found == entries.end()) entries.push_back(std::move(entry));
        else *found = std::move(entry);
        return true;
    }

    /// <summary>代表選択Objectだけを描く。UI自身の保存・無効化・ImGui IDの扱いを維持する。</summary>
    static void Draw(Slot slot, Object3D* target) {
        if (!target) return;
        for (const auto& entry : Entries()) {
            if (entry.slot == slot) entry.draw(*target);
        }
    }

private:
    // 専用Inspectorの識別子・表示枠と描画コールバック。
    // 関連：InspectorExtensionRegistry（この型を使う機能の本体）
    struct Entry {
        std::string id; // UI登録の識別子。JSONのキーやObject名としては使わない。
        Slot slot;
        std::function<void(Object3D&)> draw;
    };

    // 登録は初期化時だけに行い、描画中に追加・置換しない。戻り値を外部へ公開しない。
    static std::vector<Entry>& Entries() {
        static std::vector<Entry> entries;
        return entries;
    }
};
} // namespace engine
